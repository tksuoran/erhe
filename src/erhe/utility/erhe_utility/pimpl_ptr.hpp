#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace erhe::utility {

template<class T, std::size_t Size, std::size_t Align = alignof(std::max_align_t)>
class pimpl_ptr
{
private:
    alignas(Align) std::byte storage[Size];

    T* ptr() noexcept {
        return std::launder(reinterpret_cast<T*>(storage));
    }

    const T* ptr() const noexcept {
        return std::launder(reinterpret_cast<const T*>(storage));
    }

public:
    template<class... Args>
    explicit pimpl_ptr(Args&&... args)
    {
        std::construct_at(ptr(), std::forward<Args>(args)...);
    }

    ~pimpl_ptr()
    {
        std::destroy_at(ptr());
    }

    pimpl_ptr(const pimpl_ptr& other)
    {
        std::construct_at(ptr(), *other);
    }

    pimpl_ptr(pimpl_ptr&& other)
    {
        std::construct_at(ptr(), std::move(*other));
    }

    pimpl_ptr& operator=(const pimpl_ptr& other)
    {
        **this = *other;
        return *this;
    }

    pimpl_ptr& operator=(pimpl_ptr&& other)
    {
        **this = std::move(*other);
        return *this;
    }

    T& operator*() noexcept { return *ptr(); }
    const T& operator*() const noexcept { return *ptr(); }

    T* operator->() noexcept { return ptr(); }
    const T* operator->() const noexcept { return ptr(); }

    friend void swap(pimpl_ptr& lhs, pimpl_ptr& rhs) noexcept
    {
        using std::swap;
        swap(*lhs, *rhs);
    }
};
} // namespace erhe::utility
