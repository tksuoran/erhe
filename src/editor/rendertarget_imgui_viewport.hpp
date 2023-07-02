#pragma once

#include "erhe/imgui/imgui_viewport.hpp"

#include <string_view>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_window;
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
}

namespace editor
{

class Editor_context;
class Headset_view;
class Rendertarget_mesh;

class Rendertarget_imgui_viewport
    : public erhe::imgui::Imgui_viewport
{
public:
    Rendertarget_imgui_viewport(
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Rendertarget_mesh*              rendertarget_mesh,
        const std::string_view          name,
        bool                            imgui_ini = true
    );
    virtual ~Rendertarget_imgui_viewport() noexcept;

    [[nodiscard]] auto rendertarget_mesh() -> Rendertarget_mesh*;
    void set_clear_color(const glm::vec4& value);

    // Implements Imgui_viewport
    [[nodiscard]] auto get_scale_value  () const -> float override;
    [[nodiscard]] auto begin_imgui_frame() -> bool override;
    void end_imgui_frame() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node() override;

    [[nodiscard]] auto get_consumer_input_texture(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_consumer_input_framebuffer(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Framebuffer> override;

    [[nodiscard]] auto get_consumer_input_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::toolkit::Viewport override;

    [[nodiscard]] auto get_producer_output_texture(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> std::shared_ptr<erhe::graphics::Texture> override;

    [[nodiscard]] auto get_producer_output_viewport(
        erhe::rendergraph::Resource_routing resource_routing,
        int                                 key,
        int                                 depth = 0
    ) const -> erhe::toolkit::Viewport override;

    // Public API
    void set_menu_visible(bool visible);

private:
    Editor_context& m_context;

    Rendertarget_mesh* m_rendertarget_mesh{nullptr};
    glm::vec4          m_clear_color {0.0f, 0.0f, 0.0f, 0.2f};
    float              m_last_mouse_x{0.0f};
    float              m_last_mouse_y{0.0f};
};

} // namespace editor
