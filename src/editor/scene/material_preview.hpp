#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/scene/viewport.hpp"

#include <gsl/gsl>

namespace erhe::graphics
{
    class Framebuffer;
    class Renderbuffer;
    class Texture;
}

namespace erhe::primitive
{
    class Material;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Node;
}

namespace editor
{

class Content_library;
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
    void deinitialize_component     () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    [[nodiscard]] auto get_scene_root     () -> std::shared_ptr<Scene_root>;
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;

    void render_preview(
        const std::shared_ptr<Content_library>&           content_library,
        const std::shared_ptr<erhe::primitive::Material>& material,
        const erhe::scene::Viewport&                      viewport
    );

private:
    void make_rendertarget();
    void make_preview_scene();
    //// void generate_torus_geometry();

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
    std::shared_ptr<erhe::scene::Light>  m_key_light;
    std::shared_ptr<erhe::scene::Node>   m_camera_node;
    std::shared_ptr<erhe::scene::Camera> m_camera;

    std::shared_ptr<Content_library>           m_last_content_library;
    std::shared_ptr<erhe::primitive::Material> m_last_material;

    glm::vec4 m_clear_color{0.0f, 0.0f, 0.0f, 0.0f};

    int   m_slice_count{40};
    int   m_stack_count{22};
    float m_radius{1.0f};
};

extern Material_preview* g_material_preview;

} // namespace editor
