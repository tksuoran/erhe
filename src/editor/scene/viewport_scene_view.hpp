#pragma once

#include "renderers/composer.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_render_state.hpp"

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/overdraw_counter.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_host;
    class Imgui_windows;
}
namespace erhe::renderer {
    class Line_renderer_set;
    class Text_renderer;
}
namespace erhe::rendergraph {
    class Multisample_resolve_node;
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Light_projections;
}

namespace ImViewGuizmo {
    class Context;
}

struct Viewport_config_data;

namespace editor {

enum class Renderer_choice : int {
    forward,
    draw_list
};

static constexpr const char* c_renderer_choice_strings[] = {
    "Forward Renderer",
    "Draw List Renderer"
};

class App_message_bus;
class App_rendering;
class App_scenes;
class Editor_settings_store;
class Physics_window;
class Post_processing;
class Post_processing_node;
class Programs;
class Render_context;
class Scene_root;
class Selection_tool;
class Shadow_render_node;
class Transform_tool;
class Viewport_scene_view;
class Scene_views;

// Rendergraph producer node for rendering contents of scene into output rendergraph node.
// - See execute_rendergraph_node(): render_viewport_main() is used to render to connected
//   consumer rendergraph node, which provides viewport and framebuffer
// 
// Typical output rendergraph nodes are:
//  - Post_processing_node
//  - default Rendergraph_node representing default framebuffer
class Viewport_scene_view
    : public Scene_view
    , public erhe::rendergraph::Texture_rendergraph_node
    , public std::enable_shared_from_this<Viewport_scene_view>
{
public:
    Viewport_scene_view(
        Editor_settings_store*                      editor_settings_store,
        const Viewport_config_data&                 viewport_config_data,
        App_context&                                context,
        erhe::rendergraph::Rendergraph&             rendergraph,
        std::string_view                            name,
        const char*                                 ini_label,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         msaa_sample_count,
        bool                                        enable_post_processing
    );
    ~Viewport_scene_view() noexcept override;

    // Implements Scene_view
    auto get_camera                () const -> std::shared_ptr<erhe::scene::Camera>         override;
    auto get_projection_scale     (float view_distance) const -> float                                        override;
    auto get_rendergraph_node      () -> erhe::rendergraph::Rendergraph_node*               override;
    auto get_shadow_render_node    () const -> Shadow_render_node*                          override;
    auto get_camera_viewport       () const -> erhe::math::Viewport                         override { return m_projection_viewport; }
    auto as_viewport_scene_view    () -> Viewport_scene_view*                               override;
    auto as_viewport_scene_view    () const -> const Viewport_scene_view*                   override;
    auto get_closest_point_on_line (glm::vec3 P0, glm::vec3 P1) -> std::optional<glm::vec3> override;
    auto get_closest_point_on_plane(glm::vec3 N , glm::vec3 P ) -> std::optional<glm::vec3> override;

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Viewport_scene_view"; }

    // M2: copies the render state (Viewport_render_state) of the frame it
    // prepares from the live members, then records this view's tools,
    // renderables and debug visualizations into the debug and text
    // renderers (one frame slot per view), under the conditions
    // execute_rendergraph_node() renders with. The render phase reads the
    // state of the frame it renders and expands and draws the slot
    // (doc/editor/editor.md "Frame phases and the render thread"). The
    // command buffer is the upload command buffer; the recording needs it
    // for debug groups only.
    void prepare_render(erhe::graphics::Command_buffer& upload_command_buffer);
    // M2, the last step (App_rendering::prepare_composition): prepares this
    // view's composition (pass gating and bucketed draws) into the frame's
    // composer slot, under the same conditions execute_rendergraph_node()
    // renders with, reading the render state prepare_render() copied. The
    // render phase draws the slot and reads no mesh for the bucket passes.
    void prepare_composition(uint64_t frame_index, std::size_t frames_in_flight, int64_t host_system_time_ns);
    // The render state copy of frame_index (one per frame in flight);
    // verifies prepare_render() filled it for that frame.
    [[nodiscard]] auto get_render_state(uint64_t frame_index) const -> const Viewport_render_state&;

    // M2, from Grid::render() while prepare_render() records the tools: the
    // grid composition pass inputs of this view for the frame being
    // prepared.
    void record_grid(const Grid_pass_state& grid);

    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;
    // Render phase, before the frame's culling classification: builds the
    // frame's render context (the one execute_rendergraph_node() and
    // render_overlay_pass() draw with) and adds the color culling views of
    // the view's draw-list passes from it (Composer::add_cull_views();
    // doc/plans/culling.md phase 6a).
    void pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // Post-processing overlay pass (issue #230). Called by Viewport_overlay_node
    // AFTER the post-processing node when post-processing is enabled. Draws the
    // tool gizmo / hotbar rendertarget overlay meshes on top of the
    // post-processed image (input_texture), depth-testing against this view's
    // content render target depth/stencil attachment (which is stored for that
    // purpose). get_overlay_output_texture returns the resulting image.
    void render_overlay_pass(
        erhe::graphics::Command_buffer&                 command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>& input_texture
    );
    [[nodiscard]] auto get_overlay_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>;

    // Public API
    void set_window_viewport         (erhe::math::Viewport viewport);
    // Main thread (the graphics preset change): the content render target's
    // MSAA sample count, applied by the render phase of the next prepared
    // frame (Viewport_render_state::msaa_sample_count).
    void set_msaa_sample_count       (int sample_count);
    void set_is_scene_view_hovered   (bool is_hovered);
    void set_camera                  (const std::shared_ptr<erhe::scene::Camera>& camera);
    void update_pointer_2d_position  (glm::vec2 position_in_viewport);
    void update_hover                (bool ray_only = false);
    // Force the ID pass to render for the next `frame_count` frames even
    // though no real pointer hovers the viewport. The MCP pick_at probe needs
    // this: it arms a pointer position, but the per-frame ImGui draw resets
    // the hovered flag before rendering, so without the override the ID pass
    // (the only correct pick source for skinned meshes) never runs headlessly.
    void arm_pick_probe              (int frame_count);
    void request_cursor_relative_hold(bool relative_hold_enable);
    
    void viewport_toolbar            () override;
    
    void set_shader_debug            (erhe::scene_renderer::Shader_debug shader_debug);
    auto get_shader_debug            () const -> erhe::scene_renderer::Shader_debug;

    // Main thread: the overdraw visualization of the view, applied from the
    // next prepared frame (Viewport_render_state::overdraw). Only on devices
    // with is_overdraw_supported().
    void set_overdraw_mode           (Overdraw_mode mode);
    [[nodiscard]] auto get_overdraw_mode() const -> Overdraw_mode;
    [[nodiscard]] auto is_overdraw_supported() const -> bool;
    // Any thread: the overdraw statistics of the newest retired frame the
    // view counted (not valid before one retired, nor after the mode turned
    // off).
    [[nodiscard]] auto get_overdraw_statistics() const -> erhe::scene_renderer::Overdraw_statistics;
    // Main thread: the visualization's color ramp reaches white at this
    // count (Viewport_render_state::overdraw_max_count).
    void set_overdraw_max_count(uint32_t max_count);
    [[nodiscard]] auto get_overdraw_max_count() const -> uint32_t;
    // Render phase: the view's count buffer of frame_index, which its
    // execute filled in that frame; null when it did not count (Overdraw_node
    // reads it).
    [[nodiscard]] auto get_overdraw_count_buffer(uint64_t frame_index) const -> const erhe::graphics::Buffer*;

    void set_renderer_choice(Renderer_choice choice);
    auto get_renderer_choice() const -> Renderer_choice;

    [[nodiscard]] auto get_ini_label                       () const -> const char* { return m_ini_label; }
    [[nodiscard]] auto get_viewport_from_window            (glm::vec2 position_in_window) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport                 (glm::vec3 position_in_world ) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto unproject_to_world                  (glm::vec3 position_in_window) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto is_scene_view_hovered               () const -> bool;
    [[nodiscard]] auto get_window_viewport                 () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_projection_viewport             () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_msaa_sample_count               () const -> int;
    [[nodiscard]] auto get_position_in_viewport            () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto get_position_in_world_viewport_depth(float viewport_depth) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_show_navigation_gizmo           () const -> bool;
    [[nodiscard]] auto get_cursor_relative_hold            () const -> bool;

    // Per-viewport navigation gizmo (ImGui overlay). Drawn by Viewport_window and
    // driven (orbit/zoom/pan/snap) by Navigation_gizmo_tool via erhe::commands.
    [[nodiscard]] auto get_navigation_gizmo                () -> ImViewGuizmo::Context&;

    // Called by ~Scene_views on every view it still tracks, so a view that
    // outlives Scene_views (it is a Rendergraph_node, and therefore also a
    // Texture_reference that the Imgui_renderer may still retain) does not
    // call erase() on the destroyed Scene_views from its own destructor.
    // Same shape as Rendergraph_node's m_is_registered / ~Rendergraph.
    void forget_scene_views() { m_registered_in_scene_views = false; }

private:
    void update_hover_with_id_render();

    // M2 (prepare_composition): the edge-line feed of the wide-line passes,
    // gated as the passes draw (enabled, the view's render style), with each
    // pass's primitive settings at the prepared frame's host time.
    void prepare_wide_line_feed(
        const Render_context&                         context,
        int64_t                                       host_system_time_ns,
        erhe::scene_renderer::Content_wide_line_feed& out
    );

    // (Re)builds the overlay render target (color matching the content sample
    // count + single-sample resolved output) and its render pass, which loads
    // the content render target's depth/stencil attachment. See issue #230.
    void update_overlay_render_target(int width, int height);

    // Render phase (execute_rendergraph_node()): the content render pass of
    // the frame - begins it (clearing the render target) and, when the
    // frame renders, draws the composition, renderables and (without
    // post-processing) the tools.
    void record_content_render_pass(Render_context& context, const Viewport_render_state& state);

    // Implements Scene_view per-view "Scene and Camera" persistence: adds the
    // camera, shader_debug and renderer_choice that live on this derived view.
    void write_scene_and_camera_settings(Scene_and_camera_settings& out) const override;
    auto resolve_pending_scene_and_camera() -> bool override;

    static int s_serial;

    bool                               m_registered_in_scene_views{true};

    std::optional<glm::vec2>           m_position_in_viewport {};

    std::string                        m_name                 {};
    const char*                        m_ini_label            {nullptr};
    std::weak_ptr<erhe::scene::Camera> m_camera               {};

    // Host window bounds in host space (ImGui window, ImGui viewport / Context_window)
    erhe::math::Viewport               m_window_viewport      {0, 0, 0, 0};

    // viewport for 3d camera
    erhe::math::Viewport               m_projection_viewport  {0, 0, 0, 0};
    int                                m_msaa_sample_count    {0};

    erhe::scene_renderer::Shader_debug m_shader_debug                   {erhe::scene_renderer::Shader_debug::none};
    Overdraw_mode                      m_overdraw_mode                  {Overdraw_mode::off};
    uint32_t                           m_overdraw_max_count             {8};
    Renderer_choice                    m_renderer_choice                {Renderer_choice::forward};
    bool                               m_is_scene_view_hovered          {false};
    int                                m_pick_probe_frames              {0}; // see arm_pick_probe()
    bool                               m_show_navigation_gizmo          {true};
    bool                               m_relative_hold_enable           {false};
    std::unique_ptr<ImViewGuizmo::Context> m_navigation_gizmo;
    Property_editor m_property_editor;


    // Post-processing overlay pass (issue #230). When post-processing is enabled
    // the content render pass stores depth/stencil and these resources draw the
    // tool / rendertarget overlay after post-processing, sharing that depth.
    bool                                         m_post_processing_enabled{false};
    std::shared_ptr<erhe::graphics::Texture>     m_overlay_color_texture;    // multisampled when MSAA, else == resolved
    std::shared_ptr<erhe::graphics::Texture>     m_overlay_resolved_texture; // single-sample output shown by ImGui
    std::unique_ptr<erhe::graphics::Render_pass> m_overlay_render_pass;
    const erhe::graphics::Texture*               m_overlay_depth_texture{nullptr};
    int                                          m_overlay_width {0};
    int                                          m_overlay_height{0};
    // The render state, one copy per frame in flight, indexed by frame
    // index modulo their count; sized at the first prepare_render().
    std::vector<Viewport_render_state>           m_render_states;
    // This view's prepared composition, one slot per frame in flight.
    Composer_frame_slots                         m_composer_slots;
    // Render phase owned, the frame being rendered: the render context
    // pre_execute_rendergraph_node() built (empty when the node renders
    // nothing this frame) and the single view its views span names.
    erhe::scene_renderer::Camera_view_input      m_frame_view_input{};
    std::optional<Render_context>                m_frame_context;
    uint64_t                                     m_frame_context_frame_index{std::numeric_limits<uint64_t>::max()};
    // The overdraw counting of the view: render phase owned, except
    // get_statistics(), which any thread may call.
    erhe::scene_renderer::Overdraw_view_state    m_overdraw_view_state;
};

}
