#pragma once

#include "erhe_gl/wrapper_enums.hpp"

namespace erhe::graphics
{

class Input_assembly_state
{
public:
    gl::Primitive_type primitive_topology{gl::Primitive_type::points};
    bool               primitive_restart {false};

    static Input_assembly_state points;
    static Input_assembly_state lines;
    static Input_assembly_state line_loop;
    static Input_assembly_state triangles;
    static Input_assembly_state triangle_fan;
    static Input_assembly_state triangle_strip;
};

[[nodiscard]] auto operator==(
    const Input_assembly_state& lhs,
    const Input_assembly_state& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Input_assembly_state& lhs,
    const Input_assembly_state& rhs
) noexcept -> bool;

class Input_assembly_state_tracker
{
public:
    void reset  ();
    void execute(const Input_assembly_state& state);
};

} // namespace erhe::graphics
