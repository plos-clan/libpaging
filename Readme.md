# Library Paging
This is a library about paging for OS DIYER

# How to use the library

## 1. Use C interface.

### Initialization
In your codes, you should create a PagingInitializationInformation, and set initialization information.

You should provide heap memory allocater, collector.
What's more, you should provide two functions that convert physical address to virtual address and virtual address to physical address.
like this:
```c
struct PagingInitializationInformation info;

info.allocater = /* Your heap memory allocater */;
info.collector = /* Your heap memory collector */;
info.virtual_to_physical = /* Your function that convert virtual address to physical address */;
info.physical_to_virtual = /* Your function that convert physical address to virtual address */;
```
Then, you should set your page memory start and page memory end in kernel space.
Such as:
```c
info.start_page = 0x100000;
info.end_page =  0x1000000;
```
You ought to set your mapping mode.
There are three mode can be choosed:
- 2mb mapping(PAGE_2M)
- 4kb mapping(PAGE_4K)
- 1gb mapping(PAGE_1G)

```c
info.map_mode = PAGE_4K;
```
Finally, call initialization function

```c
initialize_paging(&info);
```

## 2. Use C++ interface.

### Initialization
Initialization process of C++ is similar to that of C.
But the difference is C++ use class instead of function.
like this:
```c++
/* Initialization process */
Paging paging{&info};
```

### map
the map
