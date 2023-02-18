#include "erhe/application/windows/log_window.hpp"

#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/timestamp.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <algorithm>

namespace erhe::application
{

Log_window_toggle_pause_command::Log_window_toggle_pause_command()
    : Command{"Log_window.toggle_pause"}
{
}

auto Log_window_toggle_pause_command::try_call() -> bool
{
    g_log_window->toggle_pause();
    return true;
}

Log_window* g_log_window{nullptr};

Log_window::Log_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Log_window::~Log_window() noexcept
{
    ERHE_VERIFY(g_log_window == nullptr);
}

void Log_window::deinitialize_component()
{
    ERHE_VERIFY(g_log_window == this);
    m_toggle_pause_command.set_host(nullptr);
    g_log_window = nullptr;
}

void Log_window::declare_required_components()
{
    require<Imgui_windows>();
    require<Commands     >();
}

void Log_window::initialize_component()
{
    ERHE_VERIFY(g_log_window == nullptr);
    g_imgui_windows->register_imgui_window(this, "log");
    m_min_size[0] = 220.0f;
    m_min_size[1] = 120.0f;

    g_commands->register_command   (&m_toggle_pause_command);
    g_commands->bind_command_to_key(&m_toggle_pause_command, erhe::toolkit::Key_escape);

    Command_host::set_description("Log_window");
    m_toggle_pause_command.set_host(this);

    g_log_window = this;
}

void Log_window::toggle_pause()
{
    m_paused = !m_paused;
}

void Log_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

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
        ImGui::PushFont(g_imgui_renderer->mono_font());
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

} // namespace erhe::application
