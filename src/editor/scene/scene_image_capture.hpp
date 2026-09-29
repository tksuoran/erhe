#pragma once

#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
    class Gpu_timer;
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

// One shadow-mapped light of the shadow pass a scene image render used, copied
// from that pass's Light_projections (the matrices the forward pass sampled
// with; the directional fit depends on the render's own camera).
class Scene_image_shadow_light
{
public:
    std::string             name;
    std::size_t             id                {0};
    erhe::scene::Light_type type              {erhe::scene::Light_type::directional};
    std::size_t             slot              {0}; // light block index
    // 2D shadow map array layer (directional, spot) or cube index (point:
    // layers [6 * index, 6 * index + 6) of the cube array).
    std::size_t             layer             {0};
    glm::mat4               texture_from_world{1.0f};
    glm::mat4               clip_from_world   {1.0f};
    // Origin of the light's shadow camera in world space: for a point light
    // the centre the shader measures the radial cube distances from.
    glm::vec3               position          {0.0f};
    // The light's 2D shadow bias limits (erhe::scene_renderer::
    // Light_shadow_limits): the raster vertex depth bound of the minimum
    // bias, and whether the distance technique's ray spread condition holds
    // (false: a spot light the distance technique samples with the depth
    // technique).
    float                   raster_vertex_depth{1.0f};
    bool                    distance_rays_valid{true};
};

// Shadow map textures of the same shadow pass.
class Scene_image_shadow_maps
{
public:
    int                      map_width      {0};
    int                      map_height     {0};
    erhe::dataformat::Format map_format     {erhe::dataformat::Format::format_undefined};
    // Shadow_technique_mode::distance R32F map; undefined for the depth technique.
    erhe::dataformat::Format distance_format{erhe::dataformat::Format::format_undefined};
    int                      cube_size      {0};
    erhe::dataformat::Format cube_format    {erhe::dataformat::Format::format_undefined};
    bool                     reverse_depth  {false};
    erhe::math::Depth_range  depth_range    {erhe::math::Depth_range::zero_to_one};
};

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
        erhe::dataformat::Format                    color_format,
        Render_content                              content,
        erhe::scene_renderer::Shader_debug          shader_debug,
        const std::shared_ptr<erhe::scene::Light>&  shadow_debug_light,
        erhe::graphics::Gpu_timer*                  forward_pass_timer
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

    // Light slot (Light_projection_transforms::index) the render resolved the
    // shadow debug light to; empty when no light was requested or the light
    // got no slot (not shaded) in that frame's light set.
    [[nodiscard]] auto get_shadow_debug_light_index() const -> std::optional<uint32_t> { return m_shadow_debug_light_index; }

    // Shadow-mapped lights and shadow map textures of this render's shadow
    // pass, copied when the view rendered.
    [[nodiscard]] auto get_shadow_lights() const -> std::span<const Scene_image_shadow_light> { return m_shadow_lights; }
    [[nodiscard]] auto get_shadow_maps  () const -> const Scene_image_shadow_maps& { return m_shadow_maps; }

private:
    [[nodiscard]] auto resolve_shadow_debug_light_index() -> uint32_t;
    void copy_shadow_projections();

    // Owned: an explicit-pose camera exists only for this capture, so the view
    // keeps it alive (a scene camera is shared with its scene).
    std::shared_ptr<erhe::scene::Camera> m_camera;
    erhe::math::Viewport                 m_viewport{0, 0, 0, 0};
    Render_content                        m_content     {Render_content::scene_only};
    erhe::scene_renderer::Shader_debug    m_shader_debug{erhe::scene_renderer::Shader_debug::none};
    // Light Shader_debug::shadow_visibility shows; null = slot 0.
    std::shared_ptr<erhe::scene::Light>   m_shadow_debug_light;
    // Explicit-range timer bracketing the forward render pass; null = untimed.
    erhe::graphics::Gpu_timer*            m_forward_pass_timer{nullptr};
    std::optional<uint32_t>               m_shadow_debug_light_index;
    std::vector<Scene_image_shadow_light> m_shadow_lights;
    Scene_image_shadow_maps               m_shadow_maps;
    bool                                  m_has_rendered{false};
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

// What the pixels no scene surface covers hold.
enum class Scene_image_background : unsigned int {
    sky = 0, // the sky pass (or the clear color), as viewports show
    marked   // sky pass skipped; the readback sets their RGB to NaN
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
    // Scene color target format: format_16_vec4_float or (output linear only)
    // format_32_vec4_float. The caller checks device support.
    erhe::dataformat::Format             color_format     {erhe::dataformat::Format::format_16_vec4_float};
    Scene_image_background               background       {Scene_image_background::sky};
    erhe::scene_renderer::Shader_debug   shader_debug     {erhe::scene_renderer::Shader_debug::none};
    // Light whose visibility Shader_debug::shadow_visibility shows. Resolved
    // to its light slot by the render itself, from the light set that render's
    // shadow pass built; null keeps slot 0.
    std::shared_ptr<erhe::scene::Light>  shadow_debug_light;
    // Explicit-range Gpu_timer the view brackets its forward render pass with
    // (null: untimed). Owned by the caller, which keeps it alive until the
    // result is read, frames after the capture itself is gone.
    erhe::graphics::Gpu_timer*           forward_pass_timer{nullptr};
};

// One render_scene_image request: builds the chain Shadow_render_node ->
// Scene_image_view -> Post_processing_node (png output only) ->
// Scene_image_readback_node when constructed, the rendergraph runs it on the next frame, and the destructor
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
    [[nodiscard]] auto get_shadow_debug_light_index() const -> std::optional<uint32_t>;
    [[nodiscard]] auto get_shadow_lights() const -> std::span<const Scene_image_shadow_light>;
    [[nodiscard]] auto get_shadow_maps  () const -> const Scene_image_shadow_maps&;
    // Scene_image_background::marked: pixels with no surface coverage (their
    // RGB is NaN in get_pixels()).
    [[nodiscard]] auto get_background_pixel_count() const -> std::size_t { return m_background_pixel_count; }

    // Called by Scene_image_readback_node during Rendergraph::execute().
    void record_readback(erhe::graphics::Command_buffer& command_buffer);

private:
    void disable_chain();

    App_context&                               m_context;
    int                                        m_width {0};
    int                                        m_height{0};
    Scene_image_output                         m_output{Scene_image_output::png};
    erhe::dataformat::Format                   m_color_format{erhe::dataformat::Format::format_16_vec4_float};
    Scene_image_background                     m_background{Scene_image_background::sky};
    std::size_t                                m_background_pixel_count{0};
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
