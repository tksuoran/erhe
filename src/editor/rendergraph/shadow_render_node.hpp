#pragma once

#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <memory>

namespace erhe::graphics       { class Device; }
namespace erhe::scene_renderer { class Light_projections; }

namespace editor {

class App_context;
class Scene_view;
class Viewport_scene_view;

// Helper rendergraph node calling Shadow_renderer

// TODO Think about Shadow_render_node / Shadow_renderer relationship,
//      should Shadow_render_node have more state?
class Shadow_render_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Shadow_render_node(
        erhe::graphics::Device&         graphics_device,
        erhe::rendergraph::Rendergraph& rendergraph,
        App_context&                    context,
        Scene_view&                     scene_view,
        int                             resolution,
        int                             light_count,
        int                             depth_bits
    );
    ~Shadow_render_node() noexcept override;

    // Implements Rendergraph_node
    auto get_type_name() const -> std::string_view override { return "Shadow_render_node"; }
    void execute_rendergraph_node() override;

    auto get_producer_output_texture (int key, int depth = 0) const -> std::shared_ptr<erhe::graphics::Texture> override;
    auto inputs_allowed() const -> bool override;

    // Public API
    void reconfigure(erhe::graphics::Device& graphics_device, int resolution, int light_count, int depth_bits);

    [[nodiscard]] auto get_scene_view       () -> Scene_view&;
    [[nodiscard]] auto get_scene_view       () const -> const Scene_view&;
    [[nodiscard]] auto get_light_projections() -> erhe::scene_renderer::Light_projections&;
    [[nodiscard]] auto get_texture          () const -> std::shared_ptr<erhe::graphics::Texture>;
    [[nodiscard]] auto get_viewport         () const -> erhe::math::Viewport;

private:
    App_context&                                              m_context;
    Scene_view&                                               m_scene_view;
    std::shared_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Render_pass>> m_render_passes;
    erhe::math::Viewport                                      m_viewport{0, 0, 0, 0};
    erhe::scene_renderer::Light_projections                   m_light_projections;
};

}
