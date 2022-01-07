#pragma once

#include "erhe/gl/enum_base_zero_functions.hpp"

namespace erhe::graphics
{

class Input_assembly_state
{
public:
    Input_assembly_state();
    Input_assembly_state(
        gl::Primitive_type primitive_topology,
        bool               primitive_restart
    );

    void touch();

    size_t             serial;
    gl::Primitive_type primitive_topology{gl::Primitive_type::points};
    bool               primitive_restart {false};

    [[nodiscard]] static auto get_next_serial() -> size_t;

    static size_t               s_serial;
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
    void execute(const Input_assembly_state* state);

private:
    const Input_assembly_state* m_last{nullptr};
};

} // namespace erhe::graphics
