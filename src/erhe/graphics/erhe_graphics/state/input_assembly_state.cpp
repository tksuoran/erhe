#include "erhe_graphics/state/input_assembly_state.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics
{

void Input_assembly_state_tracker::reset()
{
}

void Input_assembly_state_tracker::execute(const Input_assembly_state& state)
{
    static_cast<void>(state);
}

Input_assembly_state Input_assembly_state::points
{
    gl::Primitive_type::points,
    true
};

Input_assembly_state Input_assembly_state::lines
{
    gl::Primitive_type::lines,
    true
};

Input_assembly_state Input_assembly_state::line_loop
{
    gl::Primitive_type::line_loop,
    true
};

Input_assembly_state Input_assembly_state::triangles
{
    gl::Primitive_type::triangles,
    true
};

Input_assembly_state Input_assembly_state::triangle_fan
{
    gl::Primitive_type::triangle_fan,
    true
};

Input_assembly_state Input_assembly_state::triangle_strip
{
    gl::Primitive_type::triangle_strip,
    true
};


auto operator==(
    const Input_assembly_state& lhs,
    const Input_assembly_state& rhs
) noexcept -> bool
{
    return
        (lhs.primitive_topology == rhs.primitive_topology) &&
        (lhs.primitive_restart  == rhs.primitive_restart );
}

auto operator!=(
    const Input_assembly_state& lhs,
    const Input_assembly_state& rhs
) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
