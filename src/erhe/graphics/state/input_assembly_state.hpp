#pragma once

#include "erhe/gl/enum_base_zero_functions.hpp"

namespace erhe::graphics
{

struct Input_assembly_state
{
    Input_assembly_state()
        : serial{get_next_serial()}
    {
    }

    Input_assembly_state(gl::Primitive_type primitive_topology,
                         bool               primitive_restart)
        : serial            {get_next_serial()}
        , primitive_topology{primitive_topology}
        , primitive_restart {primitive_restart}
    {
    }

    void touch()
    {
        serial = get_next_serial();
    }

    size_t             serial;
    gl::Primitive_type primitive_topology{gl::Primitive_type::points};
    bool               primitive_restart {false};

    static auto get_next_serial()
    -> size_t
    {
        do
        {
            s_serial++;
        }

        while (s_serial == 0);

        return s_serial;
    }

    static size_t               s_serial;
    static Input_assembly_state points;
    static Input_assembly_state lines;
    static Input_assembly_state line_loop;
    static Input_assembly_state triangles;
    static Input_assembly_state triangle_fan;
    static Input_assembly_state triangle_strip;
};

auto operator==(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept
-> bool;

auto operator!=(const Input_assembly_state& lhs, const Input_assembly_state& rhs) noexcept
-> bool;

struct Input_assembly_state_tracker
{
    void reset()
    {
        m_last = nullptr;
    }

    void execute(const Input_assembly_state* state)
    {
        m_last = state;
    }

private:
    const Input_assembly_state* m_last{nullptr};
};

} // namespace erhe::graphics
