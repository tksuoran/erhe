#ifndef style_hpp_erhe_ui
#define style_hpp_erhe_ui

#include "erhe/graphics/shader_stages.hpp"
#include "erhe/ui/font.hpp"
#include "erhe/ui/ninepatch_style.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace erhe::graphics
{

class Shader_stages;

} // namespace erhe::graphics

namespace erhe::ui
{

class Style
{
public:
    Style(glm::vec2 padding,
          glm::vec2 inner_padding) noexcept
        : padding      {padding}
        , inner_padding{inner_padding}
    {
    }

    Style(glm::vec2                      padding,
          glm::vec2                      inner_padding,
          Font*                          font,
          Ninepatch_style*               ninepatch_style,
          erhe::graphics::Shader_stages* program) noexcept
        : padding        {padding}
        , inner_padding  {inner_padding}
        , font           {font}
        , ninepatch_style{ninepatch_style}
        , program        {program}
    {
    }

    explicit Style(Font* font) noexcept
        : font{font}
    {
    }

    Style(const Style&) = delete;

    auto operator=(const Style&)
    -> Style& = delete;

    glm::vec2                      padding        {0.0f};
    glm::vec2                      inner_padding  {0.0f};
    Font*                          font           {nullptr};
    Ninepatch_style*               ninepatch_style{nullptr};
    erhe::graphics::Shader_stages* program        {nullptr};
    unsigned int                   texture_unit   {0};
};

} // namespace erhe::ui

#endif
