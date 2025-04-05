#include "tools/fly_camera_tool.hpp"
#include "tools/fly_camera_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/tools.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"
#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <glm/gtx/euler_angles.hpp>

#include <cmath>
#include <numeric>
#include <string>

namespace editor {

Jitter::Jitter() 
    : m_random_engine{m_random_device()}
    , m_distribution{0, 0}
{
}

void Jitter::imgui()
{
    ImGui::Checkbox("Enable Jitter", &m_enabled);
    if (m_enabled) {
        ImGui::BeginDisabled();
    }

    ImGui::DragIntRange2("Jitter x 100ns", &m_min, &m_max, 1.0, 0, 1000000, "%d x 100ns", nullptr, ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemEdited()) {
        if (m_max < m_min) {
            std::swap(m_min, m_max);
        }
        m_distribution = std::uniform_int_distribution<int>{m_min, m_max};
    }

    if (m_enabled) {
        ImGui::EndDisabled();
    }
}

void Jitter::sleep()
{
    if (!m_enabled) {
        return;
    }
    if (m_max == 0) {
        return;
    }
    std::uniform_int_distribution<int> distribution{m_min, m_max};
    int time_to_sleep = m_distribution(m_random_engine);
    if (time_to_sleep == 0) {
        return;
    }
    erhe::time::sleep_for_100ns(time_to_sleep);
}

auto Fly_camera_tool::try_ready() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_fly_camera->trace("Fly camera has no scene view");
        return false;
    }

    if (
        scene_view->get_hover(Hover_entry::tool_slot        ).valid ||
        scene_view->get_hover(Hover_entry::rendertarget_slot).valid
    ) {
        log_fly_camera->trace("Fly camera is hovering over tool or rendertarget");
        return false;
    }

    const Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view != nullptr) {
        // Exclude safe border near viewport edges from mouse interaction
        // to filter out viewport window resizing for example.
        const auto position_opt = viewport_scene_view->get_position_in_viewport();
        if (!position_opt.has_value()) {
            log_fly_camera->trace("Fly camera has no pointer position in viewport");
            return false;
        }
        constexpr float border   = 32.0f;
        const glm::vec2 position = position_opt.value();
        const erhe::math::Viewport viewport = viewport_scene_view->projection_viewport();
        if (
            (position.x <  border) ||
            (position.y <  border) ||
            (position.x >= viewport.width  - border) ||
            (position.y >= viewport.height - border)
        ) {
            log_fly_camera->trace("Fly camera has pointer position in border area that is ignored");
            return false;
        }
    }

    return true;
}

#pragma region Fly_camera_turn_command
Fly_camera_turn_command::Fly_camera_turn_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.turn_camera"}
    , m_context{context}
{
}

void Fly_camera_turn_command::try_ready()
{
    if (m_context.fly_camera_tool->try_ready()) {
        log_fly_camera->trace("Fly camera setting ready");
        set_ready();
        m_context.fly_camera_tool->set_cursor_relative_mode(true);
    }
}

void Fly_camera_turn_command::on_inactive()
{
    m_context.fly_camera_tool->set_cursor_relative_mode(false);
}

auto Fly_camera_turn_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    const auto value = input.variant.vector2.relative_value;
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            log_fly_camera->trace("Fly camera setting inactive due to nullptr hover scene view");
            set_inactive();
            return false;
        }
        if ((value.x != 0.0f) || (value.y != 0.0f)) {
            log_fly_camera->trace("Fly camera setting active due to pointer motion");
            set_active();
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        log_fly_camera->trace("Fly camera is not active");
        return false;
    }

    m_context.fly_camera_tool->turn_relative(input.timestamp_ns, -value.x, -value.y);
    return true;
}
#pragma endregion Fly_camera_turn_command

#pragma region Fly_camera_tumble_command
Fly_camera_tumble_command::Fly_camera_tumble_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.tumble"}
    , m_context{context}
{
}

void Fly_camera_tumble_command::try_ready()
{
    if (m_context.fly_camera_tool->try_start_tumble()) {
        set_ready();
        m_context.fly_camera_tool->set_cursor_relative_mode(true);
    }
}

auto Fly_camera_tumble_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    const auto value = input.variant.vector2.relative_value;
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            set_inactive();
            return false;
        }
        if ((value.x != 0.0f) || (value.y != 0.0f)) {
            set_active();
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.fly_camera_tool->tumble_relative(input.timestamp_ns, -value.x, -value.y);
    return true;
}
void Fly_camera_tumble_command::on_inactive()
{
        m_context.fly_camera_tool->set_cursor_relative_mode(false);
}

#pragma endregion Fly_camera_tumble_command

#pragma region Fly_camera_track_command
Fly_camera_track_command::Fly_camera_track_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.track"}
    , m_context{context}
{
}

void Fly_camera_track_command::try_ready()
{
    if (m_context.fly_camera_tool->try_start_track()) {
        set_ready();
    }
}

auto Fly_camera_track_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Ready) {
        if (m_context.fly_camera_tool->get_hover_scene_view() == nullptr) {
            set_inactive();
            return false;
        }
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    m_context.fly_camera_tool->track();
    return true;
}
#pragma endregion Fly_camera_turn_command

#pragma region Fly_camera_active_axis_float_command
Fly_camera_active_axis_float_command::Fly_camera_active_axis_float_command(
    erhe::commands::Commands& commands,
    Editor_context&           context,
    Variable                  variable,
    float                     scale
)
    : Command   {commands, "Fly_camera_active_axis_float_command"}
    , m_context {context}
    , m_variable{variable}
    , m_scale   {scale}
{
}

auto Fly_camera_active_axis_float_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_context.fly_camera_tool->set_active_control_value(input.timestamp_ns, m_variable, input.variant.float_value * m_scale);
}
#pragma endregion Fly_camera_variable_float_command

#pragma region Fly_camera_zoom_command
Fly_camera_zoom_command::Fly_camera_zoom_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.zoom_camera"}
    , m_context{context}
{
}

void Fly_camera_zoom_command::try_ready()
{
    if (m_context.fly_camera_tool->try_ready()) {
        set_ready();
    }
}

auto Fly_camera_zoom_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    set_ready();

    const auto value = input.variant.vector2.relative_value;
    //const auto state = get_command_state();
    //if (state == erhe::commands::State::Ready) {
    //    if (g_fly_camera_tool->get_hover_scene_view() == nullptr) {
    //        set_inactive();
    //        return false;
    //    }
    //    if (value.y != 0.0f) {
    //        set_active();
    //    }
    //}

    //if (get_command_state() != erhe::commands::State::Active) {
    //    return false;
    //}

    m_context.fly_camera_tool->zoom(input.timestamp_ns, value.y);
    return true;
}
#pragma endregion Fly_camera_zoom_command

#pragma region Fly_camera_frame_command
Fly_camera_frame_command::Fly_camera_frame_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Fly_camera.frame"}
    , m_context{context}
{
}

auto Fly_camera_frame_command::try_call() -> bool
{
    Scene_view* scene_view = m_context.fly_camera_tool->get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    erhe::scene::Camera* camera = m_context.fly_camera_tool->get_camera();
    if (camera == nullptr) {
        return false;
    }
    erhe::scene::Node* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return false;
    }

    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    erhe::math::Viewport viewport = viewport_scene_view->projection_viewport();

    erhe::math::Bounding_box bbox{};
    const std::vector<std::shared_ptr<erhe::Item_base>>& selection = m_context.selection->get_selection();
    for (const std::shared_ptr<erhe::Item_base>& item : selection) {
        const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
        if (!mesh) {
            continue;
        }
        const std::vector<erhe::primitive::Primitive>& primitives = mesh->get_primitives();
        for (const erhe::primitive::Primitive& primitive : primitives) {
            const auto bounding_box = primitive.get_bounding_box();
            if (!bounding_box.is_valid()) {
                continue;
            }
            erhe::math::Bounding_box world_bounding_box = bounding_box.transformed_by(node->world_from_node());
            bbox.include(world_bounding_box);
        }
    }
    if (!bbox.is_valid()) {
        return false;
    }

    glm::vec3 camera_position = camera_node->position_in_world();
    glm::vec3 target_position = bbox.center();
    glm::vec3 direction = target_position - camera_position;
    glm::vec3 direction_normalized = glm::normalize(target_position - camera_position);
    float size = glm::length(bbox.diagonal());
    erhe::scene::Projection::Fov_sides fov_sides = camera->projection()->get_fov_sides(viewport);
    float min_fov_side = std::numeric_limits<float>::max();
    for (float fov_side : { fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down }) {
        min_fov_side = std::min(std::abs(fov_side), min_fov_side);
    }
    ////float tan_fov_side = std::tanf(min_fov_side);
    float tan_fov_side = tanf(min_fov_side);
    float fit_distance = size / (2.0f * tan_fov_side);
    glm::vec3 new_position = target_position - fit_distance * direction_normalized;
    glm::mat4 new_world_from_node = erhe::math::create_look_at(new_position, target_position, glm::vec3{0.0f, 1.0f, 0.0});
    camera_node->set_world_from_node(new_world_from_node);
    return true;
}
#pragma endregion Fly_camera_frame_command

#pragma region Fly_camera_move_command
Fly_camera_move_command::Fly_camera_move_command(
    erhe::commands::Commands&            commands,
    Editor_context&                      context,
    const Variable                       variable,
    const erhe::math::Input_axis_control control,
    const bool                           active
)
    : Command   {commands, "Fly_camera.move"}
    , m_context {context}
    , m_variable{variable}
    , m_control {control }
    , m_active  {active  }
{
}

auto Fly_camera_move_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    return m_context.fly_camera_tool->try_move(input.timestamp_ns, m_variable, m_control, m_active);
}
#pragma endregion Fly_camera_move_command

Fly_camera_serialization_command::Fly_camera_serialization_command(erhe::commands::Commands& commands, Editor_context& context, bool store)
    : Command  {commands, "Fly_camera_serialization_command"}
    , m_context{context}
    , m_store  {store}
{
}

auto Fly_camera_serialization_command::try_call() -> bool
{
    //// TODO 
    //// m_context.fly_camera_tool->synthesize_input();
    m_context.fly_camera_tool->serialize_transform(m_store);
    return true;
}

void Fly_camera_tool::synthesize_input()
{
    std::random_device                    random_device;
    std::mt19937                          random_engine{random_device()};
    std::uniform_real_distribution<float> distribution(0.1f, 20.0f);

    int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

    m_before_position    = m_camera_controller->get_position();
    m_before_orientation = glm::quat_cast(m_camera_controller->get_orientation());

    float mouse_x{0.0f};
    float mouse_y{0.0f};
    m_context.context_window->get_cursor_position(mouse_x, mouse_y);

    m_synthetic_input_events.clear();

    timestamp_ns = timestamp_ns + int64_t{20'000'000}; // 20 ms
    m_synthetic_input_events.push_back(
        erhe::window::Input_event{
            .type = erhe::window::Input_event_type::key_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .key_event = {
                    .keycode       = erhe::window::Key_d,
                    .modifier_mask = 0,
                    .pressed       = true
                }
            }
        }
    );

    timestamp_ns = timestamp_ns + int64_t{5'000'000}; // 5ms
    m_synthetic_input_events.push_back(
        erhe::window::Input_event{
            .type = erhe::window::Input_event_type::key_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .key_event = {
                    .keycode       = erhe::window::Key_d,
                    .modifier_mask = 0,
                    .pressed       = false
                }
            }
        }
    );

    timestamp_ns = timestamp_ns + int64_t{300'000'000}; // 300 ms
    m_synthetic_input_events.push_back(
        erhe::window::Input_event{
            .type = erhe::window::Input_event_type::no_event,
            .timestamp_ns = timestamp_ns,
        }
    );

#if 0
    // Press left mouse button down
    timestamp = timestamp + std::chrono::milliseconds(20);
    m_synthetic_input_events.push_back(
        erhe::window::Input_event{
            .type = erhe::window::Input_event_type::mouse_button_event,
            .timestamp = timestamp,
            .u = {
                .mouse_button_event = {
                    .button        = erhe::window::Mouse_button_left,
                    .pressed       = true,
                    .modifier_mask = 0
                }
            }
        }
    );

    // Move mouse
    float end_x = mouse_x + m_synth_distance;
    while (mouse_x < end_x) {
        //float dx = distribution(random_engine);
        float dx = m_synth_distance / 100.0f;
        mouse_x += dx;
        if (mouse_x > end_x) {
            dx = end_x - mouse_x;
            mouse_x = end_x;
        }

        timestamp = timestamp + std::chrono::milliseconds(4);
        m_synthetic_input_events.push_back(
            erhe::window::Input_event{
                .type = erhe::window::Input_event_type::mouse_move_event,
                .timestamp = timestamp,
                .u = {
                    .mouse_move_event = {
                        .x             = static_cast<float>(mouse_x),
                        .y             = static_cast<float>(mouse_y),
                        .dx            = static_cast<float>(dx),
                        .dy            = static_cast<float>(0.0f),
                        .modifier_mask = 0
                    }
                }
            }
        );
    }

    // Release mouse button
    timestamp = timestamp + std::chrono::milliseconds(10);
    m_synthetic_input_events.push_back(
        erhe::window::Input_event{
            .type = erhe::window::Input_event_type::mouse_button_event,
            .timestamp = timestamp,
            .u = {
                .mouse_button_event = {
                    .button        = erhe::window::Mouse_button_left,
                    .pressed       = false,
                    .modifier_mask = 0
                }
            }
        }
    );
#endif

    log_input->info("Synthesizing {} input events", m_synthetic_input_events.size());

    m_context.context_window->set_input_event_synthesizer_callback(
        [this](erhe::window::Context_window& context_window) {
            const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            while (!m_synthetic_input_events.empty()) {
                erhe::window::Input_event event = m_synthetic_input_events.front();
                if (event.timestamp_ns > now) {
                    return;
                }
                event.timestamp_ns = now;
                context_window.inject_input_event(event);
                m_synthetic_input_events.pop_front();
            }
            if (m_synthetic_input_events.empty()) {
                log_input->info("Removing input event synthesizer");
                context_window.set_input_event_synthesizer_callback({});
            }

            m_after_position    = m_camera_controller->get_position();
            m_after_orientation = glm::quat_cast(m_camera_controller->get_orientation());

            const glm::quat orientation_delta = m_after_orientation * glm::inverse(m_before_orientation);
            const float     yaw_delta         = glm::yaw(orientation_delta);
            const float     yaw_degrees       = glm::degrees(yaw_delta);
            const float     distance          = glm::distance(m_before_position, m_after_position);
            log_input->info("Synthetic distance = {}, rotation = {} degrees", distance, yaw_degrees);

            //const glm::mat4 heading            = m_camera_controller->get_orientation();
            //const glm::quat before_orientation = glm::quat_cast(heading);
        }
    );
}

void Fly_camera_tool::serialize_transform(bool store)
{
    if (m_node == nullptr) {
        return;
    }
    if (store) {
        const erhe::scene::Trs_transform& world_from_node = m_node->node_data.transforms.world_from_node;
        glm::vec3 translation = world_from_node.get_translation();
        glm::quat rotation    = world_from_node.get_rotation();
        mINI::INIFile file("fly_camera.ini");
        mINI::INIStructure ini;
        ini["translation"]["x"] = std::to_string(translation.x);
        ini["translation"]["y"] = std::to_string(translation.y);
        ini["translation"]["z"] = std::to_string(translation.z);
        ini["rotation"]["x"] = std::to_string(rotation.x);
        ini["rotation"]["y"] = std::to_string(rotation.y);
        ini["rotation"]["z"] = std::to_string(rotation.z);
        ini["rotation"]["w"] = std::to_string(rotation.w);
        file.generate(ini);
    } else {
        auto& settings_ini = erhe::configuration::get_ini_file("fly_camera.ini");
        glm::vec3 translation{};
        const auto& translation_section = settings_ini.get_section("translation");
        translation_section.get("x", translation.x);
        translation_section.get("y", translation.y);
        translation_section.get("z", translation.z);
        glm::quat rotation{};
        const auto& rotation_section = settings_ini.get_section("rotation");
        rotation_section.get("x", rotation.x);
        rotation_section.get("y", rotation.y);
        rotation_section.get("z", rotation.z);
        rotation_section.get("w", rotation.w);
        const erhe::scene::Trs_transform& world_from_node{translation, rotation};
        m_node->set_world_from_node(world_from_node);
    }
}

Fly_camera_tool::Fly_camera_tool(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window       {imgui_renderer, imgui_windows, "Fly Camera", "fly_camera"}
    , Tool                            {editor_context}
    , m_turn_command                  {commands, editor_context}
    , m_tumble_command                {commands, editor_context}
    , m_track_command                 {commands, editor_context}
    , m_zoom_command                  {commands, editor_context}
    , m_frame_command                 {commands, editor_context}
    , m_move_up_active_command        {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::more, true }
    , m_move_up_inactive_command      {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::more, false}
    , m_move_down_active_command      {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::less, true }
    , m_move_down_inactive_command    {commands, editor_context, Variable::translate_y, erhe::math::Input_axis_control::less, false}
    , m_move_left_active_command      {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::less, true }
    , m_move_left_inactive_command    {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::less, false}
    , m_move_right_active_command     {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::more, true }
    , m_move_right_inactive_command   {commands, editor_context, Variable::translate_x, erhe::math::Input_axis_control::more, false}
    , m_move_forward_active_command   {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::less, true }
    , m_move_forward_inactive_command {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::less, false}
    , m_move_backward_active_command  {commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::more, true }
    , m_move_backward_inactive_command{commands, editor_context, Variable::translate_z, erhe::math::Input_axis_control::more, false}
    , m_active_translate_x_command    {commands, editor_context, Variable::translate_x,  1.0 / 32.0f}
    , m_active_translate_y_command    {commands, editor_context, Variable::translate_y, -1.0 / 32.0f}
    , m_active_translate_z_command    {commands, editor_context, Variable::translate_z,  1.0 / 32.0f}
    , m_active_rotate_x_command       {commands, editor_context, Variable::rotate_x,     1.0 / 128.0f}
    , m_active_rotate_y_command       {commands, editor_context, Variable::rotate_y,    -1.0 / 128.0f}
    , m_active_rotate_z_command       {commands, editor_context, Variable::rotate_z,     1.0 / 128.0f}
    , m_serialize_transform_command   {commands, editor_context, true}
    , m_deserialize_transform_command {commands, editor_context, false}

    , m_tx_graph       {"Tx",      "time", "ms", "Tx",      ""}
    , m_ty_graph       {"Ty",      "time", "ms", "Ty",      ""}
    , m_tz_graph       {"Tz",      "time", "ms", "Tz",      ""}
    , m_px_graph       {"Px",      "time", "ms", "Px",      ""}
    , m_py_graph       {"Py",      "time", "ms", "Py",      ""}
    , m_pz_graph       {"Pz",      "time", "ms", "Pz",      ""}
    , m_heading_graph  {"Heading", "time", "ms", "heading", ""}
{
    ERHE_PROFILE_FUNCTION();

    m_tx_graph.path_color  = 0xffcc4444;
    m_ty_graph.path_color  = 0xff44cc44;
    m_tz_graph.path_color  = 0xff4444cc;
    m_px_graph.path_color  = 0xffcc4444;
    m_py_graph.path_color  = 0xff44cc44;
    m_pz_graph.path_color  = 0xff4444cc;
    m_tx_graph.key_color   = 0xffff0000;
    m_ty_graph.key_color   = 0xff00ff00;
    m_tz_graph.key_color   = 0xff0000ff;
    m_px_graph.key_color   = 0xffff0000;
    m_py_graph.key_color   = 0xff00ff00;
    m_pz_graph.key_color   = 0xff0000ff;
    m_tx_graph.hover_color = 0xffff8888;
    m_ty_graph.hover_color = 0xff88ff88;
    m_tz_graph.hover_color = 0xff8888ff;
    m_px_graph.hover_color = 0xffff8888;
    m_py_graph.hover_color = 0xff88ff88;
    m_pz_graph.hover_color = 0xff8888ff;

    if (editor_context.OpenXR) {
        return;
    }

    m_camera_controller = std::make_shared<Frame_controller>();

    m_camera_controller->get_variable(Variable::translate_x).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_y).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);
    m_camera_controller->get_variable(Variable::translate_z).set_damp_and_max_delta(config.velocity_damp, config.velocity_max_delta);

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "camera_controls");
    ini.get("invert_x",           config.invert_x);
    ini.get("invert_y",           config.invert_y);
    ini.get("velocity_damp",      config.velocity_damp);
    ini.get("velocity_max_delta", config.velocity_max_delta);
    ini.get("sensitivity",        m_sensitivity);

    set_base_priority(c_priority);
    set_description  ("Fly Camera");
    set_flags        (Tool_flags::background);

    tools.register_tool(this);

    commands.register_command(&m_move_up_active_command);
    commands.register_command(&m_move_up_inactive_command);
    commands.register_command(&m_move_down_active_command);
    commands.register_command(&m_move_down_inactive_command);
    commands.register_command(&m_move_left_active_command);
    commands.register_command(&m_move_left_inactive_command);
    commands.register_command(&m_move_right_active_command);
    commands.register_command(&m_move_right_inactive_command);
    commands.register_command(&m_move_forward_active_command);
    commands.register_command(&m_move_forward_inactive_command);
    commands.register_command(&m_move_backward_active_command);
    commands.register_command(&m_move_backward_inactive_command);
    commands.register_command(&m_active_translate_x_command);
    commands.register_command(&m_active_translate_y_command);
    commands.register_command(&m_active_translate_z_command);
    commands.register_command(&m_active_rotate_x_command);
    commands.register_command(&m_active_rotate_y_command);
    commands.register_command(&m_active_rotate_z_command);
    commands.register_command(&m_serialize_transform_command);
    commands.register_command(&m_deserialize_transform_command);

    commands.bind_command_to_key(&m_move_up_active_command,         erhe::window::Key_q,  true );
    commands.bind_command_to_key(&m_move_up_inactive_command,       erhe::window::Key_q,  false);
    commands.bind_command_to_key(&m_move_down_active_command,       erhe::window::Key_e,  true );
    commands.bind_command_to_key(&m_move_down_inactive_command,     erhe::window::Key_e,  false);
    commands.bind_command_to_key(&m_move_left_active_command,       erhe::window::Key_a,  true );
    commands.bind_command_to_key(&m_move_left_inactive_command,     erhe::window::Key_a,  false);
    commands.bind_command_to_key(&m_move_right_active_command,      erhe::window::Key_d,  true );
    commands.bind_command_to_key(&m_move_right_inactive_command,    erhe::window::Key_d,  false);
    commands.bind_command_to_key(&m_move_forward_active_command,    erhe::window::Key_w,  true );
    commands.bind_command_to_key(&m_move_forward_inactive_command,  erhe::window::Key_w,  false);
    commands.bind_command_to_key(&m_move_backward_active_command,   erhe::window::Key_s,  true );
    commands.bind_command_to_key(&m_move_backward_inactive_command, erhe::window::Key_s,  false);
    commands.bind_command_to_key(&m_serialize_transform_command,    erhe::window::Key_page_up, true);
    commands.bind_command_to_key(&m_deserialize_transform_command,  erhe::window::Key_page_down, true);

    commands.register_command(&m_turn_command);
    commands.bind_command_to_mouse_drag(&m_turn_command, erhe::window::Mouse_button_left, false, 0);

    commands.register_command(&m_tumble_command);
    commands.bind_command_to_mouse_drag(&m_tumble_command, erhe::window::Mouse_button_left, true, erhe::window::Key_modifier_bit_menu);

    commands.register_command(&m_track_command);
    commands.bind_command_to_mouse_drag(&m_track_command, erhe::window::Mouse_button_middle, true, erhe::window::Key_modifier_bit_menu);

    commands.register_command(&m_zoom_command);
    commands.bind_command_to_mouse_wheel(&m_zoom_command);

    commands.register_command(&m_frame_command);
    commands.bind_command_to_key(&m_frame_command, erhe::window::Key_f, true);

    m_rotate_scale_x = config.invert_x ? -1.0f / 512.0f : 1.0f / 512.f;
    m_rotate_scale_y = config.invert_y ? -1.0f / 512.0f : 1.0f / 512.f;

    commands.bind_command_to_controller_axis(&m_active_translate_x_command, 0);
    commands.bind_command_to_controller_axis(&m_active_translate_y_command, 2);
    commands.bind_command_to_controller_axis(&m_active_translate_z_command, 1);
    commands.bind_command_to_controller_axis(&m_active_rotate_x_command, 3);
    commands.bind_command_to_controller_axis(&m_active_rotate_y_command, 5);
    commands.bind_command_to_controller_axis(&m_active_rotate_z_command, 4);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            Tool::on_message(message);
            using namespace erhe::bit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                on_hover_viewport_change();
            }
        }
    );

    m_turn_command                  .set_host(this);
    m_tumble_command                .set_host(this);
    m_zoom_command                  .set_host(this);
    m_frame_command                 .set_host(this);
    m_move_up_active_command        .set_host(this);
    m_move_up_inactive_command      .set_host(this);
    m_move_down_active_command      .set_host(this);
    m_move_down_inactive_command    .set_host(this);
    m_move_left_active_command      .set_host(this);
    m_move_left_inactive_command    .set_host(this);
    m_move_right_active_command     .set_host(this);
    m_move_right_inactive_command   .set_host(this);
    m_move_forward_active_command   .set_host(this);
    m_move_forward_inactive_command .set_host(this);
    m_move_backward_active_command  .set_host(this);
    m_move_backward_inactive_command.set_host(this);
}

void Fly_camera_tool::update_camera()
{
    if (m_context.OpenXR) {
        return;
    }
    if (!m_use_viewport_camera) {
        return;
    }

    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    const auto camera = (viewport_scene_view)
        ? viewport_scene_view->get_camera()
        : std::shared_ptr<erhe::scene::Camera>{};
    const auto* camera_node = camera ? camera->get_node() : nullptr;

    // TODO This is messy

    if (m_camera_controller->get_node() != camera_node) {
        set_camera(camera.get());
    }
}

void Fly_camera_tool::set_camera(erhe::scene::Camera* const camera, erhe::scene::Node* node)
{
    // attach() below requires world from node matrix, which
    // might not be valid due to transform hierarchy.
    if ((node == nullptr) && (camera != nullptr)) {
        node = camera->get_node();
    }

    if (node != nullptr) {
        auto* scene_root = static_cast<Scene_root*>(node->node_data.host);
        if (scene_root != nullptr) {
            scene_root->get_scene().update_node_transforms();
        } else {
            log_fly_camera->warn("node does not have scene root");
        }
    }

    m_camera_controller->set_node(node);
    m_camera = camera;
    m_node = node;
}

auto Fly_camera_tool::get_camera() const -> erhe::scene::Camera*
{
    return m_camera;
}

void Fly_camera_tool::translation(int64_t timestamp_ns, const int tx, const int ty, const int tz)
{
    static_cast<void>(timestamp_ns);
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    float x = static_cast<float>(tx) * m_camera_controller->translate_x.max_delta() / 256.0f;
    float y = static_cast<float>(ty) * m_camera_controller->translate_y.max_delta() / 256.0f;
    float z = static_cast<float>(tz) * m_camera_controller->translate_z.max_delta() / 256.0f;

    m_camera_controller->translate_x.adjust(x);
    m_camera_controller->translate_y.adjust(y);
    m_camera_controller->translate_z.adjust(z);
}

void Fly_camera_tool::rotation(int64_t timestamp_ns, const int rx, const int ry, const int rz)
{
    static_cast<void>(timestamp_ns);
    if (!m_camera_controller) {
        return;
    }

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    constexpr float scale = 65536.0f;
    m_camera_controller->rotate_x.adjust(m_sensitivity * static_cast<float>(rx) / scale);
    m_camera_controller->rotate_y.adjust(m_sensitivity * static_cast<float>(ry) / scale);
    m_camera_controller->rotate_z.adjust(m_sensitivity * static_cast<float>(rz) / scale);
}

void Fly_camera_tool::on_hover_viewport_change()
{
    m_camera_controller->translate_x.reset();
    m_camera_controller->translate_y.reset();
    m_camera_controller->translate_z.reset();
}

auto Fly_camera_tool::adjust(int64_t timestamp_ns, Variable variable, float value) -> bool
{
    static_cast<void>(timestamp_ns);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    if ((get_hover_scene_view() == nullptr)) {
        m_camera_controller->translate_x.reset();
        m_camera_controller->translate_y.reset();
        m_camera_controller->translate_z.reset();
        m_camera_controller->rotate_x.reset();
        m_camera_controller->rotate_y.reset();
        m_camera_controller->rotate_z.reset();
        return false;
    }

    auto& controller = m_camera_controller->get_variable(variable);
    controller.adjust(value);
    return true;
}

auto Fly_camera_tool::set_active_control_value(int64_t timestamp_ns, Variable variable, float value) -> bool
{
    static_cast<void>(timestamp_ns);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};
    m_camera_controller->set_active_control_value(variable, value);
    return true;
}

auto Fly_camera_tool::try_move(int64_t timestamp_ns, const Variable variable, const erhe::math::Input_axis_control control, const bool active) -> bool
{
    static_cast<void>(timestamp_ns);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    if ((get_hover_scene_view() == nullptr) && active) {
        m_camera_controller->translate_x.reset();
        m_camera_controller->translate_y.reset();
        m_camera_controller->translate_z.reset();
        return false;
    }

    auto& controller = m_camera_controller->get_variable(variable);
    controller.set(control, active);
    if (m_recording) {
        record_translation_sample(timestamp_ns);
    }
    return true;
}

auto Fly_camera_tool::zoom(int64_t timestamp_ns, const float delta) -> bool
{
    static_cast<void>(timestamp_ns);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    if (delta != 0.0f) {
        glm::vec3 position = m_camera_controller->get_position();
        const float l = glm::length(position);
        const float k = (-1.0f / 32.0f) * l * delta;
        m_camera_controller->get_variable(Variable::translate_z).adjust(k);
    }

    return true;
}

auto Fly_camera_tool::turn_relative(int64_t timestamp_ns, const float dx, const float dy) -> bool
{
    static_cast<void>(timestamp_ns);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    const float rx = m_sensitivity * dy * m_rotate_scale_y;
    const float ry = m_sensitivity * dx * m_rotate_scale_x;
    if (false) {
        m_camera_controller->apply_rotation(rx, ry, 0.0f);
    } else {
        m_camera_controller->get_variable(Variable::rotate_x).adjust(rx / 2.0f);
        m_camera_controller->get_variable(Variable::rotate_y).adjust(ry / 2.0f);
    }

    if (m_recording) {
        record_heading_sample(timestamp_ns);
    }

    return true;
}

void Fly_camera_tool::set_cursor_relative_mode(bool relative_mode_enabled)
{
    if (!m_context.OpenXR) {
        m_context.context_window->set_cursor_relative_hold(relative_mode_enabled);
    }
}

auto Fly_camera_tool::try_start_tumble() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if (hover == nullptr) {
        return false;
    }

    if (!hover->position.has_value()) {
        return false;
    }

    m_tumble_pivot = hover->position;
    return true;
}

auto Fly_camera_tool::tumble_relative(int64_t timestamp_ns, float dx, float dy) -> bool
{
    static_cast<void>(timestamp_ns); // TODO consider how to correctly the time into account here
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    const auto viewport_scene_view = m_context.scene_views->hover_scene_view();
    if (!viewport_scene_view) {
        return false;
    }

    if (!m_tumble_pivot.has_value()) {
        return false;
    }

    const float rx = m_sensitivity * dy * m_rotate_scale_y;
    const float ry = m_sensitivity * dx * m_rotate_scale_x;
    m_camera_controller->apply_tumble(m_tumble_pivot.value(), rx, ry, 0.0f);
    return true;
}

auto Fly_camera_tool::try_start_track() -> bool
{
    const Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    const Hover_entry* hover = scene_view->get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit);
    if (hover == nullptr) {
        return false;
    }

    if (!hover->position.has_value()) {
        return false;
    }

    m_track_plane_point = hover->position;
    m_track_plane_normal = m_camera_controller->get_axis_z();
    return true;
}

auto Fly_camera_tool::track() -> bool
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }

    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }

    if (!m_track_plane_point.has_value() || !m_track_plane_normal.has_value()) {
        return false;
    }

    auto get_track_position = [&]() -> std::optional<glm::vec3>
    {
        const std::optional<glm::vec3> ray_origin_opt    = scene_view->get_control_ray_origin_in_world();
        const std::optional<glm::vec3> ray_direction_opt = scene_view->get_control_ray_direction_in_world();
        if (!ray_origin_opt.has_value() || !ray_direction_opt.has_value()) {
            return {};
        }
        const glm::vec3 ray_origin    = ray_origin_opt   .value();
        const glm::vec3 ray_direction = ray_direction_opt.value();
        std::optional<float> t = erhe::math::intersect_plane<float>(m_track_plane_normal.value(), m_track_plane_point.value(), ray_origin, ray_direction);
        if (!t.has_value()) {
            return {};
        }
        return ray_origin + t.value() * ray_direction;
    };

    std::optional<glm::vec3> hover_position = get_track_position();
    if (!hover_position.has_value()) {
        return false;
    }

    glm::vec3 translation = hover_position.value() - m_track_plane_point.value();
    if (glm::length(translation) < 0.001f) {
        return true;
    }

    glm::vec3 old_position = m_camera_controller->get_position();
    glm::vec3 new_position = old_position - translation;
    m_camera_controller->set_position(new_position);
    m_camera_controller->get_node()->update_world_from_node();
    viewport_scene_view->update_hover(true);

    m_track_plane_point = get_track_position();
    return true;
}

//void Fly_camera_tool::update_fixed_step(const Time_context&)
//{
//    if (!m_camera_controller) { // TODO
//        return; 
//    }
//
//    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};
//
//    m_camera_controller->update_fixed_step();
//}

void Fly_camera_tool::record_translation_sample(int64_t time_ns)
{
    if (!m_recording) {
        return;
    }
    if (m_sample_count >= m_max_samples) {
        m_recording = false;
    }
    erhe::scene::Node* node = m_camera_controller->get_node();
    if (node == nullptr) {
        return;
    }

    int64_t      time_ns_ = time_ns - m_recording_start_time_ns;
    const double time_ms_ = static_cast<double>(time_ns_) / 1'000'000.0;
    const float  time_ms  = static_cast<float>(time_ms_);

    const glm::vec3 p = node->position_in_world();
    float tx = m_camera_controller->translate_x.current_value();
    float ty = m_camera_controller->translate_y.current_value();
    float tz = m_camera_controller->translate_z.current_value();
    m_tx_graph.samples.push_back(ImVec2{time_ms, tx});
    m_ty_graph.samples.push_back(ImVec2{time_ms, ty});
    m_tz_graph.samples.push_back(ImVec2{time_ms, tz});
    m_px_graph.samples.push_back(ImVec2{time_ms, p.x});
    m_py_graph.samples.push_back(ImVec2{time_ms, p.y});
    m_pz_graph.samples.push_back(ImVec2{time_ms, p.z});
}

void Fly_camera_tool::record_heading_sample(int64_t timestamp_ns)
{
    int64_t      time_ns_ = timestamp_ns - m_recording_start_time_ns;
    const double time_ms_ = static_cast<double>(time_ns_) / 1'000'000.0;
    const float  time_ms  = static_cast<float>(time_ms_);

    // record_sample(timestamp_ns);
    const glm::mat4 orientation_mat4 = m_camera_controller->get_orientation();
    const glm::quat orientation_quat = glm::quat_cast(orientation_mat4);
    const float     heading          = glm::yaw(orientation_quat);
    m_heading_graph.samples.push_back(ImVec2{time_ms, heading});
    m_sample_count += 1;
}

void Fly_camera_tool::on_frame_begin()
{
    update_camera();
}

void Fly_camera_tool::on_frame_end()
{
    m_jitter.sleep();
}

void Fly_camera_tool::update_fixed_step(const Time_context& time_context)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    static_cast<void>(time_context);
    if (!m_camera_controller) { // TODO
        return;
    }
    m_camera_controller->update_fixed_step();
}

//    record_sample(timestamp_ns);

auto simple_degrees(const float radians_value) -> float
{
    const auto degrees_value   = glm::degrees(radians_value);
    const auto degrees_mod_360 = std::fmod(degrees_value, 360.0f);
    return (degrees_mod_360 <= 180.0f)
        ? degrees_mod_360
        : degrees_mod_360 - 360.0f;
}

void Fly_camera_tool::show_input_axis_ui(const char* label, erhe::math::Input_axis& input_axis) const
{
    ImGui::PushID(label);

    bool  less  = input_axis.less();
    bool  more  = input_axis.more();
    float damp  = input_axis.damp();
    float value = input_axis.current_value();

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
    if (less) { ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("<"); }
    if (more) { ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(">"); }

    ImGui::TableSetColumnIndex(3); 
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushID(3);
    ImGui::SliderFloat("##", &damp, 0.0f, 1.0, "%.3f", ImGuiSliderFlags_NoRoundToFormat);
    if (ImGui::IsItemEdited()) {
        input_axis.set_damp(damp);
    }
    ImGui::PopID();

    ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::Text("%.7f", value);

    ImGui::PopID();
}

void Fly_camera_tool::imgui()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_fly_camera{m_mutex};

    float speed = m_camera_controller->translate_z.max_delta();
    ImGui::SliderFloat("Speed",       &speed,         0.001f, 0.1f); //, "%.3f", logarithmic);
    if (ImGui::IsItemEdited()) {
        m_camera_controller->translate_x.set_max_delta(speed);
        m_camera_controller->translate_y.set_max_delta(speed);
        m_camera_controller->translate_z.set_max_delta(speed);
    }
    ImGui::SliderFloat("Move Speed", &m_camera_controller->move_speed, 0.001f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Turn Speed", &m_sensitivity, 0.2f, 2.0f);


    //erhe::math::Input_axis& control = m_camera_controller->translate_x;
    if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_None)) {
        ImGui::Text("Input events: %zu", m_sample_count);
        m_jitter.imgui();
        ImGui::Separator();
        ImGui::SliderFloat("Synth Input Distance", &m_synth_distance, 10.0f, 10000.0f);
        ImGui::Separator();

        ImGui::Checkbox   ("Use Viewport Camera", &m_use_viewport_camera);

        if (ImGui::TreeNodeEx("Controls", ImGuiTreeNodeFlags_None)) {
            ImGui::BeginTable("Controls", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch, 5.0f);
            ImGui::TableSetupColumn("Less",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("More",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("damp",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            show_input_axis_ui("Tx", m_camera_controller->translate_x);
            show_input_axis_ui("Ty", m_camera_controller->translate_y);
            show_input_axis_ui("Tz", m_camera_controller->translate_z);
            show_input_axis_ui("Rx", m_camera_controller->rotate_x);
            show_input_axis_ui("Ry", m_camera_controller->rotate_y);
            show_input_axis_ui("Rz", m_camera_controller->rotate_z);
            show_input_axis_ui("sm", m_camera_controller->speed_modifier);
            ImGui::EndTable();
            ImGui::TreePop();
        }

                           ImGui::Checkbox("Tx",      &m_tx_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Ty",      &m_ty_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Tz",      &m_tz_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Px",      &m_px_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Py",      &m_py_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Pz",      &m_pz_graph     .plot);
        ImGui::SameLine(); ImGui::Checkbox("Heading", &m_heading_graph.plot);

        if (m_tx_graph     .plot) ImGui::SliderFloat("Tx Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_ty_graph     .plot) ImGui::SliderFloat("Ty Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_tz_graph     .plot) ImGui::SliderFloat("Tz Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_px_graph     .plot) ImGui::SliderFloat("Px Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_py_graph     .plot) ImGui::SliderFloat("Py Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_pz_graph     .plot) ImGui::SliderFloat("Pz Scale",     &m_tx_graph      .y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
        if (m_heading_graph.plot) ImGui::SliderFloat("Heading Scale", &m_heading_graph.y_scale, 0.0001f, 1000000.0f, "%.5f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);

        ImGui::TreePop();
    }

    if (!m_recording) {
        if (ImGui::Button("Start Recording")) {
            m_events         .clear();
            m_tx_graph       .clear();
            m_ty_graph       .clear();
            m_tz_graph       .clear();
            m_px_graph       .clear();
            m_py_graph       .clear();
            m_pz_graph       .clear();
            m_heading_graph  .clear();
            m_recording_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            m_sample_count = 0;
            m_recording = true;
        }
    } else {
        if (ImGui::Button("Stop Recording")) {
            m_recording = false;
        }
    }

    if (m_graph_plotter.begin()) {
        for (const Event& event : m_events) {
            m_graph_plotter.sample_x_line(event.x, 0x88008800);
        }
        m_graph_plotter.plot(m_tx_graph     );
        m_graph_plotter.plot(m_ty_graph     );
        m_graph_plotter.plot(m_tz_graph     );
        m_graph_plotter.plot(m_px_graph     );
        m_graph_plotter.plot(m_py_graph     );
        m_graph_plotter.plot(m_pz_graph     );
        m_graph_plotter.plot(m_heading_graph);
        for (const Event& event : m_events) {
            m_graph_plotter.sample_text(event.x, 0.0f, event.text.c_str(), 0xff00ff00);
        }
        m_graph_plotter.end();
    }
#endif
}

}
