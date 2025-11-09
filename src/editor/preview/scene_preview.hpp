#pragma once

#include "renderers/composer.hpp"
#include "scene/scene_view.hpp"

#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics {
    class Render_pass;
    class Device;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::primitive { class Material; }
namespace erhe::renderer  { class Light_projections; }
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Scene_message_bus;
}

namespace editor {

class App_context;
class App_rendering;
class Content_library;
class Mesh_memory;
class Programs;
class Scene_root;
class Tools;

class Scene_preview : public Scene_view
{
public:
    Scene_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    app_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );
    ~Scene_preview();

    // Implements Scene_view
    auto get_camera           () const -> std::shared_ptr<erhe::scene::Camera>           override;
    auto get_perspective_scale() const -> float                                          override;
    auto get_rendergraph_node ()       -> erhe::rendergraph::Rendergraph_node*           override;
    auto get_light_projections() const -> const erhe::scene_renderer::Light_projections* override;
    auto get_shadow_texture   () const -> erhe::graphics::Texture*                       override;

    // Public API
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;

    void resize             (int width, int height);
    void set_color_texture  (const std::shared_ptr<erhe::graphics::Texture>& color_texture);
    void set_clear_color    (glm::vec4 clear_color);
    void update_rendertarget(erhe::graphics::Device& graphics_device);

protected:
    erhe::graphics::Device&                       m_graphics_device;
    int                                           m_width{0};
    int                                           m_height{0};
    glm::vec4                                     m_clear_color{0.5f, 0.5f, 0.5f, 0.0f};
    bool                                          m_use_external_color_texture{false};
    erhe::dataformat::Format                      m_color_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    erhe::dataformat::Format                      m_depth_format;
    std::unique_ptr<erhe::graphics::Texture>      m_depth_texture;
    std::shared_ptr<erhe::graphics::Render_pass>  m_render_pass;
    erhe::scene_renderer::Light_projections       m_light_projections;
    erhe::renderer::Pipeline_pass                 m_pipeline_pass;
    Composer                                      m_composer;
    std::shared_ptr<Scene_root>                   m_scene_root_shared;
    std::shared_ptr<erhe::scene::Node>            m_camera_node;
    std::shared_ptr<erhe::scene::Camera>          m_camera;
    std::shared_ptr<Content_library>              m_content_library;
    std::shared_ptr<erhe::graphics::Texture>      m_shadow_texture;
};

}
