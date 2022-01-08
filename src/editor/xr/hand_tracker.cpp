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
    return
        (m_joints[joint].location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == XR_SPACE_LOCATION_POSITION_VALID_BIT;
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
        case Hand_name::Left: return m_left_hand;
        case Hand_name::Right: return m_right_hand;
        default: ERHE_FATAL("bad hand name %x", static_cast<unsigned int>(hand_name));
    }
}

void Hand_tracker::tool_render(const Render_context& context)
{
    static_cast<void>(context);

    if (!m_show_hands)
    {
        return;
    }

    const auto camera        = m_headset_renderer->root_camera();
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
    ImGui::Checkbox  ("Show Hands", &m_show_hands);
    ImGui::ColorEdit4("Left Hand",  &m_left_hand_color.x, ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4("Right Hand", &m_right_hand_color.x, ImGuiColorEditFlags_Float);
}

#if 0
        {
            ERHE_PROFILE_SCOPE("headset lines");

            auto* theremin_tool = get<Theremin_tool>();
            auto& line_renderer = context.line_renderer->hidden;
            constexpr uint32_t red       = 0xff0000ffu;
            constexpr uint32_t green     = 0xff00ff00u;
            constexpr uint32_t blue      = 0xffff0000u;
            constexpr float    thickness = 70.0f;

            auto* controller_node      = m_controller_visualization->get_node();
            auto  controller_position  = controller_node->position_in_world();
            auto  controller_direction = controller_node->direction_in_world();
            auto  end_position         = controller_position - m_headset->trigger_value() * 2.0f * controller_direction;
            const glm::vec3 origo {0.0f, 0.0f, 0.0f};
            const glm::vec3 unit_x{1.0f, 0.0f, 0.0f};
            const glm::vec3 unit_y{0.0f, 1.0f, 0.0f};
            const glm::vec3 unit_z{0.0f, 0.0f, 1.0f};
            line_renderer.set_line_color(red);
            line_renderer.add_lines({{origo, unit_x}}, thickness);
            line_renderer.set_line_color(green);
            line_renderer.add_lines({{origo, unit_y}}, thickness);
            line_renderer.set_line_color(blue);
            line_renderer.add_lines({{origo, unit_z}}, thickness);
            line_renderer.set_line_color(green);
            line_renderer.add_lines({{controller_position, end_position }}, thickness);

            auto*      view_camera  = get<Fly_camera_tool>()->camera();
            const auto transform    = view_camera->world_from_node();
            auto*      log_window   = get<Log_window>();
            const bool left_active  = m_headset->get_hand_tracking_active(XR_HAND_LEFT_EXT);
            const bool right_active = m_headset->get_hand_tracking_active(XR_HAND_RIGHT_EXT);
            log_window->frame_log("Hand tracking left - {}", left_active ? "active" : "inactive");

            std::optional<erhe::toolkit::Closest_points<float>> left_closest_point;
            std::optional<erhe::toolkit::Closest_points<float>> right_closest_point;
            if (left_active)
            {
                Hand_visualization left_hand;
                const glm::vec3 p0{-0.15f, 0.0f, 0.0f};
                const glm::vec3 p1{-0.15f, 2.0f, 0.0f};
                left_hand.update(*m_headset.get(), XR_HAND_LEFT_EXT);
                left_closest_point = left_hand.get_closest_point_to_line(transform, p0, p1);
                line_renderer.set_line_color(0xff8888ff);
                line_renderer.add_lines( { { p0, p1 } }, thickness );
                if (left_closest_point.has_value())
                {
                    const auto  P = left_closest_point.value().P;
                    const auto  Q = left_closest_point.value().Q;
                    const float d = glm::distance(P, Q);
                    const float s = 0.085f / d;
                    line_renderer.set_line_color(viridis.get(s));
                    line_renderer.add_lines( { { P, Q } }, thickness );
                    if (theremin_tool != nullptr)
                    {
                        theremin_tool->set_left_distance(d);
                    }
                }

                line_renderer.set_line_color(0xff0088ff);
                left_hand.draw_joint_line_strip(transform, thumb_joints,  line_renderer);
                left_hand.draw_joint_line_strip(transform, index_joints,  line_renderer);
                left_hand.draw_joint_line_strip(transform, middle_joints, line_renderer);
                left_hand.draw_joint_line_strip(transform, ring_joints,   line_renderer);
                left_hand.draw_joint_line_strip(transform, little_joints, line_renderer);
            }

            log_window->frame_log("Hand tracking right - {}", right_active ? "active" : "inactive");
            if (right_active)
            {
                Hand_visualization right_hand;
                const glm::vec3 p0{0.15f, 0.0f, 0.0f};
                const glm::vec3 p1{0.15f, 2.0f, 0.0f};
                right_hand.update(*m_headset.get(), XR_HAND_RIGHT_EXT);
                right_closest_point = right_hand.get_closest_point_to_line(transform, p0, p1);
                line_renderer.set_line_color(0xffff8888);
                line_renderer.add_lines( { { p0, p1 } }, thickness );
                if (right_closest_point.has_value())
                {
                    const auto  P = right_closest_point.value().P;
                    const auto  Q = right_closest_point.value().Q;
                    const float d = glm::distance(P, Q);
                    const float s = 0.085f / d;
                    line_renderer.set_line_color(temperature.get(s));
                    line_renderer.add_lines( { { P, Q } }, thickness );
                    if (theremin_tool != nullptr)
                    {
                        theremin_tool->set_right_distance(d);
                    }
                }

                line_renderer.set_line_color(0xffff8800);
                right_hand.draw_joint_line_strip(transform, thumb_joints,  line_renderer);
                right_hand.draw_joint_line_strip(transform, index_joints,  line_renderer);
                right_hand.draw_joint_line_strip(transform, middle_joints, line_renderer);
                right_hand.draw_joint_line_strip(transform, ring_joints,   line_renderer);
                right_hand.draw_joint_line_strip(transform, little_joints, line_renderer);
            }
        }
#endif

} // namespace editor

