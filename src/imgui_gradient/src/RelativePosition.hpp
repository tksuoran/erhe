#pragma once

#include <imgui.h>
#include <cassert>
#include "WrapMode.hpp"

namespace ImGG {

/// Represents a number between 0 and 1.
class RelativePosition {
public:
    /// `position` must be between 0 and 1.
    explicit RelativePosition(float position)
        : _value{position}
    {
        assert_invariants();
    }

    RelativePosition(float position, WrapMode wrap_mode);

    /// Returns a number between 0 and 1.
    auto get() const -> float { return _value; }

    /// `position` must be between 0 and 1.
    void set(float position)
    {
        _value = position;
        assert_invariants();
    }

    auto imgui_widget(const char* label, float width) -> bool;

    friend auto operator<(const RelativePosition& a, const RelativePosition& b) -> bool { return a.get() < b.get(); }
    friend auto operator>(const RelativePosition& a, const RelativePosition& b) -> bool { return a.get() > b.get(); }
    friend auto operator<=(const RelativePosition& a, const RelativePosition& b) -> bool { return a.get() <= b.get(); }
    friend auto operator>=(const RelativePosition& a, const RelativePosition& b) -> bool { return a.get() >= b.get(); }
    friend auto operator==(const RelativePosition& a, const RelativePosition& b) -> bool { return a.get() == b.get(); }
    friend auto operator!=(const RelativePosition& a, const RelativePosition& b) -> bool { return !(a == b); }

private:
    void assert_invariants() const
    {
        assert(0.f <= _value && _value <= 1.f && "RelativePosition value should be between 0.f and 1.f");
    }

private:
    float _value{0.f};
};

} // namespace ImGG