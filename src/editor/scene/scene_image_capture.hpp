#pragma once

#include "scene/scene_view.hpp"

#include "erhe_math/viewport.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
}
namespace erhe::scene {
    class Camera;
}

namespace editor {

class App_context;
class Post_processing_node;
class Scene_image_capture;
class Scene_root;
class Shadow_render_node;

// Offscreen scene render for the MCP tool render_scene_image
// (doc/editor/rendergraph.md "Scene image capture").
//
// Scene_image_view is a window-less sibling of Viewport_scene_view: a
// Scene_view and a Texture_rendergraph_node with the same HDR color format,
// fed by its own Shadow_render_node and followed by its own
// Post_processing_node (tonemap), but with an explicit camera, size and
// viewport config instead of a Viewport_window's. It renders scene content
// only: no ID pass, no tools, no renderables, no debug renderer / text, and
// the composition passes marked Composition_pass_kind::editor_aid (grid,
// selection outline, ghost edges, brush, solid bones) are skipped through
// Render_content::scene_only.
class Scene_image_view
    : public Scene_view
    , public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Scene_image_view(
        App_context&                                context,
        erhe::rendergraph::Rendergraph&             rendergraph,
        const Viewport_config&                      viewport_config,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         width,
        int                                         height,
        int                                         msaa_sample_count,
        erhe::scene_renderer::Shader_debug          shader_debug
    );
    ~Scene_image_view() noexcept override;

    // Implements Scene_view
    auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    auto get_projection_scale  (float view_distance) const -> float            override;
    auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;
    auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    auto get_camera_viewport   () const -> erhe::math::Viewport                 override { return m_viewport; }

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Scene_image_view"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // True once execute_rendergraph_node() rendered the scene into the color
    // texture (scene root and camera were alive).
    [[nodiscard]] auto has_rendered() const -> bool { return m_has_rendered; }

private:
    // Owned: an explicit-pose camera exists only for this capture, so the view
    // keeps it alive (a scene camera is shared with its scene).
    std::shared_ptr<erhe::scene::Camera> m_camera;
    erhe::math::Viewport                 m_viewport{0, 0, 0, 0};
    erhe::scene_renderer::Shader_debug   m_shader_debug{erhe::scene_renderer::Shader_debug::none};
    bool                                 m_has_rendered{false};
};

// Final node of a capture chain: records the texture-to-buffer copy of the
// image to read back, then disables the chain.
class Scene_image_readback_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Scene_image_readback_node(erhe::rendergraph::Rendergraph& rendergraph, Scene_image_capture& capture);

    auto get_type_name           () const -> std::string_view override { return "Scene_image_readback_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

private:
    Scene_image_capture& m_capture;
};

enum class Scene_image_output : unsigned int {
    png = 0, // tonemapped (post-processed) image, what a viewport shows
    linear   // linear HDR scene color before post-processing
};

enum class Scene_image_capture_state : unsigned int {
    rendering = 0, // chain enabled, waiting for the rendergraph to run it
    in_flight,     // copy recorded, waiting for that frame to retire
    complete,      // pixels read back (get_pixels)
    failed         // nothing to read (no scene / camera / texture); see get_error
};

class Scene_image_capture_create_info
{
public:
    std::shared_ptr<Scene_root>          scene_root;
    std::shared_ptr<erhe::scene::Camera> camera;
    int                                  width            {0};
    int                                  height           {0};
    int                                  msaa_sample_count{0};
    Scene_image_output                   output           {Scene_image_output::png};
    erhe::scene_renderer::Shader_debug   shader_debug     {erhe::scene_renderer::Shader_debug::none};
};

// One render_scene_image request: builds the chain Shadow_render_node ->
// Scene_image_view -> Post_processing_node -> Scene_image_readback_node when
// constructed, the rendergraph runs it on the next frame, and the destructor
// removes every node again. It exists only while a request is pending, so the
// tool costs nothing per frame when unused. Destroy it only once poll()
// reports complete or failed (the recorded frame has retired, and the chain
// has not executed since), so no GPU work references its textures.
class Scene_image_capture
{
public:
    Scene_image_capture(App_context& context, const Scene_image_capture_create_info& create_info);
    ~Scene_image_capture() noexcept;

    Scene_image_capture(const Scene_image_capture&) = delete;
    auto operator=(const Scene_image_capture&) -> Scene_image_capture& = delete;

    [[nodiscard]] auto poll      () -> Scene_image_capture_state;
    [[nodiscard]] auto get_width () const -> int { return m_width; }
    [[nodiscard]] auto get_height() const -> int { return m_height; }
    // RGBA float, rows top to bottom; valid while poll() reports complete.
    [[nodiscard]] auto get_pixels() const -> std::span<const glm::vec4>;
    [[nodiscard]] auto get_error () const -> const std::string& { return m_error; }
    [[nodiscard]] auto get_output() const -> Scene_image_output { return m_output; }

    // Called by Scene_image_readback_node during Rendergraph::execute().
    void record_readback(erhe::graphics::Command_buffer& command_buffer);

private:
    void disable_chain();

    App_context&                               m_context;
    int                                        m_width {0};
    int                                        m_height{0};
    Scene_image_output                         m_output{Scene_image_output::png};
    Scene_image_capture_state                  m_state {Scene_image_capture_state::rendering};
    std::string                                m_error;
    std::shared_ptr<Scene_image_view>          m_view;
    std::shared_ptr<Shadow_render_node>        m_shadow_render_node;
    std::shared_ptr<Post_processing_node>      m_post_processing_node;
    std::shared_ptr<Scene_image_readback_node> m_readback_node;
    std::unique_ptr<erhe::graphics::Buffer>    m_readback_buffer;
    uint64_t                                   m_readback_frame{0};
    std::vector<glm::vec4>                     m_pixels;
};

} // namespace editor
