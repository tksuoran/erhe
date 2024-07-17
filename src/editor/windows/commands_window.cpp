#include "windows/commands_window.hpp"

#include "editor_context.hpp"

#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/key_binding.hpp"
#include "erhe_commands/mouse_button_binding.hpp"
#include "erhe_commands/mouse_drag_binding.hpp"
#include "erhe_commands/mouse_wheel_binding.hpp"
#include "erhe_commands/update_binding.hpp"
#include "erhe_window/window_event_handler.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe_commands/xr_boolean_binding.hpp"
#   include "erhe_commands/xr_float_binding.hpp"
#   include "erhe_commands/xr_vector2f_binding.hpp"
#   include "erhe_xr/xr_action.hpp"
#endif

#include <fmt/format.h>

namespace editor {

Commands_window::Commands_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Commands", "commands"}
    , m_context                {editor_context}
{
}

void Commands_window::imgui()
{
    using namespace erhe::commands;

    const auto* host = get_imgui_host();
    if (host->want_capture_keyboard()) {
        ImGui::TextUnformatted("ImGui Want Capture Keyboard");
    }
    if (host->want_capture_mouse()) {
        ImGui::TextUnformatted("ImGui Want Capture Mouse");
    }

    auto& commands = *m_context.commands;
    auto* active_mouse_command = commands.get_active_mouse_command();
    ImGui::Text(
        "Active mouse command: %s",
        (active_mouse_command != nullptr) ? active_mouse_command->get_name() : "(none)"
    );

    if (ImGui::TreeNodeEx("Commands", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNodeEx("Active", ImGuiTreeNodeFlags_DefaultOpen)) {
            filtered_commands(State::Active);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Ready", ImGuiTreeNodeFlags_DefaultOpen)) {
            filtered_commands(State::Ready);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Inactive", ImGuiTreeNodeFlags_DefaultOpen)) {
            filtered_commands(State::Inactive);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Disabled", ImGuiTreeNodeFlags_DefaultOpen)) {
            filtered_commands(State::Disabled);
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Registered Commands")) {
        for (const auto& command : commands.get_commands()) {
            ImGui::Text("%d %s", command->get_priority(), command->get_name());
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Key Bindings")) {
        for (const auto& binding : commands.get_key_bindings()) {
            ImGui::Text(
                "%d/%d %s %s <- %s %s %s",
                binding.get_command()->get_priority(),
                commands.get_command_priority(binding.get_command()),
                binding.is_command_host_enabled() ? "Enabled" : "Disabled",
                binding.get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())],
                erhe::window::c_str(binding.get_keycode()),
                binding.get_pressed() ? "pressed" : "released"
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Update Bindings")) {
        for (const auto& binding : commands.get_update_bindings()) {
            ImGui::Text(
                "%d/%d %s %s <- %s",
                binding.get_command()->get_priority(),
                commands.get_command_priority(binding.get_command()),
                binding.is_command_host_enabled() ? "Enabled" : "Disabled",
                binding.get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Mouse Bindings")) {
        for (const auto& binding : commands.get_mouse_bindings()) {
            const auto type = binding->get_type();
            switch (type) {
                default: {
                    break;
                }
                case Command_binding::Type::Mouse_button: {
                    const auto click_binding = static_cast<erhe::commands::Mouse_button_binding*>(binding.get());
                    ImGui::Text(
                        "%d/%d %s %s <- %s %d",
                        binding->get_command()->get_priority(),
                        commands.get_command_priority(binding->get_command()),
                        binding->is_command_host_enabled() ? "Enabled" : "Disabled",
                        binding->get_command()->get_name(),
                        Command_binding::c_type_strings[static_cast<int>(binding->get_type())],
                        click_binding->get_button()
                    );
                    break;
                }
                case Command_binding::Type::Mouse_drag: {
                    const auto drag_binding = static_cast<Mouse_drag_binding*>(binding.get());
                    ImGui::Text(
                        "%d %s <- %s %d",
                        binding->get_command()->get_priority(),
                        binding->get_command()->get_name(),
                        Command_binding::c_type_strings[static_cast<int>(binding->get_type())],
                        drag_binding->get_button()
                    );
                    break;
                }
                case Command_binding::Type::Mouse_motion: {
                    ImGui::Text(
                        "%d/%d %s ,_ %s",
                        binding->get_command()->get_priority(),
                        commands.get_command_priority(binding->get_command()),
                        binding->get_command()->get_name(),
                        Command_binding::c_type_strings[static_cast<int>(binding->get_type())]
                    );
                    break;
                }
            }
        }
        for (const auto& binding : commands.get_mouse_wheel_bindings()) {
            ImGui::Text(
                "%d/%d %s <- %s",
                binding->get_command()->get_priority(),
                commands.get_command_priority(binding->get_command()),
                binding->get_command()->get_name(),
                Command_binding::c_type_strings[static_cast<int>(binding->get_type())]
            );
        }
        ImGui::TreePop();
    }

#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (ImGui::TreeNodeEx("OpenXR boolean action Bindings")) {
        for (const auto& binding : commands.get_xr_boolean_bindings()) {
            ImGui::Text(
                "%d/%d %s %s <- %s %s",
                binding.get_command()->get_priority(),
                commands.get_command_priority(binding.get_command()),
                binding.is_command_host_enabled() ? "Enabled" : "Disabled",
                binding.get_command()->get_name(),
                binding.xr_action->name.c_str(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("OpenXR float action Bindings", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& binding : commands.get_xr_float_bindings()) {
            ImGui::Text(
                "%d/%d %s %s: <- %s %s",
                binding.get_command()->get_priority(),
                commands.get_command_priority(binding.get_command()),
                binding.is_command_host_enabled() ? "Enabled" : "Disabled",
                binding.get_command()->get_name(),
                binding.xr_action->name.c_str(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("OpenXR vector2f action Bindings", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& binding : commands.get_xr_vector2f_bindings()) {
            ImGui::Text(
                "%d/%d %s %s: <- %s %s",
                binding.get_command()->get_priority(),
                commands.get_command_priority(binding.get_command()),
                binding.is_command_host_enabled() ? "Enabled" : "Disabled",
                binding.get_command()->get_name(),
                binding.xr_action->name.c_str(),
                Command_binding::c_type_strings[static_cast<int>(binding.get_type())]
            );
        }
        ImGui::TreePop();
    }
#endif // defined(ERHE_XR_LIBRARY_OPENXR)
}

void Commands_window::filtered_commands(const erhe::commands::State filter)
{
    const auto& commands = m_context.commands->get_commands();

    for (auto* command : commands) {
        if (command->get_command_state() == filter) {
            const auto* host = command->get_host();
            const std::string label = fmt::format(
                "{}/{} {} : {} {} {}",
                command->get_priority(),
                m_context.commands->get_command_priority(command),
                command->get_name(),
                host ? host->get_description() : "",
                host ? host->get_priority() : 0,
                host ? (host->is_enabled() ? "Enabled" : "Disabled") : "No host"
            );
            ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Leaf);
        }
    }
}

} // namespace editor
