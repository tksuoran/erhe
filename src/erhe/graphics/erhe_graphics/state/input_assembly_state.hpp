#pragma once

#include "erhe_graphics/enums.hpp"

namespace erhe::graphics {

class Input_assembly_state
{
public:
    Primitive_type primitive_topology{Primitive_type::point};
    bool           primitive_restart {false};

    static Input_assembly_state point;
    static Input_assembly_state line;
    static Input_assembly_state triangle;
    static Input_assembly_state triangle_strip;
};

[[nodiscard]] auto operator==(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept -> bool;

class Input_assembly_state_tracker
{
public:
    void reset  ();
    void execute(const Input_assembly_state& state);
};

} // namespace erhe::graphics
