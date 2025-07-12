#pragma once

#include "renderers/composer.hpp"
#include "scene/scene_view.hpp"

#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics {
    class Render_pass;
    class Device;
    class Renderbuffer;
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
class Brush;
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

    // Implements Scene_view
    auto get_scene_root       () const -> std::shared_ptr<Scene_root>                    override;
    auto get_camera           () const -> std::shared_ptr<erhe::scene::Camera>           override;
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
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::shared_ptr<erhe::graphics::Render_pass>  m_render_pass;
    erhe::scene_renderer::Light_projections       m_light_projections;
    erhe::renderer::Pipeline_pass                 m_pipeline_pass;
    Composer                                      m_composer;
    std::shared_ptr<Scene_root>                   m_scene_root;
    std::shared_ptr<erhe::scene::Node>            m_camera_node;
    std::shared_ptr<erhe::scene::Camera>          m_camera;
    std::shared_ptr<Content_library>              m_content_library;
    std::shared_ptr<erhe::graphics::Texture>      m_shadow_texture;
};

class Material_preview : public Scene_preview
{
public:
    Material_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    app_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );

    void render_preview    (const std::shared_ptr<erhe::primitive::Material>& material);
    void show_preview      ();

private:
    void make_preview_scene(Mesh_memory& mesh_memory);
    //// void generate_torus_geometry();

    std::shared_ptr<erhe::primitive::Material> m_last_material;

    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;

    int                                        m_slice_count{40};
    int                                        m_stack_count{22};
    float                                      m_radius{1.0f};
};

class Brush_preview : public Scene_preview
{
public:
    Brush_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::scene::Scene_message_bus& scene_message_bus,
        App_context&                    app_context,
        Mesh_memory&                    mesh_memory,
        Programs&                       programs
    );

    void render_preview(
        const std::shared_ptr<erhe::graphics::Texture>& texture,
        const std::shared_ptr<Brush>&                   brush,
        int64_t                                         time
    );

private:
    void make_preview_scene();

    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<erhe::scene::Node>         m_key_light_node;
    std::shared_ptr<erhe::scene::Light>        m_key_light;
    std::shared_ptr<erhe::scene::Node>         m_fill_light_node;
    std::shared_ptr<erhe::scene::Light>        m_fill_light;
};

}
