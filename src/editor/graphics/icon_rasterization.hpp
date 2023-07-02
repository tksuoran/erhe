#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace erhe::graphics {
    class Instance;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_renderer;
}
namespace lunasvg
{
    class Document;
}

namespace editor {

class Programs;

class Icon_rasterization
{
public:
    Icon_rasterization(
        erhe::graphics::Instance&    graphics_instance,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        Programs&                    programs,
        int                          size,
        int                          column_count,
        int                          row_count
    );

    void rasterize(
        lunasvg::Document& document,
        int                column,
        int                row
    );

    [[nodiscard]] auto get_size() const -> int;

    void icon(
        glm::vec2 uv0,
        glm::vec4 tint_color = glm::vec4{1.0f}
    ) const;

    auto icon_button(
        uint32_t  id,
        glm::vec2 uv0,
        glm::vec4 backround_color = glm::vec4{0.0f},
        glm::vec4 tint_color      = glm::vec4{1.0f},
        bool      linear          = false
    ) const -> bool;

private:
    [[nodiscard]] auto uv1(const glm::vec2& uv0) const -> glm::vec2;

    erhe::imgui::Imgui_renderer& m_imgui_renderer;

    std::shared_ptr<erhe::graphics::Texture> m_texture;
    uint64_t                                 m_texture_handle{0};
    int                                      m_icon_width    {0};
    int                                      m_icon_height   {0};
    float                                    m_icon_uv_width {0.0f};
    float                                    m_icon_uv_height{0.0f};
};

} // namespace editor
