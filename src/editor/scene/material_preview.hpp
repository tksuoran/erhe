#pragma once

#include "renderers/composer.hpp"
#include "scene/scene_view.hpp"

#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <gsl/gsl>

namespace erhe::graphics {
    class Framebuffer;
    class Instance;
    class Renderbuffer;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::renderer {
    class Light_projections;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Scene_message_bus;
}

namespace editor
{

class Editor_context;
class Editor_rendering;
class Content_library;
class Mesh_memory;
class Scene_root;
class Tools;

class Material_preview
    : public Scene_view
{
public:
    Material_preview(
        erhe::graphics::Instance&       graphics_instance,
        erhe::scene::Scene_message_bus& scene_message_bus,
        Editor_context&                 editor_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );

    // Implements Imgui_window
    //void imgui() override;

    // Implements Scene_view
    auto get_scene_root       () const -> std::shared_ptr<Scene_root>                    override;
    auto get_camera           () const -> std::shared_ptr<erhe::scene::Camera>           override;
    auto get_rendergraph_node ()       -> erhe::rendergraph::Rendergraph_node*           override;
    auto get_light_projections() const -> const erhe::scene_renderer::Light_projections* override;
    auto get_shadow_texture   () const -> erhe::graphics::Texture*                       override;

    // Public API
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;

    void set_area_size(int size);
    void update_rendertarget(erhe::graphics::Instance& graphics_instance);
    void render_preview(
        const std::shared_ptr<erhe::primitive::Material>& material
    );
    void show_preview();

private:
    void make_preview_scene (Mesh_memory& mesh_memory);
    //// void generate_torus_geometry();

    int                                           m_width{0};
    int                                           m_height{0};
    gl::Internal_format                           m_color_format;
    gl::Internal_format                           m_depth_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
    erhe::scene_renderer::Light_projections       m_light_projections;
    erhe::renderer::Pipeline_renderpass           m_pipeline_renderpass;
    Composer                                      m_composer;

    std::shared_ptr<Scene_root>          m_scene_root;
    std::shared_ptr<Content_library>     m_content_library;
    std::shared_ptr<erhe::scene::Node>   m_node;
    std::shared_ptr<erhe::scene::Mesh>   m_mesh;
    std::shared_ptr<erhe::scene::Node>   m_key_light_node;
    std::shared_ptr<erhe::scene::Light>  m_key_light;
    std::shared_ptr<erhe::scene::Node>   m_camera_node;
    std::shared_ptr<erhe::scene::Camera> m_camera;

    std::shared_ptr<erhe::graphics::Texture>   m_shadow_texture;
    std::shared_ptr<erhe::primitive::Material> m_last_material;

    glm::vec4 m_clear_color{0.0f, 0.0f, 0.0f, 0.333f};

    int       m_slice_count{40};
    int       m_stack_count{22};
    float     m_radius{1.0f};
};

} // namespace editor
