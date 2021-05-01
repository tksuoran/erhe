#ifndef color_blend_state_hpp_erhe_graphics
#define color_blend_state_hpp_erhe_graphics

#include "erhe/gl/enum_base_zero_functions.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

namespace erhe::graphics
{

struct Blend_state_component
{
    gl::Blend_equation_mode equation_mode     {gl::Blend_equation_mode::func_add};
    gl::Blending_factor     source_factor     {gl::Blending_factor::one};
    gl::Blending_factor     destination_factor{gl::Blending_factor::zero};
};

struct Blend_state_component_hash
{
    auto operator()(const Blend_state_component& blend_state_component) const noexcept
    -> size_t
    {
        return (gl::base_zero(blend_state_component.equation_mode     ) << 0u) | // 3 bits
               (gl::base_zero(blend_state_component.source_factor     ) << 3u) | // 5 bits
               (gl::base_zero(blend_state_component.destination_factor) << 8u);  // 5 bits
    }
};

auto operator==(const Blend_state_component& lhs,
                const Blend_state_component& rhs) noexcept
-> bool;

auto operator!=(const Blend_state_component& lhs,
                const Blend_state_component& rhs) noexcept
-> bool;

struct Color_blend_state
{
    Color_blend_state()
        : serial{get_next_serial()}
    {
    }

    Color_blend_state(bool                  enabled,
                      Blend_state_component rgb,
                      Blend_state_component alpha,
                      glm::vec4             color                  = {},
                      bool                  color_write_mask_red   = true,
                      bool                  color_write_mask_green = true,
                      bool                  color_write_mask_blue  = true,
                      bool                  color_write_mask_alpha = true
    )
        : serial                {get_next_serial()}
        , enabled               {enabled}
        , rgb                   {rgb}
        , alpha                 {alpha}
        , color                 {color}
        , color_write_mask_red  {color_write_mask_red}
        , color_write_mask_green{color_write_mask_green}
        , color_write_mask_blue {color_write_mask_blue}
        , color_write_mask_alpha{color_write_mask_alpha}
    {
    }

    void touch()
    {
        serial = get_next_serial();
    }

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

    size_t                serial;
    bool                  enabled{false};
    Blend_state_component rgb;
    Blend_state_component alpha;
    glm::vec4             color{0.0f, 0.0f, 0.0f, 0.0f};
    bool                  color_write_mask_red  {true};
    bool                  color_write_mask_green{true};
    bool                  color_write_mask_blue {true};
    bool                  color_write_mask_alpha{true};

    static size_t            s_serial;
    static Color_blend_state color_blend_disabled;
    static Color_blend_state color_blend_premultiplied;
    static Color_blend_state color_writes_disabled;
};

struct Blend_state_hash
{
    auto operator()(const Color_blend_state& state) const noexcept
    -> size_t
    {
        return
            (
                (state.enabled ? 1u : 0u)                          | // 1 bit
                (Blend_state_component_hash{}(state.rgb  ) << 1u)  | // 13 bits
                (Blend_state_component_hash{}(state.alpha) << 14u)
            ) ^
            (
                std::hash<glm::vec4>{}(state.color)
            ) ^
            (
               (state.color_write_mask_red   ? 1u : 0u) |
               (state.color_write_mask_green ? 2u : 0u) |
               (state.color_write_mask_blue  ? 4u : 0u) |
               (state.color_write_mask_alpha ? 8u : 0u)
            );
    }
};

auto operator==(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept
-> bool;

auto operator!=(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept
-> bool;

class Color_blend_state_tracker
{
public:
    void reset();

    void execute(const Color_blend_state* state) noexcept;

private:
    size_t            m_last{0};
    Color_blend_state m_cache;
};

} // namespace erhe::graphics

#endif
