#include "windows/controller_inputs_window.hpp"

#include "app_context.hpp"
#include "xr/headset_view.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_action.hpp"
#endif

#include <imgui/imgui.h>

namespace editor {

Controller_inputs_window::Controller_inputs_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Controller Inputs", "controller_inputs"}
    , m_context                {context}
{
}

#if defined(ERHE_XR_LIBRARY_OPENXR)

namespace {

void state_cell(const bool active, const bool state)
{
    if (!active) {
        ImGui::TextDisabled("inactive");
        return;
    }
    if (state) {
        ImGui::TextColored(ImVec4{0.2f, 1.0f, 0.2f, 1.0f}, "TRUE");
    } else {
        ImGui::TextUnformatted("false");
    }
}

}

void Controller_inputs_window::boolean_row(const char* label, const erhe::xr::Xr_action_boolean* action)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    if (action == nullptr) {
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("-");
        return;
    }
    const bool active = action->state.isActive == XR_TRUE;
    const bool state  = action->state.currentState == XR_TRUE;

    Boolean_history& history = m_boolean_histories[action];
    if (history.initialized && (history.last_state != state)) {
        ++history.change_count;
    }
    history.last_state  = state;
    history.initialized = true;

    ImGui::TableSetColumnIndex(1);
    state_cell(active, state);
    ImGui::TableSetColumnIndex(2);
    if (history.change_count > 0) {
        ImGui::Text("%llu", static_cast<unsigned long long>(history.change_count));
    } else {
        ImGui::TextDisabled("0");
    }
}

void Controller_inputs_window::imgui()
{
    if (!m_context.OpenXR || (m_context.headset_view == nullptr)) {
        ImGui::TextUnformatted("OpenXR is not active.");
        return;
    }
    erhe::xr::Headset* headset = m_context.headset_view->get_headset();
    if (headset == nullptr) {
        ImGui::TextUnformatted("No headset.");
        return;
    }
    erhe::xr::Xr_actions* actions = headset->get_actions_right();
    if (actions == nullptr) {
        ImGui::TextUnformatted("No right controller actions.");
        return;
    }

    ImGui::TextUnformatted("Right controller");

    if (ImGui::BeginTable("right_booleans", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Boolean");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Changes");
        ImGui::TableHeadersRow();
        boolean_row("select_click",     actions->select_click);
        boolean_row("system_click",     actions->system_click);
        boolean_row("menu_click",       actions->menu_click);
        boolean_row("squeeze_click",    actions->squeeze_click);
        boolean_row("a_click",          actions->a_click);
        boolean_row("a_touch",          actions->a_touch);
        boolean_row("b_click",          actions->b_click);
        boolean_row("b_touch",          actions->b_touch);
        boolean_row("trigger_click",    actions->trigger_click);
        boolean_row("trigger_touch",    actions->trigger_touch);
        boolean_row("trackpad_click",   actions->trackpad_click);
        boolean_row("trackpad_touch",   actions->trackpad_touch);
        boolean_row("thumbstick_click", actions->thumbstick_click);
        boolean_row("thumbstick_touch", actions->thumbstick_touch);
        boolean_row("thumbrest_touch",  actions->thumbrest_touch);
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("right_floats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Float");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        const auto float_row = [](const char* label, const erhe::xr::Xr_action_float* action) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (action == nullptr) {
                ImGui::TextDisabled("-");
            } else if (action->state.isActive != XR_TRUE) {
                ImGui::TextDisabled("inactive");
            } else {
                ImGui::Text("%.3f", action->state.currentState);
            }
        };
        float_row("trigger_value",  actions->trigger_value);
        float_row("squeeze_value",  actions->squeeze_value);
        float_row("squeeze_force",  actions->squeeze_force);
        float_row("trackpad_force", actions->trackpad_force);
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("right_vector2", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Vector2");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        const auto vector2_row = [](const char* label, const erhe::xr::Xr_action_vector2f* action) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (action == nullptr) {
                ImGui::TextDisabled("-");
            } else if (action->state.isActive != XR_TRUE) {
                ImGui::TextDisabled("inactive");
            } else {
                ImGui::Text("%+.3f, %+.3f", action->state.currentState.x, action->state.currentState.y);
            }
        };
        vector2_row("thumbstick", actions->thumbstick);
        vector2_row("trackpad",   actions->trackpad);
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("right_poses", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Pose");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        const auto pose_row = [](const char* label, const erhe::xr::Xr_action_pose* action) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (action == nullptr) {
                ImGui::TextDisabled("-");
            } else if (action->state.isActive != XR_TRUE) {
                ImGui::TextDisabled("inactive");
            } else {
                ImGui::Text(
                    "p (%+.2f, %+.2f, %+.2f) flags 0x%llx",
                    action->position.x, action->position.y, action->position.z,
                    static_cast<unsigned long long>(action->location.locationFlags)
                );
            }
        };
        pose_row("aim_pose",  actions->aim_pose);
        pose_row("grip_pose", actions->grip_pose);
        ImGui::EndTable();
    }
}

#else

void Controller_inputs_window::imgui()
{
    ImGui::TextUnformatted("OpenXR is not available in this build.");
}

#endif

}
