#pragma once

// Global C++ operator new / delete routed to jemalloc (ERHE_MALLOC_LIBRARY=jemalloc).
//
// jemalloc is built with the je_ symbol prefix (cmake/jemalloc.cmake), so C
// malloc / free are untouched and only C++ allocations go through jemalloc.
// The counterpart of <mimalloc-new-delete.h>: these are definitions, so this
// header must be included by exactly one source file (erhe_profile/profile.cpp).

// The generated jemalloc.h starts with `#pragma GCC system_header` (C4068 on
// MSVC; erhe builds with /external:W3 /WX, so SYSTEM include paths do not hide it).
#if defined(_MSC_VER) && !defined(__clang__)
#   pragma warning(push)
#   pragma warning(disable : 4068)
#endif
#include <jemalloc/jemalloc.h>
#if defined(_MSC_VER) && !defined(__clang__)
#   pragma warning(pop)
#endif

#include <cstddef>
#include <new>

namespace erhe::profile::detail {

[[nodiscard]] inline auto jemalloc_new(std::size_t size) -> void*
{
    const std::size_t request_size = (size == 0) ? 1 : size;
    for (;;) {
        void* const pointer = je_malloc(request_size);
        if (pointer != nullptr) {
            return pointer;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc{};
        }
        handler();
    }
}

[[nodiscard]] inline auto jemalloc_new_aligned(std::size_t size, std::align_val_t alignment) -> void*
{
    const std::size_t request_size = (size == 0) ? 1 : size;
    for (;;) {
        void* const pointer = je_aligned_alloc(static_cast<std::size_t>(alignment), request_size);
        if (pointer != nullptr) {
            return pointer;
        }
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc{};
        }
        handler();
    }
}

[[nodiscard]] inline auto jemalloc_new_nothrow(std::size_t size) noexcept -> void*
{
    try {
        return jemalloc_new(size);
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] inline auto jemalloc_new_aligned_nothrow(std::size_t size, std::align_val_t alignment) noexcept -> void*
{
    try {
        return jemalloc_new_aligned(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

inline void jemalloc_delete(void* pointer) noexcept
{
    je_free(pointer);
}

inline void jemalloc_delete_sized(void* pointer, std::size_t size) noexcept
{
    // Zero-size requests were allocated as one byte; jemalloc's sized free
    // needs the size the allocation was made with.
    if ((pointer == nullptr) || (size == 0)) {
        je_free(pointer);
        return;
    }
    je_sdallocx(pointer, size, 0);
}

inline void jemalloc_delete_aligned_sized(void* pointer, std::size_t size, std::align_val_t alignment) noexcept
{
    if ((pointer == nullptr) || (size == 0)) {
        je_free(pointer);
        return;
    }
    je_sdallocx(pointer, size, MALLOCX_ALIGN(static_cast<std::size_t>(alignment)));
}

} // namespace erhe::profile::detail

void* operator new  (std::size_t size) { return erhe::profile::detail::jemalloc_new(size); }
void* operator new[](std::size_t size) { return erhe::profile::detail::jemalloc_new(size); }
void* operator new  (std::size_t size, const std::nothrow_t&) noexcept { return erhe::profile::detail::jemalloc_new_nothrow(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return erhe::profile::detail::jemalloc_new_nothrow(size); }
void* operator new  (std::size_t size, std::align_val_t alignment) { return erhe::profile::detail::jemalloc_new_aligned(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return erhe::profile::detail::jemalloc_new_aligned(size, alignment); }
void* operator new  (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return erhe::profile::detail::jemalloc_new_aligned_nothrow(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return erhe::profile::detail::jemalloc_new_aligned_nothrow(size, alignment); }

void operator delete  (void* pointer) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete[](void* pointer) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete  (void* pointer, const std::nothrow_t&) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete  (void* pointer, std::size_t size) noexcept { erhe::profile::detail::jemalloc_delete_sized(pointer, size); }
void operator delete[](void* pointer, std::size_t size) noexcept { erhe::profile::detail::jemalloc_delete_sized(pointer, size); }
void operator delete  (void* pointer, std::align_val_t) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete  (void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { erhe::profile::detail::jemalloc_delete(pointer); }
void operator delete  (void* pointer, std::size_t size, std::align_val_t alignment) noexcept { erhe::profile::detail::jemalloc_delete_aligned_sized(pointer, size, alignment); }
void operator delete[](void* pointer, std::size_t size, std::align_val_t alignment) noexcept { erhe::profile::detail::jemalloc_delete_aligned_sized(pointer, size, alignment); }
