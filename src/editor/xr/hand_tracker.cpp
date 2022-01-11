#include "xr/hand_tracker.hpp"
#include "editor_tools.hpp"

#include "renderers/line_renderer.hpp"
#include "xr/headset_renderer.hpp"

#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/xr/headset.hpp"

#include <imgui.h>

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
}

void Hand::update(erhe::xr::Headset& headset)
{
    //m_is_active = headset.get_hand_tracking_active(m_hand);
    //
    //if (!m_is_active)
    //{
    //    return;
    //}

    int orientation_valid_count   = 0;
    int position_valid_count      = 0;
    int orientation_tracked_count = 0;
    int position_tracked_count    = 0;
    int nonzero_size_count        = 0;
    for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i)
    {
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
) const -> std::optional<erhe::toolkit::Closest_points<float>>
{
    if (!m_is_active)
    {
        return {};
    }

    std::optional<erhe::toolkit::Closest_points<float>> result;
    float min_distance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i)
    {
        const auto joint = static_cast<XrHandJointEXT>(i);
        const bool valid = is_valid(joint);
        if (valid)
        {
            const glm::vec3 pos{
                m_joints[joint].location.pose.position.x,
                m_joints[joint].location.pose.position.y,
                m_joints[joint].location.pose.position.z
            };
            const auto p             = glm::vec3{transform * glm::vec4{pos, 1.0f}};
            const auto closest_point = erhe::toolkit::closest_point<float>(p0, p1, p);
            if (closest_point.has_value())
            {
                const auto  q        = closest_point.value();
                const float distance = glm::distance(p, q);
                if (distance < min_distance)
                {
                    min_distance = distance;
                    result = { p, q };
                }
            }
        }
    }
    return result;
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
    if (!lhs_valid || !rhs_valid)
    {
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
    if (!m_is_active)
    {
        return false;
    }

    if (joint >= m_joints.size())
    {
        return false;
    }

    // TODO
    //return
    //    (m_joints[joint].location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT;

    return m_joints[joint].location.radius > 0.0f;
}

void Hand::draw(Line_renderer& line_renderer, const glm::mat4 transform)
{
    if (!m_is_active)
    {
        return;
    }

    draw_joint_line_strip(transform, thumb_joints,  line_renderer);
    draw_joint_line_strip(transform, index_joints,  line_renderer);
    draw_joint_line_strip(transform, middle_joints, line_renderer);
    draw_joint_line_strip(transform, ring_joints,   line_renderer);
    draw_joint_line_strip(transform, little_joints, line_renderer);
}

void Hand::draw_joint_line_strip(
    const glm::mat4                    transform,
    const std::vector<XrHandJointEXT>& joint_names,
    Line_renderer&                     line_renderer
) const
{
    ERHE_PROFILE_FUNCTION

    if (joint_names.size() < 2)
    {
        return;
    }

    for (size_t i = 1; i < joint_names.size(); ++i)
    {
        const size_t joint_id_a = static_cast<size_t>(joint_names[i - 1]);
        const size_t joint_id_b = static_cast<size_t>(joint_names[i]);
        if (joint_id_a >= m_joints.size())
        {
            continue;
        }
        if (joint_id_b >= m_joints.size())
        {
            continue;
        }
        const auto& joint_a = m_joints[joint_id_a];
        const auto& joint_b = m_joints[joint_id_b];
        if ((joint_a.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT)
        {
            continue;
        }
        if ((joint_b.location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != XR_SPACE_LOCATION_POSITION_VALID_BIT)
        {
            continue;
        }
        line_renderer.add_lines(
            transform,
            {
                {
                    glm::vec3{joint_a.location.pose.position.x, joint_a.location.pose.position.y, joint_a.location.pose.position.z},
                    glm::vec3{joint_b.location.pose.position.x, joint_b.location.pose.position.y, joint_b.location.pose.position.z}
                }
            },
            10.0f
        );
    }
}

Hand_tracker::Hand_tracker()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_description}
    , m_left_hand                {XR_HAND_LEFT_EXT}
    , m_right_hand               {XR_HAND_RIGHT_EXT}
{
}

auto Hand_tracker::description() -> const char*
{
    return c_description.data();
}

void Hand_tracker::connect()
{
    m_headset_renderer  = get<Headset_renderer >();
    m_line_renderer_set = get<Line_renderer_set>();
    require<Editor_tools>();
}

void Hand_tracker::initialize_component()
{
    get<Editor_tools>()->register_background_tool(this);
}

void Hand_tracker::update(erhe::xr::Headset& headset)
{
    m_left_hand.update(headset);
    m_right_hand.update(headset);
}

auto Hand_tracker::get_hand(const Hand_name hand_name) -> Hand&
{
    switch (hand_name)
    {
        using enum Hand_name;
        case Left: return m_left_hand;
        case Right: return m_right_hand;
        default: ERHE_FATAL("bad hand name %x", static_cast<unsigned int>(hand_name));
    }
}

void Hand_tracker::set_left_hand_color(const uint32_t color)
{
    m_left_hand_color = ImGui::ColorConvertU32ToFloat4(color);
}

void Hand_tracker::set_right_hand_color(const uint32_t color)
{
    m_right_hand_color = ImGui::ColorConvertU32ToFloat4(color);
}

void Hand_tracker::tool_render(const Render_context& context)
{
    static_cast<void>(context);

    if (!m_show_hands)
    {
        return;
    }

    const auto camera = m_headset_renderer->root_camera();
    if (!camera)
    {
        return;
    }

    const auto transform     = camera->world_from_node();
    auto&      line_renderer = m_line_renderer_set->hidden;

    // 0xff0088ff
    line_renderer.set_line_color(ImGui::ColorConvertFloat4ToU32(m_left_hand_color));
    m_left_hand .draw(line_renderer, transform);

    // 0xff88ff00
    line_renderer.set_line_color(ImGui::ColorConvertFloat4ToU32(m_right_hand_color));
    m_right_hand.draw(line_renderer, transform);
}

void Hand_tracker::imgui()
{
    ImGui::Checkbox("Show Hands", &m_show_hands);
    ImGui::ColorEdit4("Left Hand",  &m_left_hand_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4("Right Hand", &m_right_hand_color.x, ImGuiColorEditFlags_Float);
}

} // namespace editor

