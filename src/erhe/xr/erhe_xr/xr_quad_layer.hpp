#pragma once

#include "erhe_xr/xr_swapchain_image.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>

namespace erhe::graphics { class Texture; }

namespace erhe::xr {

class Xr_session;

// A single OpenXR quad composition layer (XrCompositionLayerQuad) backed by its
// own color swapchain. The editor renders 2D UI content (e.g. Hud / Hotbar)
// directly into the acquired swapchain image each frame and the runtime then
// composites the quad at native resolution.
//
// Created via Xr_session::create_quad_layer(); the instance registers itself
// with the owning session so Xr_session::end_frame() can enumerate and submit
// every visible, rendered quad layer after the projection layer.
//
// Per-frame usage:
//   erhe::graphics::Texture* texture = quad_layer->acquire(); // acquire + wait
//   ... render UI into texture ...
//   quad_layer->release();                                    // release + mark rendered
// build_layer() then succeeds for this frame.
class Quad_layer
{
public:
    Quad_layer(
        Xr_session&  session,
        Swapchain&&  swapchain,
        uint32_t     width,
        uint32_t     height
    );
    ~Quad_layer() noexcept;
    Quad_layer    (const Quad_layer&) = delete;
    void operator=(const Quad_layer&) = delete;
    Quad_layer    (Quad_layer&&)      = delete;
    void operator=(Quad_layer&&)      = delete;

    [[nodiscard]] auto is_valid () const -> bool;
    [[nodiscard]] auto get_width () const -> uint32_t;
    [[nodiscard]] auto get_height() const -> uint32_t;

    // Acquire + wait the next swapchain image and return the erhe texture to
    // render into (nullptr if not visible / invalid / acquire failed). The
    // acquired image is held until release() is called.
    [[nodiscard]] auto acquire() -> erhe::graphics::Texture*;
    // Release the acquired image and mark the layer rendered for this frame.
    void release();

    void set_visible       (bool visible);
    void set_pose          (const XrPosef& pose_in_stage);
    void set_size          (const XrExtent2Df& size_meters);
    void set_eye_visibility(XrEyeVisibility eye_visibility);
    // When double-sided, Xr_session::end_frame() submits a second, back-facing
    // quad (see build_back_layer()) so the layer is visible from behind too.
    void set_double_sided  (bool double_sided);
    // When depth-tested, Xr_session::end_frame() chains an
    // XrCompositionLayerDepthTestFB so the projection layer's depth can occlude
    // this quad. Requires runtime support and a submitted projection depth.
    void set_depth_tested  (bool depth_tested);
    // Composition order: quad layers are submitted in ascending order, so a higher
    // value composites later (on top). Used to keep the Hud in front of the Hotbar.
    void set_composition_order(int order);

    [[nodiscard]] auto is_visible        () const -> bool;
    [[nodiscard]] auto is_double_sided   () const -> bool;
    [[nodiscard]] auto is_depth_tested   () const -> bool;
    [[nodiscard]] auto get_composition_order() const -> int;

    // Fill a caller-owned XrCompositionLayerQuad for submission in
    // Xr_session::end_frame(). Returns false (leaving out untouched) when the
    // layer is not submittable this frame (invalid, hidden, or not rendered).
    [[nodiscard]] auto build_layer(XrSpace stage_space, XrCompositionLayerQuad& out) const -> bool;

    // Like build_layer(), but for the back face of a double-sided quad: the pose
    // is rotated 180 degrees about the quad's local up (Y) axis so the quad
    // faces the opposite direction, reusing the same swapchain image. Because a
    // rigid pose cannot reflect, this rotation also swaps left/right in world
    // space: the back face stays readable, but its pixels are not at the same
    // world positions as the front, so this is not a true two-sided surface. A
    // true two-sided surface -- same pixels at the same world position, mirrored
    // when viewed from behind -- would need a horizontally-flipped image copy,
    // which is not done here. Returns false when not submittable this frame.
    [[nodiscard]] auto build_back_layer(XrSpace stage_space, XrCompositionLayerQuad& out) const -> bool;

    // Clear the per-frame "rendered" flag. Xr_session::end_frame() calls this
    // for every registered layer once per frame (regardless of whether the
    // frame was rendered), so a deferred/non-rendered frame cannot submit a
    // stale image on a later frame.
    void reset_frame_state();

private:
    Xr_session&                    m_session;
    Swapchain                      m_swapchain;
    std::optional<Swapchain_image> m_acquired_image;
    uint32_t                       m_width               {0};
    uint32_t                       m_height              {0};
    bool                           m_visible             {false};
    bool                           m_rendered_this_frame {false};
    XrEyeVisibility                m_eye_visibility      {XR_EYE_VISIBILITY_BOTH};
    XrPosef                        m_pose                {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
    XrExtent2Df                    m_size                {1.0f, 1.0f};
    bool                           m_double_sided        {false};
    bool                           m_depth_tested        {false};
    int                            m_composition_order   {0};
};

} // namespace erhe::xr
