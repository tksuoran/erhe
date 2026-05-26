#pragma once

#include <openxr/openxr.h>

#include "erhe_dataformat/dataformat.hpp"

#include <spdlog/spdlog.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace erhe::graphics { class Texture; }

namespace erhe::xr {

class Pose
{
public:
    glm::quat orientation{};
    glm::vec3 position{};
};

class Render_view
{
public:
    uint32_t                 slot;
    Pose                     view_pose;
    float                    fov_left;
    float                    fov_right;
    float                    fov_up;
    float                    fov_down;
    // Non-owning. Points into the per-view Swapchain's pre-built texture
    // wrappers; valid for the lifetime of the Xr_session.
    erhe::graphics::Texture* color_texture        {nullptr};
    erhe::graphics::Texture* depth_stencil_texture{nullptr};
    // Fragment density map for fixed foveated rendering, or nullptr when the
    // swapchain was not created with foveation. Non-owning, same lifetime as
    // color_texture (the acquired swapchain image).
    erhe::graphics::Texture* fragment_density_map_texture{nullptr};
    erhe::dataformat::Format color_format;
    erhe::dataformat::Format depth_stencil_format;
    uint32_t                 width;
    uint32_t                 height;
    bool                     composition_alpha;
    float                    near_depth;
    float                    far_depth;
};

inline auto to_glm(const XrQuaternionf& q) -> glm::quat
{
    return glm::quat{q.w, q.x, q.y, q.z};
    //return glm::make_quat(&q.x);
}

inline auto to_glm(const XrVector3f& v) -> glm::vec3
{
    return glm::make_vec3(&v.x);
}

inline auto to_glm(const XrPosef& p) -> glm::mat4
{
    glm::mat4 orientation = glm::mat4_cast(to_glm(p.orientation));
    glm::mat4 translation = glm::translate(glm::mat4{1}, to_glm(p.position));
    return translation * orientation;
}

inline auto from_glm(const glm::quat& q) -> XrQuaternionf
{
    return XrQuaternionf{q.x, q.y, q.z, q.w};
}

inline auto from_glm(const glm::vec3& v) -> XrVector3f
{
    return XrVector3f{v.x, v.y, v.z};
}

// Decompose a rigid (translation + rotation, no shear) transform into an
// XrPosef. Any scale present in the upper-left 3x3 is dropped; glm::quat_cast
// normalizes, so uniformly scaled rotations still produce a valid orientation.
inline auto from_glm(const glm::mat4& m) -> XrPosef
{
    const glm::quat orientation = glm::normalize(glm::quat_cast(glm::mat3{m}));
    const glm::vec3 position    = glm::vec3{m[3]};
    return XrPosef{from_glm(orientation), from_glm(position)};
}

auto to_string_message_severity(XrDebugUtilsMessageSeverityFlagsEXT severity_flags) -> std::string;
auto to_string_message_type    (XrDebugUtilsMessageTypeFlagsEXT     type_flags)     -> std::string;

auto check(XrResult result, spdlog::level::level_enum log_level = spdlog::level::level_enum::err) -> bool;

#define ERHE_XR_CHECK(xr_result) if (!check(xr_result)) { return false; }

void check_gl_context_in_current_in_this_thread();

class Hand_tracking_joint
{
public:
    XrHandJointLocationEXT location;
    XrHandJointVelocityEXT velocity;
};

extern const char* c_str(::XrActionType            e) noexcept;
extern const char* c_str(::XrEnvironmentBlendMode  e) noexcept;
extern const char* c_str(::XrEyeVisibility         e) noexcept;
extern const char* c_str(::XrFormFactor            e) noexcept;
extern const char* c_str(::XrObjectType            e) noexcept;
extern const char* c_str(::XrResult                e) noexcept;
extern const char* c_str(::XrReferenceSpaceType    e) noexcept;
extern const char* c_str(::XrSessionState          e) noexcept;
extern const char* c_str(::XrStructureType         e) noexcept;
extern const char* c_str(::XrViewConfigurationType e) noexcept;
extern const char* c_str(::XrHandJointEXT          e) noexcept;
extern const char* c_str(::XrColorSpaceFB          e) noexcept;

}
