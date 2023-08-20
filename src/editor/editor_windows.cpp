#include "editor_windows.hpp"
#include "editor_context.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_viewport.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"

namespace editor {

Editor_windows::Editor_windows(Editor_context& context)
    : m_context{context}
{
}

void Editor_windows::builtin_imgui_window_menu()
{
    if (ImGui::BeginMenu("ImGui")) {
        ImGui::MenuItem("Demo",             "", &m_imgui_builtin_windows.demo);
        ImGui::MenuItem("Style Editor",     "", &m_imgui_builtin_windows.style_editor);
        ImGui::MenuItem("Metrics/Debugger", "", &m_imgui_builtin_windows.metrics);
        ImGui::MenuItem("Stack Tool",       "", &m_imgui_builtin_windows.stack_tool);
        ImGui::EndMenu();
    }
}

void Editor_windows::viewport_menu(erhe::imgui::Imgui_viewport& imgui_viewport)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});

    if (ImGui::BeginMainMenuBar()) {

        if (ImGui::BeginMenu("Window")) {

            m_context.imgui_windows->window_menu_entries(imgui_viewport);

            ImGui::Separator();

            builtin_imgui_window_menu();

            ImGui::Separator();

            const auto& windows = m_context.imgui_windows->get_windows();
            if (ImGui::MenuItem("Close All")) {
                for (const auto& window : windows) {
                    window->hide();
                }
            }
            if (ImGui::MenuItem("Open All")) {
                for (const auto& window : windows) {
                    window->show();
                }
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar();

    if (m_imgui_builtin_windows.demo) {
        ImGui::ShowDemoWindow(&m_imgui_builtin_windows.demo);
    }

    if (m_imgui_builtin_windows.style_editor) {
        ImGui::Begin("Dear ImGui Style Editor", &m_imgui_builtin_windows.style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    if (m_imgui_builtin_windows.metrics) {
        ImGui::ShowMetricsWindow(&m_imgui_builtin_windows.metrics);
    }

    if (m_imgui_builtin_windows.stack_tool) {
        ImGui::ShowStackToolWindow(&m_imgui_builtin_windows.stack_tool);
    }
}

} // namespace editor
