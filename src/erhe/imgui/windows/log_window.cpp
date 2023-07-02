#include "erhe/imgui/windows/log_window.hpp"

#include "erhe/imgui/imgui_log.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/commands/commands.hpp"

#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/timestamp.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace erhe::imgui
{

Log_window_toggle_pause_command::Log_window_toggle_pause_command(erhe::commands::Commands& commands)
    : Command{commands, "Log_window.toggle_pause"}
{
}

auto Log_window_toggle_pause_command::try_call() -> bool
{
    reinterpret_cast<Log_window*>(get_host())->toggle_pause();
    return true;
}

Log_window::Log_window(
    erhe::commands::Commands& commands,
    Imgui_renderer&           imgui_renderer,
    Imgui_windows&            imgui_windows
)
    : Imgui_window          {imgui_renderer, imgui_windows, "Log", "log"}
    , m_toggle_pause_command{commands}
{
    m_min_size[0] = 220.0f;
    m_min_size[1] = 120.0f;

    commands.register_command   (&m_toggle_pause_command);
    commands.bind_command_to_key(&m_toggle_pause_command, erhe::toolkit::Key_escape);

    Command_host::set_description("Log_window");
    m_toggle_pause_command.set_host(this);
}

void Log_window::toggle_pause()
{
    m_paused = !m_paused;
}

void Log_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    auto& tail  = erhe::log::get_tail_store_log();
    auto& frame = erhe::log::get_frame_store_log();

    if (
        ImGui::TreeNodeEx(
            "Tail",
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_Framed
        )
    ) {
        ImGui::SetNextItemWidth(100.0f);

        ImGui::DragInt(
            "Show",
            &m_tail_buffer_show_size,
            1.0f,
            1,
            std::numeric_limits<int>::max(),
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);

        ImGui::DragInt(
            "Trim",
            &m_tail_buffer_trim_size,
            1.0f,
            1,
            std::numeric_limits<int>::max(),
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Button("Clear")) {
            tail->trim(0);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Checkbox("Paused", &m_paused)) {
            tail->set_paused(m_paused);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Checkbox("Last on Top", &m_last_on_top);

        const auto trim_size = static_cast<size_t>(m_tail_buffer_trim_size);
        tail->trim(trim_size);

        auto& tail_entries = tail->get_log();

        const auto visible_count = (std::min)(
            static_cast<size_t>(m_tail_buffer_show_size),
            tail_entries.size()
        );
        ImGui::PushFont(m_imgui_renderer.mono_font());
        if (m_last_on_top) {
            for (
                auto i = tail_entries.rbegin(),
                end = tail_entries.rbegin() + visible_count;
                i != end;
                ++i
            ) {
                auto& entry = *i;
                ImGui::SetNextItemWidth(100.0f);
                ImGui::TextColored(
                    ImVec4{0.7f, 0.7f, 0.7f, 1.0f},
                    "%s",
                    entry.timestamp.c_str()
                );
                ImGui::SameLine();
                //ImGui::TextColored(entry.color, "%s", entry.message.c_str());
                ImGui::TextUnformatted(entry.message.c_str());
                //if (entry.repeat_count > 0)
                //{
                //    ImGui::TextColored(
                //        ImVec4{0.55f, 0.55f, 0.55f, 1.0f},
                //        "Message repeated %u times",
                //        entry.repeat_count
                //    );
                //}
            }

        } else {
            for (
                auto i = tail_entries.begin(),
                end = tail_entries.begin() + visible_count;
                i != end;
                ++i
            ) {
                auto& entry = *i;
                ImGui::SetNextItemWidth(100.0f);
                ImGui::TextColored(
                    ImVec4{0.7f, 0.7f, 0.7f, 1.0f},
                    "%s",
                    entry.timestamp.c_str()
                );
                ImGui::SameLine();
                //ImGui::TextColored(entry.color, "%s", entry.message.c_str());
                ImGui::TextUnformatted(entry.message.c_str());
                //if (entry.repeat_count > 0)
                //{
                //    ImGui::TextColored(
                //        ImVec4{0.55f, 0.55f, 0.55f, 1.0f},
                //        "Message repeated %u times",
                //        entry.repeat_count
                //    );
                //}
            }
        }
        ImGui::PopFont();
        ImGui::TreePop();
    }

    if (
        ImGui::TreeNodeEx(
            "Frame",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed
        )
    ) {
        auto& frame_entries = frame->get_log();
        for (const auto& entry : frame_entries) {
            ImGui::SetNextItemWidth(100.0f);
            ImGui::TextUnformatted (entry.timestamp.c_str());
            ImGui::SameLine        ();
            ImGui::TextUnformatted (entry.message.c_str());
        }
        ImGui::TreePop();
    }
    frame->trim(0);
#endif
}

} // namespace erhe::imgui
