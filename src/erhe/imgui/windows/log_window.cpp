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

static constexpr const char* c_log_level_names[] = {
    "trace",
    "debug",
    "info",
    "warning",
    "error",
    "critical",
    "off"
};

Log_window_toggle_pause_command::Log_window_toggle_pause_command(
    erhe::commands::Commands& commands,
    Log_window&               log_window
)
    : Command     {commands, "Log_window.toggle_pause"}
    , m_log_window{log_window}
{
}

auto Log_window_toggle_pause_command::try_call() -> bool
{
    m_log_window.toggle_pause();
    return true;
}

Log_window::Log_window(
    erhe::commands::Commands& commands,
    Imgui_renderer&           imgui_renderer,
    Imgui_windows&            imgui_windows
)
    : Imgui_window          {imgui_renderer, imgui_windows, "Log", "log"}
    , m_toggle_pause_command{commands, *this}
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

auto Log_window::get_color(const spdlog::level::level_enum level) const -> unsigned int
{
    switch (level) {
        case spdlog::level::level_enum::trace   : return ImGui::ColorConvertFloat4ToU32(ImVec4{0.4f, 0.6f, 1.0f, 1.0f});
        case spdlog::level::level_enum::debug   : return ImGui::ColorConvertFloat4ToU32(ImVec4{1.0f, 1.0f, 0.0f, 1.0f});
        case spdlog::level::level_enum::info    : return ImGui::ColorConvertFloat4ToU32(ImVec4{0.8f, 0.8f, 0.8f, 1.0f});
        case spdlog::level::level_enum::warn    : return ImGui::ColorConvertFloat4ToU32(ImVec4{1.0f, 0.5f, 0.0f, 1.0f});
        case spdlog::level::level_enum::err     : return ImGui::ColorConvertFloat4ToU32(ImVec4{1.0f, 0.0f, 0.0f, 1.0f});
        case spdlog::level::level_enum::critical: return ImGui::ColorConvertFloat4ToU32(ImVec4{1.0f, 0.2f, 0.2f, 1.0f});
        case spdlog::level::level_enum::off     : return ImGui::ColorConvertFloat4ToU32(ImVec4{0.4f, 0.4f, 0.4f, 1.0f});
        default:                                  return ImGui::ColorConvertFloat4ToU32(ImVec4{0.5f, 0.5f, 0.5f, 1.0f});
    }
}

void Log_window::log_entry(erhe::log::Entry& entry)
{
    if (m_paused && (entry.serial > m_pause_serial)) {
        return;
    }
    if (entry.level < m_min_level_to_show) {
        return;
    }

    ImGui::TableNextRow();
    if (ImGui::TableSetColumnIndex(0)) {
        ImGui::TextColored(
            ImVec4{0.7f, 0.7f, 0.7f, 1.0f},
            "%s",
            entry.timestamp.c_str()
        );
    }
    if (ImGui::TableSetColumnIndex(1)) {
        ImGui::TextUnformatted(entry.logger.c_str());
    }
    if (ImGui::TableSetColumnIndex(2)) {
        ImGui::PushStyleColor(ImGuiCol_Text, get_color(entry.level));
        ImGui::PushID(static_cast<int>(entry.serial));
        if (ImGui::Selectable(entry.message.c_str(), entry.selected)) {
            entry.selected = !entry.selected;
        }
        //if (entry.repeat_count > 0)
        //{
        //    ImGui::TextColored(
        //        ImVec4{0.55f, 0.55f, 0.55f, 1.0f},
        //        "Message repeated %u times",
        //        entry.repeat_count
        //    );
        //}
        ImGui::PopID();
        ImGui::PopStyleColor();
        if (!m_last_on_top && m_follow && !m_paused) {
            ImGui::SetScrollHereY();
        }
    }
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
        if (ImGui::BeginTable("log_parent", 1, ImGuiTableFlags_ScrollX)) {
            // Control row, which is frozen
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0)) {
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
                    m_pause_serial = tail->get_serial();
                    //tail->set_paused(m_paused);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::Checkbox("Last on Top", &m_last_on_top);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::Checkbox("Follow", &m_follow);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                ImGui::Combo(
                    "##LogLevel",
                    reinterpret_cast<int*>(&m_min_level_to_show),
                    c_log_level_names,
                    IM_ARRAYSIZE(c_log_level_names),
                    IM_ARRAYSIZE(c_log_level_names)
                );
            }

            // Log content rows
            const auto trim_size = static_cast<size_t>(m_tail_buffer_trim_size);
            tail->trim(trim_size);

            auto& tail_entries = tail->get_log();

            const auto visible_count = (std::min)(
                static_cast<size_t>(m_tail_buffer_show_size),
                tail_entries.size()
            );
            ImGui::TableNextRow();
            if (ImGui::TableSetColumnIndex(0)) {
                ImGui::PushFont(m_imgui_renderer.mono_font());
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
                const ImVec2 outer_size{-FLT_MIN, 0.0f};
                const float  inner_width{4000.0f};  // TODO Need a better way, to make long lines horizontally scrollable
                if (ImGui::BeginTable("log_content", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, outer_size, inner_width)) {
                    ImGui::TableSetupColumn("TimeStamp", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Logger",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
                    ImGui::TableSetupColumn("Message",   ImGuiTableColumnFlags_WidthFixed, 4000.0f - 140.0f - 170.0f);

                    if (m_last_on_top) {
                        for (
                            auto i = tail_entries.rbegin(),
                            end = tail_entries.rbegin() + visible_count;
                            i != end;
                            ++i
                        ) {
                            log_entry(*i);
                        }
                    } else {
                        for (
                            auto i = tail_entries.begin(),
                            end = tail_entries.begin() + visible_count;
                            i != end;
                            ++i
                        ) {
                            log_entry(*i);
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();
                ImGui::PopFont();
            }
            ImGui::EndTable();
        }
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
