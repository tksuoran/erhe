#include "xr/hand_tracker.hpp"

#include "editor_context.hpp"
#include "editor_rendering.hpp"
#include "xr/headset_view.hpp"

#include "erhe_renderer/line_renderer.hpp"
#include "renderers/render_context.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_xr/headset.hpp"

namespace editor
{

namespace {

const std::vector<XrHandJointEXT> thumb_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_THUMB_METACARPAL_EXT,
    XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
    XR_HAND_JOINT_THUMB_DISTAL_EXT,
    XR_HAND_JOINT_THUMB_TIP_EXT
};
const std::vector<XrHandJointEXT> index_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_INDEX_METACARPAL_EXT,
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
    XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,
    XR_HAND_JOINT_INDEX_DISTAL_EXT,
    XR_HAND_JOINT_INDEX_TIP_EXT
};
const std::vector<XrHandJointEXT> middle_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
    XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_MIDDLE_DISTAL_EXT,
    XR_HAND_JOINT_MIDDLE_TIP_EXT
};
const std::vector<XrHandJointEXT> ring_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_RING_METACARPAL_EXT,
    XR_HAND_JOINT_RING_PROXIMAL_EXT,
    XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
    XR_HAND_JOINT_RING_DISTAL_EXT,
    XR_HAND_JOINT_RING_TIP_EXT
};
const std::vector<XrHandJointEXT> little_joints = {
    XR_HAND_JOINT_WRIST_EXT,
    XR_HAND_JOINT_LITTLE_METACARPAL_EXT,
    XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
    XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT,
    XR_HAND_JOINT_LITTLE_DISTAL_EXT,
    XR_HAND_JOINT_LITTLE_TIP_EXT
};

} // anonymous namespace

Hand::Hand(const XrHandEXT hand)
    : m_hand{hand}
{
    std::fill(
        m_color.begin(),
        m_color.end(),
        ImVec4{0.8f, 0.8f, 0.8f, 1.0f}
    );
}

auto to_finger(const XrHandJointEXT joint) -> size_t
{
    switch (joint) {
        case XR_HAND_JOINT_PALM_EXT               : return Finger_name::palm;
        case XR_HAND_JOINT_WRIST_EXT              : return Finger_name::wrist;
        case XR_HAND_JOINT_THUMB_METACARPAL_EXT   : return Finger_name::thumb;
        case XR_HAND_JOINT_THUMB_PROXIMAL_EXT     : return Finger_name::thumb;
        case XR_HAND_JOINT_THUMB_DISTAL_EXT       : return Finger_name::thumb;
        case XR_HAND_JOINT_THUMB_TIP_EXT          : return Finger_name::thumb;
        case XR_HAND_JOINT_INDEX_METACARPAL_EXT   : return Finger_name::index;
        case XR_HAND_JOINT_INDEX_PROXIMAL_EXT     : return Finger_name::index;
        case XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT : return Finger_name::index;
        case XR_HAND_JOINT_INDEX_DISTAL_EXT       : return Finger_name::index;
        case XR_HAND_JOINT_INDEX_TIP_EXT          : return Finger_name::index;
        case XR_HAND_JOINT_MIDDLE_METACARPAL_EXT  : return Finger_name::middle;
        case XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT    : return Finger_name::middle;
        case XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT: return Finger_name::middle;
        case XR_HAND_JOINT_MIDDLE_DISTAL_EXT      : return Finger_name::middle;
        case XR_HAND_JOINT_MIDDLE_TIP_EXT         : return Finger_name::middle;
        case XR_HAND_JOINT_RING_METACARPAL_EXT    : return Finger_name::ring;
        case XR_HAND_JOINT_RING_PROXIMAL_EXT      : return Finger_name::ring;
        case XR_HAND_JOINT_RING_INTERMEDIATE_EXT  : return Finger_name::ring;
        case XR_HAND_JOINT_RING_DISTAL_EXT        : return Finger_name::ring;
        case XR_HAND_JOINT_RING_TIP_EXT           : return Finger_name::ring;
        case XR_HAND_JOINT_LITTLE_METACARPAL_EXT  : return Finger_name::little;
        case XR_HAND_JOINT_LITTLE_PROXIMAL_EXT    : return Finger_name::little;
        case XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT: return Finger_name::little;
        case XR_HAND_JOINT_LITTLE_DISTAL_EXT      : return Finger_name::little;
        case XR_HAND_JOINT_LITTLE_TIP_EXT         : return Finger_name::little;
        default:                                    return Finger_name::palm;
    }
}

void Hand::update(erhe::xr::Headset& headset)
{
    //m_is_active = headset.get_hand_tracking_active(m_hand);
    //
    //if (!m_is_active) {
    //    return;
    //}

    int orientation_valid_count   = 0;
    int position_valid_count      = 0;
    int orientation_tracked_count = 0;
    int position_tracked_count    = 0;
    int nonzero_size_count        = 0;
    for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i) {
        m_joints[i] = headset.get_hand_tracking_joint(
            m_hand, static_cast<XrHandJointEXT>(i)
        );
        const bool  orientation_valid   = (m_joints[i].location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT  ) == XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        const bool  position_valid      = (m_joints[i].location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT     ) == XR_SPACE_LOCATION_POSITION_VALID_BIT;
        const bool  orientation_tracked = (m_joints[i].location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) == XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        const bool  position_tracked    = (m_joints[i].location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT   ) == XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        const float radius              = m_joints[i].location.radius;
        if (orientation_valid  ) ++orientation_valid_count;
        if (position_valid     ) ++position_valid_count;
        if (orientation_tracked) ++orientation_tracked_count;
        if (position_tracked   ) ++position_tracked_count;
        if (radius > 0.0f      ) ++nonzero_size_count;
    }
    m_is_active = nonzero_size_count > XR_HAND_JOINT_COUNT_EXT / 2;
}

auto Hand::get_closest_point_to_line(
    const glm::mat4 transform,
    const glm::vec3 p0,
    const glm::vec3 p1
) const -> std::optional<Finger_point>
{
    if (!m_is_active) {
        return {};
    }

    std::optional<Finger_point> result;
    float min_distance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i) {
        const auto joint = static_cast<XrHandJointEXT>(i);
        const bool valid = is_valid(joint);
        if (valid) {
            const glm::vec3 pos{
                m_joints[joint].location.pose.position.x,
                m_joints[joint].location.pose.position.y,
                m_joints[joint].location.pose.position.z
            };
            const auto p             = glm::vec3{transform * glm::vec4{pos, 1.0f}};
            const auto closest_point = erhe::math::closest_point<float>(p0, p1, p);
            if (closest_point.has_value()) {
                const auto  q        = closest_point.value();
                const float distance = glm::distance(p, q);
                if (distance < min_distance) {
                    min_distance = distance;
                    result = {
                        .finger       = to_finger(joint),
                        .finger_point = p,
                        .point        = q
                    };
                }
            }
        }
    }
    return result;
}

//// auto Hand::get_closest_point_to_plane(
////     const glm::mat4 transform,
////     const glm::vec3 point_on_plane,
////     const glm::vec3 plane_normal
//// ) const -> std::optional<Closest_finger>
//// {
////     if (!m_is_active) {
////         return {};
////     }
////
////     std::optional<Closest_finger> result;
////     float min_distance = std::numeric_limits<float>::max();
////     for (size_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i) {
////         const auto joint = static_cast<XrHandJointEXT>(i);
////         const bool valid = is_valid(joint);
////         if (valid) {
////             const glm::vec3 pos{
////                 m_joints[joint].location.pose.position.x,
////                 m_joints[joint].location.pose.position.y,
////                 m_joints[joint].location.pose.position.z
////             };
////             const auto p             = glm::vec3{transform * glm::vec4{pos, 1.0f}};
////             const auto closest_point = erhe::math::project_point_to_plane<float>(plane_normal, point_on_plane, p);
////             if (closest_point.has_value()) {
////                 const auto  q        = closest_point.value();
////                 const float distance = glm::distance(p, q);
////                 if (distance < min_distance) {
////                     min_distance = distance;
////                     result = {
////                         .finger = to_finger(joint),
////                         .closest_points = {
////                             .P = p,
////                             .Q = q
////                         }
////                     };
////                 }
////             }
////         }
////     }
////     return result;
//// }

//// auto Hand::get_closest_point_to_plane(
////     const XrHandJointEXT joint,
////     const glm::mat4      transform,
////     const glm::vec3      point_on_plane,
////     const glm::vec3      plane_normal
//// ) const -> std::optional<Closest_finger>
//// {
////     const bool valid = is_valid(joint);
////     if (valid) {
////         const glm::vec3 pos{
////             m_joints[joint].location.pose.position.x,
////             m_joints[joint].location.pose.position.y,
////             m_joints[joint].location.pose.position.z
////         };
////         const auto p             = glm::vec3{transform * glm::vec4{pos, 1.0f}};
////         const auto closest_point = erhe::math::project_point_to_plane<float>(plane_normal, point_on_plane, p);
////         if (closest_point.has_value()) {
////             const auto q = closest_point.value();
////             return Closest_finger{
////                 .finger = to_finger(joint),
////                 .closest_points = {
////                     .P = p,
////                     .Q = q
////                 }
////             };
////         }
////     }
////     return {};
//// }

auto Hand::get_joint(const XrHandJointEXT joint) const -> std::optional<Joint>
{
    const bool valid = is_valid(joint);
    if (valid) {
#ifdef GLM_FORCE_QUAT_DATA_XYZW
        glm::quat q{
            m_joints[joint].location.pose.orientation.x,
            m_joints[joint].location.pose.orientation.y,
            m_joints[joint].location.pose.orientation.z,
            m_joints[joint].location.pose.orientation.w
        };
#else
        glm::quat q{
            m_joints[joint].location.pose.orientation.w,
            m_joints[joint].location.pose.orientation.x,
            m_joints[joint].location.pose.orientation.y,
            m_joints[joint].location.pose.orientation.z
        };
#endif
        return Joint{
            .position = glm::vec3{
                m_joints[joint].location.pose.position.x,
                m_joints[joint].location.pose.position.y,
                m_joints[joint].location.pose.position.z
            },
            .orientation = glm::mat4{q}
        };
    }
    return {};
}

auto Hand::distance(
    const XrHandJointEXT lhs,
    const XrHandJointEXT rhs
) const -> std::optional<float>
{
    const auto lhs_joint = static_cast<XrHandJointEXT>(lhs);
    const auto rhs_joint = static_cast<XrHandJointEXT>(rhs);
    const bool lhs_valid = is_valid(lhs_joint);
    const bool rhs_valid = is_valid(rhs_joint);
    if (!lhs_valid || !rhs_valid) {
        return {};
    }
    const glm::vec3 lhs_pos{
        m_joints[lhs_joint].location.pose.position.x,
        m_joints[lhs_joint].location.pose.position.y,
        m_joints[lhs_joint].location.pose.position.z
    };
    const glm::vec3 rhs_pos{
        m_joints[rhs_joint].location.pose.position.x,
        m_joints[rhs_joint].location.pose.position.y,
        m_joints[rhs_joint].location.pose.position.z
    };
    return glm::distance(lhs_pos, rhs_pos);
}

auto Hand::is_active() const -> bool
{
    return m_is_active;
}

auto Hand::is_valid(const XrHandJointEXT joint) const -> bool
{
    if (!m_is_active) {
        return false;
    }

    if (joint >= m_joints.size()) {
        return false;
    }

    // TODO
    //return
    //    (m_joints[joint].location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT;

    return m_joints[joint].location.radius > 0.0f;
}

void Hand::set_color(const std::size_t finger, const ImVec4 color)
{
    m_color[finger] = color;
}

void Hand::draw(
    erhe::renderer::Line_renderer& line_renderer,
    const glm::mat4                transform
)
{
    if (!m_is_active) {
        return;
    }

    line_renderer.set_line_color(m_color[Finger_name::thumb]);
    draw_joint_line_strip(transform, thumb_joints,  line_renderer);

    line_renderer.set_line_color(m_color[Finger_name::index]);
    draw_joint_line_strip(transform, index_joints,  line_renderer);

    line_renderer.set_line_color(m_color[Finger_name::middle]);
    draw_joint_line_strip(transform, middle_joints, line_renderer);

    line_renderer.set_line_color(m_color[Finger_name::ring]);
    draw_joint_line_strip(transform, ring_joints,   line_renderer);

    line_renderer.set_line_color(m_color[Finger_name::little]);
    draw_joint_line_strip(transform, little_joints, line_renderer);

    // constexpr uint32_t red   = 0xff0000ffu;
    // constexpr uint32_t green = 0xff00ff00u;
    // constexpr uint32_t blue  = 0xffff0000u;
}

void Hand::draw_joint_line_strip(
    const glm::mat4                    transform,
    const std::vector<XrHandJointEXT>& joint_names,
    erhe::renderer::Line_renderer&     line_renderer
) const
{
    ERHE_PROFILE_FUNCTION();

    if (joint_names.size() < 2) {
        return;
    }

    for (size_t i = 1; i < joint_names.size(); ++i) {
        const std::size_t joint_id_a = static_cast<std::size_t>(joint_names[i - 1]);
        const std::size_t joint_id_b = static_cast<std::size_t>(joint_names[i    ]);
        if (joint_id_a >= m_joints.size()) {
            continue;
        }
        if (joint_id_b >= m_joints.size()) {
            continue;
        }
        const auto& joint_a = m_joints[joint_id_a];
        const auto& joint_b = m_joints[joint_id_b];
        if ((joint_a.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            continue;
        }
        if ((joint_b.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            continue;
        }
        line_renderer.add_lines(
            transform,
            {
                erhe::renderer::Line4{
                    glm::vec4{
                        joint_a.location.pose.position.x,
                        joint_a.location.pose.position.y,
                        joint_a.location.pose.position.z,
                        joint_a.location.radius * 256.0f
                    },
                    glm::vec4{
                        joint_b.location.pose.position.x,
                        joint_b.location.pose.position.y,
                        joint_b.location.pose.position.z,
                        joint_b.location.radius * 256.0f
                    }
                }
            }
        );
    }
}

Hand_tracker::Hand_tracker(
    Editor_context&   editor_context,
    Editor_rendering& editor_rendering
)
    : m_context   {editor_context}
    , m_left_hand {XR_HAND_LEFT_EXT}
    , m_right_hand{XR_HAND_RIGHT_EXT}
{
    editor_rendering.add(this);
    //set_flags      (Tool_flags::background);
    //set_description("Hand_tracker");
}

Hand_tracker::~Hand_tracker() noexcept
{
    m_context.editor_rendering->remove(this);
}

void Hand_tracker::update_hands(erhe::xr::Headset& headset)
{
    m_left_hand.update(headset);
    m_right_hand.update(headset);
}

auto Hand_tracker::get_hand(const Hand_name hand_name) -> Hand&
{
    switch (hand_name) {
        //using enum Hand_name;
        case Hand_name::Left:  return m_left_hand;
        case Hand_name::Right: return m_right_hand;
        default: ERHE_FATAL("bad hand name %x", static_cast<unsigned int>(hand_name));
    }
}

void Hand_tracker::set_color(const Hand_name hand_name, const ImVec4 color)
{
    get_hand(hand_name).set_color(Finger_name::thumb,  color);
    get_hand(hand_name).set_color(Finger_name::index,  color);
    get_hand(hand_name).set_color(Finger_name::middle, color);
    get_hand(hand_name).set_color(Finger_name::ring,   color);
    get_hand(hand_name).set_color(Finger_name::little, color);
}

void Hand_tracker::set_color(
    const Hand_name   hand_name,
    const std::size_t finger_name,
    const ImVec4      color
)
{
    get_hand(hand_name).set_color(finger_name, color);
}

void Hand_tracker::render(const Render_context&)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_show_hands) {
        return;
    }

    const auto root_node = m_context.headset_view->get_root_node();
    if (!root_node) {
        return;
    }

    const auto transform     = root_node->world_from_node();
    auto&      line_renderer = *m_context.line_renderer_set->hidden.at(3).get();

    m_left_hand .draw(line_renderer, transform);
    m_right_hand.draw(line_renderer, transform);
}

} // namespace editor

