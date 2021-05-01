#ifndef depth_stencil_state_hpp_erhe_graphics
#define depth_stencil_state_hpp_erhe_graphics

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/graphics/configuration.hpp"
#include <functional>

namespace erhe::graphics
{

struct Depth_stencil_state;

struct Stencil_op_state
{
    gl::Stencil_op       stencil_fail_op{gl::Stencil_op::keep};
    gl::Stencil_op       z_fail_op      {gl::Stencil_op::keep};
    gl::Stencil_op       z_pass_op      {gl::Stencil_op::keep};
    gl::Stencil_function function       {gl::Stencil_function::always};
    unsigned int         reference      {0u};
    unsigned int         test_mask      {0xffffU};
    unsigned int         write_mask     {0xffffU};
};

struct Stencil_state_component_hash
{
    auto operator()(const Stencil_op_state& stencil_state_component) const noexcept
    -> size_t
    {
        static_assert(sizeof(size_t) >= 5u);
        return (gl::base_zero(stencil_state_component.stencil_fail_op)) | // 3 bits
               (gl::base_zero(stencil_state_component.z_fail_op) << 3u) | // 3 bits
               (gl::base_zero(stencil_state_component.z_pass_op) << 6u) | // 3 bits
               (gl::base_zero(stencil_state_component.function ) << 9u) | // 3 bits
               ((static_cast<uint64_t>(stencil_state_component.reference ) & 0xffU) << 12u) | // 8 bits
               ((static_cast<uint64_t>(stencil_state_component.test_mask ) & 0xffU) << 20u) | // 8 bits
               ((static_cast<uint64_t>(stencil_state_component.write_mask) & 0xffU) << 28u); // 8 bits
    }
};

auto operator==(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool;

auto operator!=(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool;

namespace Maybe_reversed {
    static constexpr gl::Depth_function less    = Configuration::reverse_depth ? gl::Depth_function::greater : gl::Depth_function::less;
    static constexpr gl::Depth_function lequal  = Configuration::reverse_depth ? gl::Depth_function::gequal  : gl::Depth_function::lequal;
    static constexpr gl::Depth_function greater = Configuration::reverse_depth ? gl::Depth_function::less    : gl::Depth_function::greater;
    static constexpr gl::Depth_function gequal  = Configuration::reverse_depth ? gl::Depth_function::lequal  : gl::Depth_function::gequal;
}

struct Depth_stencil_state
{
    Depth_stencil_state()
        : serial{get_next_serial()}
    {
    }

    Depth_stencil_state(bool               depth_test_enable,
                        bool               depth_write_enable,
                        gl::Depth_function depth_compare_op,
                        bool               stencil_test_enable,
                        Stencil_op_state   front,
                        Stencil_op_state   back
    )
        : serial             {get_next_serial()}
        , depth_test_enable  {depth_test_enable}
        , depth_write_enable {depth_write_enable}
        , depth_compare_op   {depth_compare_op}
        , stencil_test_enable{stencil_test_enable}
        , front              {front}
        , back               {back}
    {
    }

    void touch()
    {
        serial = get_next_serial();
    }

    size_t             serial;
    bool               depth_test_enable  {false};
    bool               depth_write_enable {true}; // OpenGL depth mask
    gl::Depth_function depth_compare_op   {gl::Depth_function::less}; // Not Maybe_reversed::less, this has to match default OpenGL state
    bool               stencil_test_enable{false};
    Stencil_op_state   front;
    Stencil_op_state   back;

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

    static size_t s_serial;

    static Depth_stencil_state depth_test_disabled_stencil_test_disabled;
    static Depth_stencil_state depth_test_enabled_stencil_test_disabled;
    static Depth_stencil_state depth_test_enabled_greater_or_equal_stencil_test_disabled;
    static Depth_stencil_state depth_test_always_stencil_test_disabled;
};

struct Depth_stencil_state_hash
{
    auto operator()(const Depth_stencil_state& state) const noexcept
    -> size_t
    {
        // Since front might match with back, apply std::hash()
        // to one of them to avoid them perfectly canceling out.
        return
            (
                (state.depth_test_enable   ? 1u : 0u) |
                (state.depth_write_enable  ? 2u : 0u) |
                (state.stencil_test_enable ? 3u : 0u) |
                (gl::base_zero(state.depth_compare_op) << 3) // 3 bits
            )
            ^
            (
               Stencil_state_component_hash{}(state.front)
            )
            ^
            (
               Stencil_state_component_hash{}(state.back)
            );
    }
};

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool;

auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool;

class Depth_stencil_state_tracker
{
public:
    void reset();

    void execute(const Depth_stencil_state* state);

private:
    static void execute_component(gl::Stencil_face_direction face,
                                  const Stencil_op_state&    state,
                                  Stencil_op_state&          cache);

    static void execute_shared(const Stencil_op_state& state,
                               Depth_stencil_state&    cache);

private:
    size_t              m_last{0};
    Depth_stencil_state m_cache;
};

} // namespace erhe::graphics

#endif
