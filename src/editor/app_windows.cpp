#include "app_windows.hpp"
#include "app_context.hpp"
#include "app_rendering.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/menu_binding.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_window/window.hpp"

namespace editor {

App_windows::App_windows(
    App_context&              context,
    erhe::commands::Commands& commands
)
    : m_context{context}
    , m_renderdoc_capture_command{commands, "RenderDoc.Capture", [this]() -> bool { renderdoc_capture(); return true; } }
{
    commands.register_command(&m_renderdoc_capture_command);

    if (context.renderdoc) {
        commands.bind_command_to_menu(&m_renderdoc_capture_command, "Developer.RenderDoc Capture");
        commands.bind_command_to_key(&m_renderdoc_capture_command, erhe::window::Key_f9); //, true, erhe::window::Key_modifier_bit_ctrl);
    }
}

void App_windows::renderdoc_capture()
{
    if (m_context.renderdoc) {
//  #if defined(ERHE_XR_LIBRARY_OPENXR)
//          if (m_context.OpenXR && m_context.headset_view->is_active()) {
//              m_context.app_rendering->request_renderdoc_capture();
//          }
//  #endif
        m_context.app_rendering->trigger_capture();
    }
}

void App_windows::builtin_imgui_window_menu()
{
    if (ImGui::BeginMenu("ImGui")) {
        ImGui::MenuItem("Demo",             "", &m_imgui_builtin_windows.demo);
        ImGui::MenuItem("Metrics/Debugger", "", &m_imgui_builtin_windows.metrics);
        ImGui::MenuItem("Debug Log",        "", &m_imgui_builtin_windows.debug_log);
        ImGui::MenuItem("ID Stack Tool",    "", &m_imgui_builtin_windows.id_stack_tool);
        ImGui::MenuItem("About",            "", &m_imgui_builtin_windows.about);
        ImGui::MenuItem("Style Editor",     "", &m_imgui_builtin_windows.style_editor);
        ImGui::MenuItem("User Guide",       "", &m_imgui_builtin_windows.user_guide);
        ImGui::EndMenu();
    }
}

void App_windows::viewport_menu(erhe::imgui::Imgui_host& imgui_host)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{10.0f, 2.0f});

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                int64_t timestamp_ns = 0; // TODO
                m_context.context_window->handle_window_close_event(timestamp_ns);
            }
            ImGui::EndMenu();
        }
        if (m_context.developer_mode) {
            if (ImGui::BeginMenu("Developer")) {
                m_context.imgui_windows->window_menu_entries(imgui_host, true);
                ImGui::Separator();

                if (m_context.renderdoc) {
#if defined(ERHE_XR_LIBRARY_OPENXR)
                    if (m_context.OpenXR && m_context.headset_view->is_active()) {
                        if (ImGui::MenuItem("Make OpenXR RenderDoc Capture")) {
                            m_context.app_rendering->request_renderdoc_capture();
                        }
                    }
#endif
                }
                ImGui::EndMenu();
            }
        }

        const auto& menu_bindings = m_context.commands->get_menu_bindings();
        for (const erhe::commands::Menu_binding& menu_binding : menu_bindings) {
            const std::string& menu_path = menu_binding.get_menu_path();
            std::size_t path_end = menu_path.length();
            std::size_t offset = 0;
            std::vector<std::string> menu_path_entries;
            while (true) {
                std::size_t separator_position = menu_path.find_first_of('.', offset);
                if (separator_position == std::string::npos) {
                    separator_position = path_end;
                }
                std::size_t span_length = separator_position - offset;
                if (span_length > 1) {
                    const std::string menu_label = menu_path.substr(offset, span_length);
                    menu_path_entries.emplace_back(menu_label);
                }
                offset = separator_position + 1;
                if (offset >= path_end) {
                    break;
                }
            }
            bool activate = false;
            size_t begin_menu_count = 0;
            for (size_t i = 0, end = menu_path_entries.size(); i < end; ++i) {
                const std::string& label = menu_path_entries[i];
                if (i == end - 1) {
                    activate = ImGui::MenuItem(label.c_str());
                } else {
                    if (!ImGui::BeginMenu(label.c_str())) {
                        break;
                    }
                    ++begin_menu_count;
                }
            }
            for (size_t i = 0; i < begin_menu_count; ++i) {
                ImGui::EndMenu();
            }
            if (activate) {
                erhe::commands::Command* command = menu_binding.get_command();
                if (command != nullptr) {
                    if (command->is_enabled()) {
                        command->try_call();
                    }
                }
            }
        }

        if (ImGui::BeginMenu("Window")) {
            m_context.imgui_windows->window_menu_entries(imgui_host, false);

            ImGui::Separator();

            builtin_imgui_window_menu();

            ImGui::Separator();

            const auto& windows = m_context.imgui_windows->get_windows();
            if (ImGui::MenuItem("Close All")) {
                for (const auto& window : windows) {
                    window->hide_window();
                }
            }
            if (ImGui::MenuItem("Open All")) {
                for (const auto& window : windows) {
                    window->show_window();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);

    if (m_imgui_builtin_windows.demo) {
        ImGui::ShowDemoWindow(&m_imgui_builtin_windows.demo);
    }

    if (m_imgui_builtin_windows.metrics) {
        ImGui::ShowMetricsWindow(&m_imgui_builtin_windows.metrics);
    }

    if (m_imgui_builtin_windows.debug_log) {
        ImGui::ShowDebugLogWindow(&m_imgui_builtin_windows.debug_log);
    }

    if (m_imgui_builtin_windows.id_stack_tool) {
        ImGui::ShowIDStackToolWindow(&m_imgui_builtin_windows.id_stack_tool);
    }

    if (m_imgui_builtin_windows.about) {
        ImGui::ShowAboutWindow(&m_imgui_builtin_windows.about);
    }

    if (m_imgui_builtin_windows.style_editor) {
        ImGui::Begin("Dear ImGui Style Editor", &m_imgui_builtin_windows.style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
}

} // namespace editor
