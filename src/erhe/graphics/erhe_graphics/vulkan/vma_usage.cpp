#ifdef _MSC_VER
// VS 2026
#pragma warning(disable : 4100) // unreferenced parameter
#pragma warning(disable : 4189) // local variable is initialized but not referenced
#pragma warning(disable : 4324) // structure was padded due to alignment specifier
// VS 2022
#pragma warning(disable : 4127) // conditional expression is constant
#endif

#define VMA_IMPLEMENTATION
#include "volk.h"
#include "vk_mem_alloc.h"