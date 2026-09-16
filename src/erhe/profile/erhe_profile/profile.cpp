#include "erhe_profile/profile.hpp"

#if defined(ERHE_MALLOC_LIBRARY_MIMALLOC)
#   include <mimalloc-new-delete.h>
#elif defined(ERHE_MALLOC_LIBRARY_JEMALLOC)
#   include "erhe_profile/jemalloc_new_delete.hpp"
#endif

// #if defined(ERHE_PROFILE_LIBRARY_TRACY)
// 
// #include <new>
// 
// void* operator new(std::size_t n)
// {
//     auto ptr = malloc(n);
//     TracyAllocS(ptr, n, 24);
//     return ptr;
// }
// 
// void operator delete(void* p)
// {
//     TracyFreeS(p, 24);
//     free(p);
// }
// 
// #endif // defined(ERHE_PROFILE_LIBRARY_TRACY)
