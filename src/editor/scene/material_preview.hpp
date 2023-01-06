#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/scene/viewport.hpp"

#include <gsl/gsl>

namespace erhe::application {
    class Commands;
    class Configuration;
    class Imgui_windows;
}

namespace erhe::geometry {
    class Geometry;
}

namespace erhe::graphics {
    class Framebuffer;
    class Renderbuffer;
    class Texture;
}

namespace erhe::primitive {
    class Material;
}

namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Node;
}

namespace editor
{

class Content_library;
class Mesh_memory;
class Render_context;
class Scene_root;

class Material_preview
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Material_preview"};
    static constexpr std::string_view c_title    {"Material preview"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Material_preview ();
    ~Material_preview() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    [[nodiscard]] auto get_scene_root() -> std::shared_ptr<Scene_root>;

    void render_preview(
        const std::shared_ptr<Content_library>&           content_library,
        const std::shared_ptr<erhe::primitive::Material>& material,
        const erhe::scene::Viewport&                      viewport
    );

private:
    void make_rendertarget();
    void make_preview_scene();
    //// void generate_torus_geometry();

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration> m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows> m_imgui_windows;
    std::shared_ptr<Mesh_memory>                      m_mesh_memory;

    int                                           m_width;
    int                                           m_height;
    gl::Internal_format                           m_color_format;
    gl::Internal_format                           m_depth_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;

    std::shared_ptr<Scene_root>          m_scene_root;
    std::shared_ptr<erhe::scene::Node>   m_node;
    std::shared_ptr<erhe::scene::Mesh>   m_mesh;
    std::shared_ptr<erhe::scene::Node>   m_key_light_node;
    //// std::shared_ptr<erhe::scene::Node>  m_fill_light_node;
    std::shared_ptr<erhe::scene::Light>  m_key_light;
    //// std::shared_ptr<erhe::scene::Light> m_fill_light;
    std::shared_ptr<erhe::scene::Node>   m_camera_node;
    std::shared_ptr<erhe::scene::Camera> m_camera;

    std::shared_ptr<Content_library>           m_last_content_library;
    std::shared_ptr<erhe::primitive::Material> m_last_material;

    glm::vec4 m_clear_color{0.0f, 0.0f, 0.0f, 0.0f};

    int   m_slice_count{40};
    int   m_stack_count{22};
    float m_radius{1.0f};
    ////std::shared_ptr<erhe::geometry::Geometry> m_sphere_geometry;

    ////int   m_major_steps{40};
    ////int   m_minor_steps{22};
    ////float m_major_radius{1.00f};
    ////float m_minor_radius{0.25f};
    ////std::shared_ptr<erhe::geometry::Geometry> m_torus_geometry;
};

} // namespace editor
