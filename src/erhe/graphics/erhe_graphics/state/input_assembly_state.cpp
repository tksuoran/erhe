#include "erhe_graphics/state/input_assembly_state.hpp"

namespace erhe::graphics {

void Input_assembly_state_tracker::reset()
{
}

void Input_assembly_state_tracker::execute(const Input_assembly_state& state)
{
    static_cast<void>(state);
}

Input_assembly_state Input_assembly_state::point
{
    Primitive_type::point,
    true
};

Input_assembly_state Input_assembly_state::line
{
    Primitive_type::line,
    true
};

Input_assembly_state Input_assembly_state::triangle
{
    Primitive_type::triangle,
    true
};

Input_assembly_state Input_assembly_state::triangle_strip
{
    Primitive_type::triangle_strip,
    true
};


auto operator==(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept -> bool
{
    return
        (lhs.primitive_topology == rhs.primitive_topology) &&
        (lhs.primitive_restart  == rhs.primitive_restart );
}

auto operator!=(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
