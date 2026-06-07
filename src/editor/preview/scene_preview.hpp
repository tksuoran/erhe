#pragma once

#include "renderers/composer.hpp"
#include "scene/scene_view.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics {
    class Command_buffer;
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
}
namespace erhe::dataformat { class Vertex_format; }
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Mesh_memory;
}

namespace editor {

class App_context;
class App_rendering;
class Content_library;
class Programs;
class Scene_root;
class Tools;

class Scene_preview : public Scene_view
{
public:
    Scene_preview(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        App_context&                    app_context
    );
    ~Scene_preview() noexcept;

    // Implements Scene_view
    auto get_camera           () const -> std::shared_ptr<erhe::scene::Camera>           override;
    auto get_perspective_scale() const -> float                                          override;
    auto get_rendergraph_node ()       -> erhe::rendergraph::Rendergraph_node*           override;
    auto get_light_projections() const -> const erhe::scene_renderer::Light_projections* override;
    auto get_shadow_texture   () const -> erhe::graphics::Texture*                       override;

    // Public API
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;

    void resize                 (int width, int height);
    void set_color_texture      (const std::shared_ptr<erhe::graphics::Texture>& color_texture);
    void set_color_texture_layer(unsigned int layer);
    void set_clear_color        (glm::vec4 clear_color);
    void update_rendertarget    (erhe::graphics::Device& graphics_device);

    // Init-time prewarm. Drives Forward_renderer::prewarm_standard_variants
    // against this preview's own scene_root + content_library, so the
    // first time the user opens the preview window does not pay for
    // glslang -> SPIR-V compile of the standard variants for the
    // preview's specific (Light_layer, content-library materials,
    // single-view view_count=0) combination. Pipelines targeted are
    // m_render_pipeline_states; the bucket+extra_materials walk uses
    // mesh_memory.vertex_format as the fallback. Safe to call before
    // update_rendertarget has built the offscreen render pass -- this
    // only populates the shader-module cache, not the per-render-pass
    // VkPipeline cache.
    void prewarm_variants(erhe::scene_renderer::Forward_renderer& forward_renderer);

protected:
    App_context&                                       m_context;
    bool                                               m_y_flip;
    int                                                m_width{0};
    int                                                m_height{0};
    glm::vec4                                          m_clear_color{0.5f, 0.5f, 0.5f, 0.0f};
    bool                                               m_use_external_color_texture{false};
    erhe::dataformat::Format                           m_color_format;
    std::shared_ptr<erhe::graphics::Texture>           m_color_texture;
    unsigned int                                       m_color_texture_layer{0};
    erhe::dataformat::Format                           m_depth_format;
    std::unique_ptr<erhe::graphics::Texture>           m_depth_texture;
    std::shared_ptr<erhe::graphics::Render_pass>       m_render_pass;
    erhe::scene_renderer::Light_projections            m_light_projections;
    erhe::graphics::Base_render_pipeline               m_render_pipeline;
    std::vector<erhe::graphics::Base_render_pipeline*> m_render_pipelines;
    Composer                                           m_composer;
    std::shared_ptr<Scene_root>                        m_scene_root_shared;
    std::shared_ptr<erhe::scene::Node>                 m_camera_node;
    std::shared_ptr<erhe::scene::Camera>               m_camera;
    std::shared_ptr<Content_library>                   m_content_library;
    std::shared_ptr<erhe::graphics::Texture>           m_shadow_texture;
};

}
