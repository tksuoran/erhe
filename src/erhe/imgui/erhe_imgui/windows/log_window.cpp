#include "erhe_imgui/windows/log_window.hpp"

#include "erhe_imgui/imgui_log.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_commands/commands.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_time/timestamp.hpp"

#include <imgui/imgui.h>
#include <mini/ini.h>

#include <algorithm>

namespace erhe::imgui {

Logs_toggle_pause_command::Logs_toggle_pause_command(erhe::commands::Commands& commands, Logs& logs)
    : Command{commands, "Logs.toggle_pause"}
    , m_logs {logs}
{
}

auto Logs_toggle_pause_command::try_call() -> bool
{
    m_logs.toggle_pause();
    return true;
}

Logs::Logs(erhe::commands::Commands& commands, Imgui_renderer& imgui_renderer)
    : m_imgui_renderer{imgui_renderer}
    , m_toggle_pause_command{commands, *this}
{
    commands.register_command   (&m_toggle_pause_command);
    commands.bind_command_to_key(&m_toggle_pause_command, erhe::window::Key_escape);

    Command_host::set_description("Logs");
    m_toggle_pause_command.set_host(this);
}

Log_settings_window::Log_settings_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs)
    : Imgui_window{imgui_renderer, imgui_windows, "Log Settings", "log_settings"}
    , m_logs{logs}
{
    m_min_size[0] = 220.0f;
    m_min_size[1] = 120.0f;
}

Tail_log_window::Tail_log_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs)
    : Imgui_window{imgui_renderer, imgui_windows, "Tail Log", "tail_log"}
    , m_logs{logs}
{
    m_min_size[0] = 220.0f;
    m_min_size[1] = 120.0f;
}

Frame_log_window::Frame_log_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs)
    : Imgui_window{imgui_renderer, imgui_windows, "Frame Log", "frame_log"}
    , m_logs{logs}
{
    m_min_size[0] = 220.0f;
    m_min_size[1] = 120.0f;
}

void Logs::toggle_pause()
{
    m_paused = !m_paused;
}

auto get_log_level_color_vec4(const spdlog::level::level_enum level) -> ImVec4
{
    switch (level) {
        case spdlog::level::level_enum::trace   : return ImVec4{0.4f, 0.6f, 1.0f, 1.0f};
        case spdlog::level::level_enum::debug   : return ImVec4{1.0f, 1.0f, 0.0f, 1.0f};
        case spdlog::level::level_enum::info    : return ImVec4{0.8f, 0.8f, 0.8f, 1.0f};
        case spdlog::level::level_enum::warn    : return ImVec4{1.0f, 0.5f, 0.0f, 1.0f};
        case spdlog::level::level_enum::err     : return ImVec4{1.0f, 0.0f, 0.0f, 1.0f};
        case spdlog::level::level_enum::critical: return ImVec4{1.0f, 0.2f, 0.2f, 1.0f};
        case spdlog::level::level_enum::off     : return ImVec4{0.4f, 0.4f, 0.4f, 1.0f};
        default:                                  return ImVec4{0.5f, 0.5f, 0.5f, 1.0f};
    }
}

auto get_log_level_color(const spdlog::level::level_enum level) -> uint32_t
{
    return ImGui::ColorConvertFloat4ToU32(get_log_level_color_vec4(level));
}

void Logs::log_entry(erhe::log::Entry& entry)
{
    if (m_paused && (entry.serial > m_pause_serial)) {
        return;
    }
    if (entry.level < m_min_level_to_show) {
        return;
    }

    ImGui::TableNextRow();
    if (ImGui::TableSetColumnIndex(0)) {
        ImGui::TextColored(ImVec4{0.7f, 0.7f, 0.7f, 1.0f}, "%s", entry.timestamp.c_str());
    }
    if (ImGui::TableSetColumnIndex(1)) {
        ImGui::TextUnformatted(entry.logger.c_str());
    }
    if (ImGui::TableSetColumnIndex(2)) {
        ImGui::PushStyleColor(ImGuiCol_Text, get_log_level_color(entry.level));
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

void Logs::save_settings()
{
    mINI::INIFile file("logging.ini");
    mINI::INIStructure ini;
    spdlog::apply_all(
        [&ini](std::shared_ptr<spdlog::logger> logger) {
            const auto& name = logger->name();
            if (name.empty()) {
                return;
            }
            const auto group_name     = erhe::log::get_groupname(name);
            const auto basename       = erhe::log::get_basename (name);
            ini[group_name][basename] = erhe::log::get_levelname(logger->level());
        }
    );
    file.generate(ini);
}

auto log_level_combo(const char* label, spdlog::level::level_enum& level) -> bool
{
    ImGui::PushStyleColor(ImGuiCol_Text, get_log_level_color(level));
    const bool is_combo_open = ImGui::BeginCombo(label, erhe::log::get_levelname(level).c_str());
    ImGui::PopStyleColor();
    if (!is_combo_open) {
        return false;
    }

    bool selection_changed = false;
    for (int i = 0; i < static_cast<int>(spdlog::level::n_levels); ++i) {
        spdlog::level::level_enum level_option = static_cast<spdlog::level::level_enum>(i);
        bool selected = level == level_option;
        ImGui::PushStyleColor(ImGuiCol_Text, get_log_level_color(level_option));
        bool item_selected = ImGui::Selectable(erhe::log::get_levelname(level_option).c_str(), &selected);
        ImGui::PopStyleColor();
        if (item_selected) {
            level = level_option;
            ImGui::SetItemDefaultFocus();
            selection_changed = true;
        }
    }
    ImGui::EndCombo();
    return selection_changed;
}

void Logs::log_settings_ui()
{
    if (ImGui::Button("Save")) {
        save_settings();
    }

    std::vector<std::shared_ptr<spdlog::logger>> loggers;
    spdlog::apply_all(
        [&loggers](std::shared_ptr<spdlog::logger> logger) {
            loggers.push_back(logger);
        }
    );

    std::sort(
        loggers.begin(),
        loggers.end(),
        [](const std::shared_ptr<spdlog::logger>& lhs, const std::shared_ptr<spdlog::logger>& rhs) {
            const auto lhs_group = erhe::log::get_groupname(lhs->name());
            const auto rhs_group = erhe::log::get_groupname(rhs->name());
            if (lhs_group != rhs_group) {
                return lhs_group < rhs_group;
            }
            return lhs->name() < rhs->name();
        }
    );

    std::string current_group{};
    bool group_open = false;
    int logger_id = 0;
    for (const auto& logger : loggers) {
        const auto& name = logger->name();
        const auto group_name = erhe::log::get_groupname(name);
        if (group_name != current_group) {
            if (group_open) {
                ImGui::TreePop();
            }
            group_open = ImGui::TreeNodeEx(group_name.c_str(), ImGuiTreeNodeFlags_Framed);
            current_group = group_name;
        }
        if (!group_open) {
            continue;
        }
        ImGui::SetNextItemWidth(100.0f);
        auto level = logger->level();

        ImGui::PushID(logger_id++);
        if (log_level_combo(erhe::log::get_basename(name).c_str(), level)) {
            logger->set_level(level);
        }
        ImGui::PopID();
    }
    if (group_open) {
        ImGui::TreePop();
    }
}

void Log_settings_window::imgui()
{
    m_logs.log_settings_ui();
}

void Tail_log_window::imgui()
{
    m_logs.tail_log_imgui();
}

void Frame_log_window::imgui()
{
    //auto& frame = erhe::log::get_frame_store_log();
    std::deque<erhe::log::Entry>& frame_entries = m_frame_entries;
    ImGui::PushFont(m_imgui_renderer.mono_font());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{0.0f, 0.0f});
    const ImVec2 outer_size{-FLT_MIN, 0.0f};
    const float  inner_width{4000.0f};  // TODO Need a better way, to make long lines horizontally scrollable
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("log_content", 3, flags, outer_size, inner_width)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("TimeStamp", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Logger",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Message",   ImGuiTableColumnFlags_WidthFixed, 4000.0f - 140.0f - 170.0f);
        ImGui::TableHeadersRow();
        for (auto& entry : frame_entries) {
            m_logs.log_entry(entry);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::PopFont();
}

void Frame_log_window::on_frame_begin()
{
}

void Frame_log_window::on_frame_end()
{
    auto& frame = erhe::log::get_frame_store_log();

    frame.access_entries(
        [this](std::deque<erhe::log::Entry>& frame_entries) {
            m_frame_entries.clear();
            std::swap(m_frame_entries, frame_entries);
        }
    );
}

void Logs::tail_log_imgui()
{
    auto& tail = erhe::log::get_tail_store_log();
    if (!ImGui::BeginTable("log_parent", 1, ImGuiTableFlags_ScrollX)) {
        return;
    }

    // Control row, which is frozen
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    if (ImGui::TableSetColumnIndex(0)) {
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragInt("Show", &m_tail_buffer_show_size, 1.0f, 1, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragInt("Trim", &m_tail_buffer_trim_size, 1.0f, 1, std::numeric_limits<int>::max(), "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Button("Clear")) {
            tail.trim(0);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Checkbox("Paused", &m_paused)) {
            m_pause_serial = tail.get_serial();
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
        log_level_combo("##LogLevel", m_min_level_to_show);
    }

    // Log content rows
    const auto trim_size = static_cast<size_t>(m_tail_buffer_trim_size);
    tail.trim(trim_size);

    tail.access_entries(
        [&](std::deque<erhe::log::Entry>& tail_entries) {
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
                const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
                if (ImGui::BeginTable("log_content", 3, flags, outer_size, inner_width)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("TimeStamp", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Logger",    ImGuiTableColumnFlags_WidthFixed, 140.0f);
                    ImGui::TableSetupColumn("Message",   ImGuiTableColumnFlags_WidthFixed, 4000.0f - 140.0f - 170.0f);
                    ImGui::TableHeadersRow();
                    if (m_last_on_top) {
                        for (auto i = tail_entries.rbegin(), end = tail_entries.rbegin() + visible_count; i != end; ++i) {
                            log_entry(*i);
                        }
                    } else {
                        for (auto i = tail_entries.begin(), end = tail_entries.begin() + visible_count; i != end; ++i) {
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
    );
}

} // namespace erhe::imgui
