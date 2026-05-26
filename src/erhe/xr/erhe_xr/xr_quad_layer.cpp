#include "erhe_xr/xr_quad_layer.hpp"
#include "erhe_xr/xr_session.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr.hpp"

#include "erhe_profile/profile.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <utility>

namespace erhe::xr {

Quad_layer::Quad_layer(Xr_session& session, Swapchain&& swapchain, uint32_t width, uint32_t height)
    : m_session  {session}
    , m_swapchain{std::move(swapchain)}
    , m_width    {width}
    , m_height   {height}
{
    m_session.register_quad_layer(this);
}

Quad_layer::~Quad_layer() noexcept
{
    m_session.unregister_quad_layer(this);
}

auto Quad_layer::is_valid() const -> bool
{
    return m_swapchain.get_xr_swapchain() != XR_NULL_HANDLE;
}

auto Quad_layer::get_width() const -> uint32_t
{
    return m_width;
}

auto Quad_layer::get_height() const -> uint32_t
{
    return m_height;
}

auto Quad_layer::acquire() -> erhe::graphics::Texture*
{
    ERHE_PROFILE_FUNCTION();

    if (!is_valid() || !m_visible) {
        return nullptr;
    }

    if (m_acquired_image.has_value()) {
        // acquire() called twice without an intervening release(); reuse the
        // already-acquired image rather than acquiring a second one.
        log_xr->warn("Quad_layer::acquire() called while an image is already acquired");
        return m_acquired_image->get_texture();
    }

    std::optional<Swapchain_image> image = m_swapchain.acquire();
    if (!image.has_value() || !m_swapchain.wait()) {
        log_xr->warn("Quad_layer::acquire(): failed to acquire/wait swapchain image");
        return nullptr;
    }

    m_acquired_image = std::move(image);
    return m_acquired_image->get_texture();
}

void Quad_layer::release()
{
    if (!m_acquired_image.has_value()) {
        return;
    }
    // Swapchain_image's destructor calls Swapchain::release().
    m_acquired_image.reset();
    m_rendered_this_frame = true;
}

void Quad_layer::set_visible(bool visible)
{
    m_visible = visible;
}

void Quad_layer::set_pose(const XrPosef& pose_in_stage)
{
    m_pose = pose_in_stage;
}

void Quad_layer::set_size(const XrExtent2Df& size_meters)
{
    m_size = size_meters;
}

void Quad_layer::set_eye_visibility(XrEyeVisibility eye_visibility)
{
    m_eye_visibility = eye_visibility;
}

void Quad_layer::set_double_sided(bool double_sided)
{
    m_double_sided = double_sided;
}

void Quad_layer::set_depth_tested(bool depth_tested)
{
    m_depth_tested = depth_tested;
}

void Quad_layer::set_composition_order(int order)
{
    m_composition_order = order;
}

auto Quad_layer::is_visible() const -> bool
{
    return m_visible;
}

auto Quad_layer::is_double_sided() const -> bool
{
    return m_double_sided;
}

auto Quad_layer::is_depth_tested() const -> bool
{
    return m_depth_tested;
}

auto Quad_layer::get_composition_order() const -> int
{
    return m_composition_order;
}

auto Quad_layer::build_layer(XrSpace stage_space, XrCompositionLayerQuad& out) const -> bool
{
    if (!is_valid() || !m_visible || !m_rendered_this_frame) {
        return false;
    }

    out = XrCompositionLayerQuad{
        .type          = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .next          = nullptr,
        .layerFlags    = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                         XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
        .space         = stage_space,
        .eyeVisibility = m_eye_visibility,
        .subImage      = XrSwapchainSubImage{
            .swapchain = m_swapchain.get_xr_swapchain(),
            .imageRect = XrRect2Di{
                .offset = XrOffset2Di{0, 0},
                .extent = XrExtent2Di{static_cast<int32_t>(m_width), static_cast<int32_t>(m_height)}
            },
            .imageArrayIndex = 0
        },
        .pose          = m_pose,
        .size          = m_size
    };

    return true;
}

auto Quad_layer::build_back_layer(XrSpace stage_space, XrCompositionLayerQuad& out) const -> bool
{
    // Start from the front-facing quad (shares the same swapchain image), then
    // rotate the pose 180 degrees about the quad's local up (Y) axis so it faces
    // the opposite direction. Right-multiplying applies the rotation in the
    // quad's local frame, so the position and up vector are unchanged and only
    // the facing flips. Note this rigid rotation also swaps left/right in world
    // space, so the back face is readable but its pixels are horizontally
    // flipped in world space relative to the front (a rigid pose cannot reflect,
    // so a true same-world-pixel mirror would need a flipped image copy).
    if (!build_layer(stage_space, out)) {
        return false;
    }
    const glm::quat front_orientation = to_glm(out.pose.orientation);
    const glm::quat flip_about_up      = glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat back_orientation   = glm::normalize(front_orientation * flip_about_up);
    out.pose.orientation = from_glm(back_orientation);
    return true;
}

void Quad_layer::reset_frame_state()
{
    m_rendered_this_frame = false;
}

} // namespace erhe::xr
