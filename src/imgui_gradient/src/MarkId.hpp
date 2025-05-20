#pragma once

#include <algorithm>
#include <list>
#include "Mark.hpp"

namespace ImGG {

namespace internal {
// Thanks to https://stackoverflow.com/a/34143224
template<class T, class U>
struct transfer_const_ptr {
    using type = typename std::remove_const<U>::type*;
};
template<class T, class U>
struct transfer_const_ptr<const T&, U> {
    using type = const U*;
};

template<class T, class U>
struct transfer_const_iterator {
    using type = typename std::remove_const<U>::type::iterator;
};

template<class T, class U>
struct transfer_const_iterator<const T&, U> {
    using type = typename U::const_iterator;
};

} // namespace internal

/// Used to identify a Mark.
class MarkId {
public:
    MarkId() = default;
    explicit MarkId(const Mark* ptr)
        : _ptr{ptr}
    {}
    explicit MarkId(const Mark& ref)
        : _ptr{&ref}
    {}
    explicit MarkId(const std::list<Mark>::const_iterator iterator)
        : _ptr{&*iterator}
    {}

    void reset() { _ptr = nullptr; }

    friend auto operator==(const MarkId& a, const MarkId& b) -> bool { return a._ptr == b._ptr; };
    friend auto operator!=(const MarkId& a, const MarkId& b) -> bool { return !(a == b); };

    auto as_imgui_id() const -> void const* { return _ptr; }

private:
    /// If it is not in the list returns an invalid iterator
    template<typename GradientT>
    auto find_iterator(GradientT&& gradient) const -> typename internal::transfer_const_iterator<GradientT, std::list<Mark>>::type // Returns a `const std::list<Mark>::iterator>` if GradientT is const and a mutable `std::list<Mark>::iterator>` otherwise.
    {
        return std::find_if(gradient._marks.begin(), gradient._marks.end(), [&](const Mark& mark) {
            return &mark == _ptr;
        });
    }

    template<typename GradientT>
    auto find(GradientT&& gradient) const -> typename internal::transfer_const_ptr<GradientT, Mark>::type // Returns a `const Mark*` if GradientT is const and a mutable `Mark*` otherwise.
    {
        const auto it = find_iterator(gradient);
        return it != gradient._marks.end()
                   ? &*it
                   : nullptr;
    }

    friend class Gradient;

private:
    const Mark* _ptr{};
};

} // namespace ImGG