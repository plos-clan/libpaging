#include <cstdint>
using std::uint64_t;
using std::uint8_t;
auto _paging_memcpy_( void    *dest,
                      void    *src,
                      uint64_t size )
    -> void * {
    auto ret { dest };
    /*
     * 从较低地址复制到较高地址
     */
    while ( size-- ) {
        *reinterpret_cast< char * >( dest ) = *reinterpret_cast< char * >( const_cast< void * >( src ) );

        dest = reinterpret_cast< char * >( dest ) + 1;

        src = reinterpret_cast< char * >( const_cast< void * >( src ) ) + 1;
    }
    return reinterpret_cast< void * >( ret );
}

/**
 * @brief 设置内存内容
 * @param dest 所指向的内存单元
 * @param value 要替换的数
 * @param size dest前几个的字节
 */
auto _paging_memset_( void    *dest,
                      uint8_t  value,
                      uint64_t size )
    -> void * {
    unsigned char *ptr   = (unsigned char *)dest;
    unsigned char  val   = (unsigned char)value;
    auto           count = size;
    using size_t         = uint64_t;
    // 处理未对齐的字节
    while ( count > 0 && ( (size_t)ptr & ( sizeof( size_t ) - 1 ) ) ) {
        *ptr++ = val;
        count--;
    }

    // 如果还有足够的字节要处理
    if ( count >= sizeof( size_t ) ) {
        size_t  val_word;
        size_t *word_ptr;

        // 构造填充字
        val_word = ( val << 24 ) | ( val << 16 ) | ( val << 8 ) | val;
#if SIZE_MAX > 0xffffffff
        val_word = ( val_word << 32 ) | val_word;
#endif

        // 按字填充
        word_ptr = (size_t *)ptr;
        while ( count >= sizeof( size_t ) ) {
            *word_ptr++ = val_word;
            count -= sizeof( size_t );
        }

        ptr = (unsigned char *)word_ptr;
    }

    // 处理剩余字节
    while ( count-- ) {
        *ptr++ = val;
    }

    return (void *)dest;
}