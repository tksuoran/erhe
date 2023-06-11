#pragma once

#include "renderers/composer.hpp"
#include "scene/scene_view.hpp"

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/renderer//light_buffer.hpp"
#include "erhe/renderer//pipeline_renderpass.hpp"
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

namespace erhe::renderer
{
    class Light_projections;
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
    , public Scene_view
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
    //void imgui() override;

    // Implements Scene_view
    auto get_scene_root       () const -> std::shared_ptr<Scene_root>                          override;
    auto get_camera           () const -> std::shared_ptr<erhe::scene::Camera>                 override;
    auto get_rendergraph_node ()       -> std::shared_ptr<erhe::application::Rendergraph_node> override;
    auto get_light_projections() const -> const erhe::renderer::Light_projections*             override;
    auto get_shadow_texture   () const -> erhe::graphics::Texture*                             override;

    // Public API
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;

    void render_preview(
        const std::shared_ptr<erhe::primitive::Material>& material
        //const erhe::scene::Viewport&                      viewport
    );
    void show_preview();

private:
    void make_rendertarget();
    void make_preview_scene();
    //// void generate_torus_geometry();

    int                                           m_width{0};
    int                                           m_height{0};
    gl::Internal_format                           m_color_format;
    gl::Internal_format                           m_depth_format;
    std::shared_ptr<erhe::graphics::Texture>      m_color_texture;
    std::unique_ptr<erhe::graphics::Renderbuffer> m_depth_renderbuffer;
    std::shared_ptr<erhe::graphics::Framebuffer>  m_framebuffer;
    erhe::renderer::Light_projections             m_light_projections;
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

extern Material_preview* g_material_preview;

} // namespace editor
