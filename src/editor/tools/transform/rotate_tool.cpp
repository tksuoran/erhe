#include "tools/transform/rotate_tool.hpp"

#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using namespace glm;

Rotate_tool* g_rotate_tool{nullptr};

Rotate_tool::Rotate_tool()
    : erhe::components::Component{c_type_name}
{
}

Rotate_tool::~Rotate_tool() noexcept
{
    ERHE_VERIFY(g_rotate_tool == nullptr);
}

void Rotate_tool::deinitialize_component()
{
    ERHE_VERIFY(g_rotate_tool == this);
    g_rotate_tool = nullptr;
}

void Rotate_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Rotate_tool::initialize_component()
{
    ERHE_VERIFY(g_rotate_tool == nullptr);
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.rotate);
    g_tools->register_tool(this);
    g_rotate_tool = this;
}

void Rotate_tool::handle_priority_update(
    const int old_priority,
    const int new_priority
)
{
    auto& shared = get_shared();
    shared.settings.show_rotate = new_priority > old_priority;
}

void Rotate_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto& shared = get_shared();

    ImGui::Checkbox("Rotate Snap Enable", &shared.settings.rotate_snap_enable);
    const float rotate_snap_values[] = {  5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
    const char* rotate_snap_items [] = { "5",  "10",  "15",  "20",  "30",  "45",  "60",  "90" };
    erhe::application::make_combo(
        "Rotate Snap",
        m_rotate_snap_index,
        rotate_snap_items,
        IM_ARRAYSIZE(rotate_snap_items)
    );
    if (
        (m_rotate_snap_index >= 0) &&
        (m_rotate_snap_index < IM_ARRAYSIZE(rotate_snap_values))
    ) {
        shared.settings.rotate_snap = rotate_snap_values[m_rotate_snap_index];
    }
#endif
}

auto Rotate_tool::begin(
    unsigned int axis_mask,
    Scene_view*  scene_view
) -> bool
{
    m_axis_mask = axis_mask;
    m_active    = true;

    //if (is_rotate_active()) {
    auto& shared = get_shared();
    const bool world        = !shared.settings.local;
    const vec3 n            = get_plane_normal(world);
    const vec3 side         = get_plane_side  (world);
    const vec3 center       = shared.world_from_anchor_initial_state.get_translation();
    const auto intersection = project_pointer_to_plane(scene_view, n, center);

    if (!intersection.has_value()) {
        log_trs_tool->trace("drag not possible - no intersection");
        return false;
    }

    m_normal               = n;
    m_reference_direction  = normalize(intersection.value() - center);
    m_center_of_rotation   = center;
    m_start_rotation_angle = erhe::toolkit::angle_of_rotation<float>(m_reference_direction, n, side);

    return true;
}

auto Rotate_tool::snap(
    const float angle_radians
) const -> float
{
    auto& shared = get_shared();
    if (!shared.settings.rotate_snap_enable) {
        return angle_radians;
    }

    const float snap = glm::radians<float>(shared.settings.rotate_snap);
    return std::floor((angle_radians + snap * 0.5f) / snap) * snap;
}

auto Rotate_tool::update(Scene_view* scene_view) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (scene_view == nullptr) {
        return false;
    }

    bool ready_to_rotate = update_circle_around(scene_view);
    if (!ready_to_rotate) {
        ready_to_rotate = update_parallel(scene_view);
    }

    if (ready_to_rotate) {
        update_final();
    }

    return ready_to_rotate;
}

auto Rotate_tool::update_circle_around(Scene_view* scene_view) -> bool
{
    m_intersection = project_pointer_to_plane(
        scene_view,
        m_normal,
        m_center_of_rotation
    );
    return m_intersection.has_value();
}

auto Rotate_tool::update_parallel(Scene_view* scene_view) -> bool
{
    const auto p_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto p_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!p_origin_opt.has_value() || !p_direction_opt.has_value()) {
        return false;
    }

    const auto& shared = get_shared();
    const auto p0        = p_origin_opt.value();
    const auto direction = p_direction_opt.value();
    const auto q0        = p0 + shared.initial_drag_distance * direction;

    m_intersection = project_to_offset_plane(m_center_of_rotation, q0);
    return true;
}

void Rotate_tool::update_final()
{
    Expects(m_intersection.has_value());

    const vec3  q_                     = normalize                              (m_intersection.value() - m_center_of_rotation);
    const float angle                  = erhe::toolkit::angle_of_rotation<float>(q_, m_normal, m_reference_direction);
    const float snapped_angle          = snap                                   (angle);
    const vec3  rotation_axis_in_world = get_axis_direction                     ();
    const mat4  rotation               = erhe::toolkit::create_rotation<float>  (snapped_angle, rotation_axis_in_world);

    m_current_angle = angle;

    g_transform_tool->adjust_rotation(m_center_of_rotation, glm::quat_cast(rotation));
}

void Rotate_tool::render(const Render_context& context)
{
    if (
        (erhe::application::g_line_renderer_set == nullptr) ||
        (!is_active()) ||
        (context.camera == nullptr)
    ) {
        return;
    }

    const auto* camera_node = context.get_camera_node();
    if (camera_node == nullptr) {
        return;
    }

    const auto& shared = get_shared();
    const vec3  p                 = m_center_of_rotation;
    const vec3  n                 = m_normal;
    const vec3  side1             = m_reference_direction;
    const vec3  side2             = normalize(cross(n, side1));
    const vec3  position_in_world = p;//node.position_in_world();
    const float distance          = length(position_in_world - vec3{camera_node->position_in_world()});
    const float scale             = shared.settings.gizmo_scale * distance / 100.0f;
    const float r1                = scale * 6.0f;

    constexpr vec4 red   {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr vec4 blue  {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr vec4 orange{1.0f, 0.5f, 0.0f, 0.8f};

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();

    {
        const int sector_count = shared.settings.rotate_snap_enable
            ? static_cast<int>(glm::two_pi<float>() / glm::radians(shared.settings.rotate_snap))
            : 80;
        std::vector<vec3> positions;

        line_renderer.set_line_color(orange);
        line_renderer.set_thickness(-1.41f);
        for (int i = 0; i < sector_count + 1; ++i) {
            const float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
            const float theta = rel * glm::two_pi<float>();
            const bool  first = (i == 0);
            const bool  major = (i % 10 == 0);
            const float r0    =
                first
                    ? 0.0f
                    : major
                        ? 5.0f * scale
                        : 5.5f * scale;

            const vec3 p0 = p + r0 * std::cos(theta) * side1 + r0 * std::sin(theta) * side2;
            const vec3 p1 = p + r1 * std::cos(theta) * side1 + r1 * std::sin(theta) * side2;

            line_renderer.add_lines( {{ p0, p1 }} );
        }
    }

    // Circle (ring)
    {
        constexpr int sector_count = 200;
        std::vector<vec3> positions;
        for (int i = 0; i < sector_count + 1; ++i) {
            const float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
            const float theta = rel * glm::two_pi<float>();
            positions.emplace_back(
                p +
                r1 * std::cos(theta) * side1 +
                r1 * std::sin(theta) * side2
            );
        }
        for (size_t i = 0, count = positions.size(); i < count; ++i) {
            const std::size_t next_i = (i + 1) % count;
            line_renderer.add_lines( {{ positions[i], positions[next_i] }} );
        }
    }

    const float snapped_angle = snap(m_current_angle);
    const auto  snapped = p + r1 * std::cos(snapped_angle) * side1 + r1 * std::sin(snapped_angle) * side2;

    line_renderer.add_lines(red,                         { { p, r1 * side1 } } );
    line_renderer.add_lines(blue,                        { { p, snapped    } } );
    line_renderer.add_lines(get_axis_color(m_axis_mask), { { p - 10.0f * n, p + 10.0f * n } } );
}

} // namespace editor
