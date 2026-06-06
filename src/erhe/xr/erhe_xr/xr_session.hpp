#pragma once

#include "erhe_xr/xr.hpp"
#include "erhe_xr/xr_swapchain_image.hpp"
#include <openxr/openxr.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::graphics { class Command_buffer; class Device; }
namespace erhe::window   { class Context_window; }

namespace erhe::xr {

class Quad_layer;
class Xr_instance;

// One XR_FB_performance_metrics counter the runtime exposes (e.g.
// app_cpu_time_ms, compositor_dropped_frame_count, gpu_utilization). The
// Performance window auto-builds one plot per entry of this list when the
// extension is enabled.
class Xr_perf_counter
{
public:
    XrPath                          path        {XR_NULL_PATH};
    std::string                     display_name;
    XrPerformanceMetricsCounterMETA last       {.type = XR_TYPE_PERFORMANCE_METRICS_COUNTER_META, .next = nullptr};
};

// Aggregate handed to the multiview render callback. The shared color
// and depth textures are 2D array textures that span every view; the
// caller renders into them once with VK_KHR_multiview's viewMask, and
// the layer per view is selected via gl_ViewIndex inside the shaders.
// view_mask is the bitmask of layers the renderer should write to
// (typically (1u << view_count) - 1, e.g. 0b11 for stereo).
class Render_views_frame
{
public:
    std::vector<Render_view> views;
    erhe::graphics::Texture* shared_color_texture        {nullptr};
    erhe::graphics::Texture* shared_depth_stencil_texture{nullptr};
    // Shared fragment density map (fixed foveated rendering) spanning every
    // view (one 2D-array layer per view), or nullptr when the swapchain was
    // not created with foveation. Non-owning, same lifetime as
    // shared_color_texture.
    erhe::graphics::Texture* shared_fragment_density_map_texture{nullptr};
    uint32_t                 view_mask                   {0};
    uint32_t                 width                       {0};
    uint32_t                 height                      {0};
};

class Xr_session
{
public:
    Xr_session(
        Xr_instance&                  instance,
        erhe::window::Context_window& context_window,
        erhe::graphics::Device&       graphics_device,
        bool                          mirror_mode
    );
    ~Xr_session   () noexcept;
    Xr_session    (const Xr_session&) = delete;
    void operator=(const Xr_session&) = delete;
    Xr_session    (Xr_session&&)      = delete;
    void operator=(Xr_session&&)      = delete;

    [[nodiscard]] auto begin_session               () -> bool;
    [[nodiscard]] auto end_session                 () -> bool;
    [[nodiscard]] auto is_session_running          () const -> bool;
    [[nodiscard]] auto begin_frame                 () -> bool;
    [[nodiscard]] auto wait_frame                  () -> XrFrameState*;
    [[nodiscard]] auto render_frame                (erhe::graphics::Command_buffer& command_buffer, std::function<bool(Render_view&, erhe::graphics::Command_buffer&)> render_view_callback) -> bool;
    // Multiview render frame variant: acquires a single shared layered
    // color (and optional layered depth) image from the multiview
    // swapchain, builds a Render_views_frame describing all per-view
    // poses + the shared 2D-array textures, and invokes the callback
    // once. The callback records a single command buffer that draws
    // every view in one render pass via VK_KHR_multiview. Only valid
    // when is_multiview_enabled() returns true; returns false otherwise.
    [[nodiscard]] auto render_frame_multiview      (erhe::graphics::Command_buffer& command_buffer, std::function<bool(const Render_views_frame&, erhe::graphics::Command_buffer&)> render_views_callback) -> bool;
    [[nodiscard]] auto end_frame                   (const bool rendered) -> bool;
    [[nodiscard]] auto is_multiview_enabled        () const -> bool;

    // Quad composition layers. create_quad_layer() creates a dedicated color
    // swapchain (using the session's selected swapchain color format) and a
    // Quad_layer that registers itself with this session; end_frame() then
    // submits every visible, rendered quad layer as an XrCompositionLayerQuad
    // after the projection layer. Returns nullptr if the swapchain could not
    // be created (the caller is expected to fall back to scene-mesh rendering).
    [[nodiscard]] auto create_quad_layer(uint32_t width, uint32_t height, const std::string& debug_label) -> std::unique_ptr<Quad_layer>;
    void register_quad_layer  (Quad_layer* quad_layer);
    void unregister_quad_layer(Quad_layer* quad_layer);
    [[nodiscard]] auto get_view_count              () const -> uint32_t;
    [[nodiscard]] auto get_xr_session              () const -> XrSession;
    // Swapchain formats picked by enumerate_swapchain_formats() during
    // session construction. Both are well-defined by the time the
    // editor's prewarm phase runs; the depth/stencil format may be
    // format_undefined when the runtime did not advertise a usable depth
    // swapchain format. Used by the init-time pipeline warmup to drive
    // Device::warmup_render_pipeline against the same format tuple the
    // headset's per-frame render pass will use.
    [[nodiscard]] auto get_swapchain_color_format         () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_swapchain_depth_stencil_format () const -> erhe::dataformat::Format;
    // OpenXR's per-view recommendedSwapchainSampleCount. The runtime
    // controls swapchain MSAA, not the editor: the swapchain images this
    // session creates carry exactly this sample count, and the headset
    // render pass inherits it via the swapchain texture. Multiview
    // creation (see m_use_multiview) verifies the recommended count
    // matches across views, so returning view 0 is sufficient. Returns
    // 1 when no view configuration is available.
    [[nodiscard]] auto get_swapchain_sample_count         () const -> uint32_t;
    [[nodiscard]] auto get_xr_reference_space_local() const -> XrSpace;
    [[nodiscard]] auto get_xr_reference_space_stage() const -> XrSpace;
    [[nodiscard]] auto get_xr_reference_space_view () const -> XrSpace;
    [[nodiscard]] auto get_xr_frame_state          () const -> const XrFrameState&;
    void update_hand_tracking();
    void update_view_pose    ();
    void set_state           (XrSessionState state);

    [[nodiscard]] auto get_hand_tracking_joint (const XrHandEXT hand, const XrHandJointEXT joint) const -> Hand_tracking_joint;
    [[nodiscard]] auto get_hand_tracking_active(const XrHandEXT hand) const -> bool;
    [[nodiscard]] auto get_view_space_location () const -> const XrSpaceLocation&;
    [[nodiscard]] auto get_state               () const -> XrSessionState;

    // XR_FB_performance_metrics: refresh the cached counter values once. Call
    // each tick from Headset::begin_frame_; cheap (runtime-cached lookup).
    void query_performance_metrics();
    [[nodiscard]] auto get_perf_counters() const -> const std::vector<Xr_perf_counter>&;

    // XR_FB_passthrough lifecycle. Passthrough is created and started during
    // session creation whenever the runtime supports it (so the init status
    // screen shows the room, issue #214); set_passthrough_active(false) pauses
    // it (and stops submitting the passthrough composition layer) once the
    // editor scene takes over, unless configured to stay on for the session.
    void set_passthrough_active(bool active);
    [[nodiscard]] auto is_passthrough_active() const -> bool;

    // XR_META_boundary_visibility: current boundary visibility as reported by
    // the runtime via XrEventDataBoundaryVisibilityChangedMETA (dispatched in
    // Xr_instance::poll_xr_events()). end_frame() requests suppression while
    // the passthrough layer is being submitted; set_passthrough_active(false)
    // requests the boundary back.
    void set_boundary_visibility(XrBoundaryVisibilityMETA boundary_visibility);

private:
    [[nodiscard]] auto color_space_score          (const XrColorSpaceFB color_space) const -> int;
    [[nodiscard]] auto color_format_score         (const erhe::dataformat::Format pixelformat) const -> int;
    [[nodiscard]] auto depth_stencil_format_score (const erhe::dataformat::Format pixelformat) const -> int;
    [[nodiscard]] auto create_session             () -> bool;
    [[nodiscard]] auto enumerate_swapchain_formats() -> bool;
    [[nodiscard]] auto enumerate_reference_spaces () -> bool;
    [[nodiscard]] auto create_swapchains          () -> bool;
    [[nodiscard]] auto create_reference_space     () -> bool;
    [[nodiscard]] auto attach_actions             () -> bool;
    [[nodiscard]] auto create_hand_tracking       () -> bool;
    void               enable_performance_metrics ();
    // XR_META_boundary_visibility: issue a visibility request to the runtime,
    // logging only when the result changes (see m_last_boundary_visibility_request_result).
    void               request_boundary_visibility(XrBoundaryVisibilityMETA boundary_visibility);

    class Swapchains
    {
    public:
        Swapchains(Swapchain&& color, Swapchain&& depth_stencil)
            : color_swapchain        {std::move(color)}
            , depth_stencil_swapchain{std::move(depth_stencil)}
        {
        }

        Swapchain color_swapchain;
        Swapchain depth_stencil_swapchain;
    };

    class Hand_tracker
    {
    public:
        XrHandTrackerEXT         hand_tracker    {XR_NULL_HANDLE};
        XrHandJointLocationEXT   joint_locations [XR_HAND_JOINT_COUNT_EXT];
        XrHandJointVelocityEXT   joint_velocities[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointVelocitiesEXT velocities;
        XrHandJointLocationsEXT  locations;
    };

    Xr_instance&                                  m_instance;
    erhe::window::Context_window&                 m_context_window;
    erhe::graphics::Device&                       m_graphics_device;
    XrSession                                     m_xr_session{XR_NULL_HANDLE};
    bool                                          m_mirror_mode{false};
    erhe::dataformat::Format                      m_swapchain_color_format;
    erhe::dataformat::Format                      m_swapchain_depth_stencil_format;
    // Per-eye swapchains. Populated when multiview is disabled.
    std::vector<Swapchains>                       m_view_swapchains;
    // Shared multiview swapchains. Populated when m_use_multiview is
    // true. Each swapchain image is a 2D array texture with one layer
    // per view; the renderer attaches a 2D_ARRAY image view spanning
    // every layer and uses VK_KHR_multiview to write all layers in one
    // pass. m_view_swapchains is empty in this case.
    std::optional<Swapchain>                      m_shared_color_swapchain;
    std::optional<Swapchain>                      m_shared_depth_stencil_swapchain;
    bool                                          m_use_multiview{false};
    std::vector<XrView>                           m_xr_views;
    std::vector<XrCompositionLayerProjectionView> m_xr_composition_layer_projection_views;
    std::vector<XrCompositionLayerDepthInfoKHR>   m_xr_composition_layer_depth_infos;
    // Non-owning. Quad_layer registers/unregisters itself here via
    // register_quad_layer()/unregister_quad_layer(); end_frame() iterates this
    // to append XrCompositionLayerQuad entries.
    std::vector<Quad_layer*>                      m_quad_layers;
    // Set each rendered frame in render_frame_multiview(): true when the projection
    // layer submitted per-view depth to the compositor (usable depth swapchain AND
    // KHR_composition_layer_depth enabled). end_frame() gates the FB quad depth
    // test on this, since the test compares against the projection layer's depth.
    bool                                          m_projection_depth_submitted{false};
    std::vector<XrReferenceSpaceType>             m_xr_reference_space_types;
    XrSpace                                       m_xr_reference_space_local{XR_NULL_HANDLE};
    XrSpace                                       m_xr_reference_space_stage{XR_NULL_HANDLE};
    XrSpace                                       m_xr_reference_space_view {XR_NULL_HANDLE};
    XrSpaceLocation                               m_view_location;
    XrSessionState                                m_xr_session_state{XR_SESSION_STATE_UNKNOWN};
    XrFrameState                                  m_xr_frame_state;
    Hand_tracker                                  m_hand_tracker_left;
    Hand_tracker                                  m_hand_tracker_right;
    XrPassthroughFB                               m_passthrough_fb      {XR_NULL_HANDLE};
    XrPassthroughLayerFB                          m_passthrough_layer_fb{XR_NULL_HANDLE};
    // True while passthrough is created, started and not paused; gates the
    // XrCompositionLayerPassthroughFB submission in end_frame().
    bool                                          m_passthrough_running {false};
    // XR_META_boundary_visibility state. m_boundary_visibility mirrors the
    // runtime's reports (boundary visibility changed events); the last request
    // and its result are kept so the per-frame suppression retry in end_frame()
    // logs only on changes (the runtime returns the qualified success
    // XR_BOUNDARY_VISIBILITY_SUPPRESSION_NOT_ALLOWED_META until the compositor
    // has seen a passthrough layer).
    XrBoundaryVisibilityMETA                      m_boundary_visibility {XR_BOUNDARY_VISIBILITY_NOT_SUPPRESSED_META};
    XrBoundaryVisibilityMETA                      m_last_boundary_visibility_request{XR_BOUNDARY_VISIBILITY_MAX_ENUM_META};
    XrResult                                      m_last_boundary_visibility_request_result{XR_SUCCESS};
    bool                                          m_session_running     {false};
    std::vector<Xr_perf_counter>                  m_perf_counters;
    bool                                          m_perf_metrics_enabled{false};
};

} // namespace erhe::xr
