#pragma once
#ifdef __cplusplus
#include <stdint.h>
extern "C" {
#else
#endif

enum MemoryPageType {
    NONE,
    PAGE_4K,
    PAGE_2M,
    PAGE_1G
};
typedef void *( *Allocater )( uint64_t );
typedef void ( *Destoryer )( void * );
typedef void *( *Converter )( uint64_t );

struct PagingInitializationInformation {
    Allocater      allocater;
    Destoryer      destoryer;
    Converter      virtual_to_physical;
    Converter      physical_to_virtual;
    uint64_t       start_page;
    uint64_t       end_page;
    MemoryPageType map_mode;
};

void initialize_paging( PagingInitializationInformation *info );

#ifdef __cplusplus
}
#endif
