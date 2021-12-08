#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/gl/gl.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics
{

Input_assembly_state::Input_assembly_state()
    : serial{get_next_serial()}
{
}

Input_assembly_state::Input_assembly_state(
    gl::Primitive_type primitive_topology,
    bool               primitive_restart
)
    : serial            {get_next_serial()}
    , primitive_topology{primitive_topology}
    , primitive_restart {primitive_restart}
{
}

void Input_assembly_state::touch()
{
    serial = get_next_serial();
}

size_t Input_assembly_state::s_serial{0};

auto Input_assembly_state::get_next_serial() -> size_t
{
    do
    {
        s_serial++;
    }

    while (s_serial == 0);

    return s_serial;
}

void Input_assembly_state_tracker::reset()
{
    m_last = nullptr;
}

void Input_assembly_state_tracker::execute(const Input_assembly_state* state)
{
    m_last = state;
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


auto operator==(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept
-> bool
{
    return
        (lhs.primitive_topology == rhs.primitive_topology) &&
        (lhs.primitive_restart  == rhs.primitive_restart );
}

auto operator!=(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
