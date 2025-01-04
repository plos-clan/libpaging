#include "paging.hpp"
using namespace libpaging;

pml5t pml5_t_buffer;
pml4t pml4_t_buffer;
auto  _paging_memcpy_( void    *dest,
                       void    *src,
                       uint64_t size ) -> void *;
auto  _paging_memset_( void    *dest,
                       uint8_t  value,
                       uint64_t size ) -> void *;
Paging::Paging( PagingInitializationInformation *info ) noexcept {
    this->allocater           = info->allocater;
    this->destoryer           = info->destoryer;
    this->physical_to_virtual = info->physical_to_virtual;
    this->virtual_to_physical = info->virtual_to_physical;
    uint64_t rax { }, rbx { }, rcx { }, rdx { }, mop { 0x7 }, sop { 0 };
    __asm__ __volatile__( "cpuid\n\t"
                          : "=a"( rax ), "=b"( rbx ), "=c"( rcx ), "=d"( rdx )
                          : "0"( mop ), "2"( sop ) );

    this->support_5level_paging = ( rcx & ( 1ul << 16ul ) ) != 0;

    if ( this->support_5level_paging ) {
        this->pml5_t            = new ( &pml5_t_buffer ) pml5t { };
        this->kernel_page_table = pml5_t;
    }
    else {
        this->pml4_t            = new ( &pml4_t_buffer ) pml4t { };
        this->kernel_page_table = pml4_t;
    }

    auto level = this->support_5level_paging ? 5 : 4;

    this->kernel_page_table->page_protect( false );

    auto page_size = this->kernel_page_table->check_page_size( info->map_mode );

    // map kernel space's memory
    this->kernel_page_table->map(
        info->start_page,
        (uint64_t)this->physical_to_virtual( info->start_page ),
        ( info->end_page - info->start_page ) / this->kernel_page_table->check_page_size( info->map_mode ),
        pmlxt::PAGE_RW_W | pmlxt::PAGE_PRESENT,
        info->map_mode );
}

uint64_t address { };

using namespace std;
auto pmlxt::map( IN uint64_t physics_address, IN uint64_t virtual_address, IN uint64_t size, IN uint64_t flags, IN MemoryPageType mode ) -> void {
    pml1t  pml1t { };
    pml2t  pml2t { };
    pml3t  pml3t { };
    pml4t  pml4t { };
    pml5t  pml5t { };
    pmlxt *page_table[] {
        &pml1t,
        &pml2t,
        &pml3t,
        &pml4t,
        &pml5t
    };

    auto level     = Paging::support_5level_paging ? 5 : 4;
    auto get_table = [ & ]( IN uint64_t level ) -> pmlxt & {
        return *page_table[ level - 1 ];
    };
    auto map_helper = [ &mode, &get_table, &physics_address, &virtual_address, flags ]( this auto &self, uint64_t level, pmlxt &pmlx_t ) {
        // First, get the next page table's entry index in current page table
        auto index = pmlx_t.get_address_index_in( reinterpret_cast< void * >( virtual_address ) );

        // Second, if level satisfy our needs
        // Enter handler.
        if ( level == static_cast< uint64_t >( mode ) ) {
            if ( mode != PAGE_4K ) {
                // User need to map huge page
                auto check_next_table_under_the_old = [ get_table ]( this auto &self, uint64_t level, uint64_t index, pmlxt &pmlx_t ) -> void {
                    // Check whether there are other page tables that under the old page table is referenced by the old entry or not.

                    if ( pmlx_t.get_table( )[ index ]         // The entry isn't NULL,
                         && !pmlx_t.flags_ps_pat( index )     // PS bit isn't set, that means there is a page table that is referenced by this entry. (If the next rule 'level != 1' is true)
                         && level != 1 ) {
                        get_table( level - 1 ) = (uint64_t)Paging::physical_to_virtual( pmlx_t.flags_base( index, PAGE_4K ) );
                        for ( auto i = 0; i < 512; ++i ) {
                            self( level - 1, i, get_table( level ) );
                        }
                        // After we delete the all of page tables that under the old page table,
                        // We also ought to delete this old page table
                        Paging::destoryer( Paging::virtual_to_physical( (uint64_t)get_table( level - 1 ).get_table( ) ) );
                    }
                    else if ( level == 1 ) {
                        // Delete this page table
                        // Then return. Because there is no page table under the pml1t.
                        Paging::destoryer( Paging::virtual_to_physical( (uint64_t)pmlx_t.get_table( ) ) );
                    }
                    return;
                };
                check_next_table_under_the_old( level, index, pmlx_t );
            }
            // After checking, now set the entry.
            pmlx_t = { index,                          // The entry's index in the current page table
                       physics_address & ~0x7FFul,     // The low 12 bits is ignoreded, and the further handle is in the pmlx_t's operator= function.
                       flags | get_table( level ).is_huge( mode ),
                       mode };
            physics_address += get_table( level ).check_page_size( mode );
            virtual_address += get_table( level ).check_page_size( mode );
            return;
        }
        else if ( !pmlx_t.flags_p( index ) || pmlx_t.flags_ps_pat( index ) ) {
            // P bit isn't set or ps bit is set, that means the entry don't point to any page tables
            auto new_ = Paging::allocater( PT_SIZE );
            _paging_memset_( Paging::physical_to_virtual( (uint64_t)new_ ), 0, pmlx_t.PT_SIZE );
            pmlx_t = {
                index,                                                   // The entry's index in the current page table
                ( reinterpret_cast< uint64_t >( new_ ) & ~0x7FFul ),     // The low 12 bits is ignoreded, and the further handle is in the pmlx_t's operator= function.
                flags,
                PAGE_4K     // Use PAGE_4K mode to set Beacuse the entry points to a page table rather than a block of memory.
            };
        }

        get_table( level - 1 ) = (uint64_t)Paging::physical_to_virtual( pmlx_t.flags_base( index, PAGE_4K ) );     // Get the next table's base
        self( level - 1, get_table( level - 1 ) );
    };

    while ( size-- ) {
        map_helper( level, *this );
    }
}
auto pmlxt::unmap( IN uint64_t virtual_address, IN size_t size, IN MemoryPageType mode ) -> void {
    pml1t  pml1t { };
    pml2t  pml2t { };
    pml3t  pml3t { };
    pml4t  pml4t { };
    pml5t  pml5t { };
    pmlxt *page_table[] {
        &pml1t,
        &pml2t,
        &pml3t,
        &pml4t,
        &pml5t
    };

    auto get_table = [ & ]( IN uint64_t level ) -> pmlxt & {
        return *page_table[ level - 1 ];
    };
    auto level        = Paging::support_5level_paging ? 5 : 4;
    auto unmap_helper = [ &get_table, &virtual_address, &mode ]( this auto &self, uint64_t level, pmlxt &pmlx_t ) {     // 辅助函数
        auto index = pmlx_t.get_address_index_in( reinterpret_cast< void * >( virtual_address ) );
        if ( !pmlx_t.flags_p( index ) ) {
            virtual_address += get_table( level ).check_page_size( mode );
            return;
        }
        else if ( level == static_cast< uint64_t >( mode ) ) {
            // To unmap a page, we only should set the P bit is in the corresponding entry.
            pmlx_t.set_p( index, 0 );
            if ( mode != PAGE_4K ) {
                // If the page tabel is not pml1t,
                // Under the page table, there may be other page tables.
                auto check_next_table_under_the_old = [ get_table ]( this auto &self, uint64_t level, uint64_t index, pmlxt &pmlx_t ) -> void {
                    // Check whether there are other page tables that under the old page table is referenced by the old entry or not.
                    if ( pmlx_t.get_table( )[ index ]         // The entry isn't NULL,
                         && !pmlx_t.flags_ps_pat( index )     // PS bit isn't set, that means there is a page table that is referenced by this entry. (If the next rule 'level != 1' is true)
                         && level != 1 ) {
                        get_table( level - 1 ) = (uint64_t)Paging::physical_to_virtual( pmlx_t.flags_base( index, PAGE_4K ) );
                        for ( auto i = 0; i < 512; ++i ) {
                            self( level - 1, i, get_table( level ) );
                        }
                        // After we delete the all of page tables that under the old page table,
                        // We also ought to delete this old page table
                        Paging::destoryer( Paging::virtual_to_physical( (uint64_t)get_table( level - 1 ).get_table( ) ) );
                    }
                    else if ( level == 1 ) {
                        // Delete this page table
                        // Then return. Because there is no page table under the pml1t.
                        Paging::destoryer( Paging::virtual_to_physical( (uint64_t)pmlx_t.get_table( ) ) );
                    }
                    return;
                };
                check_next_table_under_the_old( level, index, pmlx_t );
            }
            // After all of these works, flush TLB.
            __asm__ __volatile__( "invlpg (%0)"
                                  :
                                  : "r"( address )
                                  : "memory" );
            virtual_address += get_table( level ).check_page_size( mode );
            return;
        }
        get_table( level - 1 ) = (uint64_t)Paging::physical_to_virtual( pmlx_t.flags_base( index, mode ) );
        self( level - 1, get_table( level - 1 ) );
    };
    while ( size-- ) {
        unmap_helper( level, *this );
    }
}

auto pmlxt::find_physcial_address( IN void *virtual_address, IN MemoryPageType mode ) -> void * {
    pml1t  pml1t { };
    pml2t  pml2t { };
    pml3t  pml3t { };
    pml4t  pml4t { };
    pml5t  pml5t { };
    pmlxt *page_table[] {
        &pml1t,
        &pml2t,
        &pml3t,
        &pml4t,
        &pml5t
    };

    auto get_table = [ & ]( IN uint64_t level ) -> pmlxt & {
        return *page_table[ level - 1 ];
    };

    auto offset { uint64_t( mode ) - 1 };
    auto level       = Paging::support_5level_paging ? 5 : 4;
    auto find_helper = [ &get_table, &offset, &mode ]( this auto &self, IN void *virtual_address, IN uint64_t level, IN pmlxt &pmlx_t ) -> uint64_t * {
        auto index { pmlx_t.get_address_index_in( virtual_address ) };
        if ( !pmlx_t.flags_p( index ) ) {
            return nullptr;
        }
        if ( !( ( level - offset ) - 1 ) ) {
            auto entry = (uint64_t)physical_to_virtual( pmlx_t.flags_base( index, mode ) );
            return (uint64_t *)Paging::virtual_to_physical( entry );
        }
        else {
            auto entry             = (uint64_t)Paging::physical_to_virtual( pmlx_t.flags_base( index, MemoryPageType::PAGE_4K ) );
            get_table( level - 1 ) = entry;
            return self( virtual_address, level - 1, get_table( level - 1 ) );
        }
    };
    return find_helper( virtual_address, level, *this );
}
auto pmlxt::page_protect( IN bool flags ) -> void {
    uint64_t cr0 { };
    __asm__ __volatile__( "movq %%cr0, %0\n\t" : "=r"( cr0 ) );
    if ( !flags ) {
        cr0 |= ( 1ul << 16 );
    }
    else {
        cr0 &= ~( 1ul << 16 );
    }
    __asm__ __volatile__( "movq %0, %%cr0\n\t" ::"r"( cr0 ) );
};

auto pmlxt::activate( void ) -> void {
    if ( this->get_table( ) ) {
        __asm__ __volatile__( "movq %0, %%cr3\n\t" ::"r"( Paging::virtual_to_physical( (uint64_t)this->get_table( ) ) ) );
    }
}
auto pmlxt::copy( IN pmlxt &from ) -> void {
    // copy high 2048 size.
    _paging_memset_( this->pmlx_table, 0, PT_SIZE / 2 );
    _paging_memcpy_( this->pmlx_table + 256, from.get_table( ) + 256, PT_SIZE / 2 );
}
auto pmlxt::make( void ) -> pmlxt & {
    this->pmlx_table = (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) );
    _paging_memset_( this->pmlx_table, 0, this->PT_SIZE );
    return *this;
}
pml1t::pml1t( void ) noexcept :
    pmlxt( (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) ) ) {
}
pml2t::pml2t( void ) noexcept :
    pmlxt( (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) ) ) {
}
pml3t::pml3t( void ) noexcept :
    pmlxt( (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) ) ) {
}
pml4t::pml4t( void ) noexcept :
    pmlxt( (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) ) ) {
}
pml5t::pml5t( void ) noexcept :
    pmlxt( (uint64_t *)Paging::physical_to_virtual( (uint64_t)Paging::allocater( this->PT_SIZE ) ) ) {
}

void initialize_paging( PagingInitializationInformation *info ) {
    Paging { info };
    return;
}