#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::graphics {
    class Instance;
}
namespace erhe::scene_renderer {
    class Light_projections;
}

namespace editor {

class Editor_context;
class Scene_view;
class Viewport_window;

// Helper rendergraph node calling Shadow_renderer

// TODO Think about Shadow_render_node / Shadow_renderer relationship,
//      should Shadow_render_node have more state?
class Shadow_render_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Shadow_render_node(
        erhe::graphics::Instance&       graphics_instance,
        erhe::rendergraph::Rendergraph& rendergraph,
        Editor_context&                 editor_context,
        Scene_view&                     scene_view,
        int                             resolution,
        int                             light_count
    );

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Shadow_render_node"; }
    void execute_rendergraph_node() override;

    auto get_producer_output_texture (erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto get_producer_output_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;
    auto inputs_allowed() const -> bool override;

    // Public API
    void reconfigure(erhe::graphics::Instance& graphics_instance, int resolution, int light_count);

    [[nodiscard]] auto get_scene_view       () -> Scene_view&;
    [[nodiscard]] auto get_scene_view       () const -> const Scene_view&;
    [[nodiscard]] auto get_light_projections() -> erhe::scene_renderer::Light_projections&;
    [[nodiscard]] auto get_texture          () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto get_viewport         () const -> erhe::math::Viewport;

private:
    Editor_context&                                           m_context;
    Scene_view&                                               m_scene_view;
    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
    erhe::math::Viewport                                      m_viewport{0, 0, 0, 0, true};
    erhe::scene_renderer::Light_projections                   m_light_projections;
};

} // namespace editor
