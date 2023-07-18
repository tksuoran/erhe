#pragma once

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/log/log.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <spdlog/sinks/sink.h>

#include <deque>
#include <vector>

namespace erhe::imgui
{

class Imgui_windows;
class Log_window;

class Log_window_toggle_pause_command
    : public erhe::commands::Command
{
public:
    Log_window_toggle_pause_command(
        erhe::commands::Commands& commands,
        Log_window&               log_window
    );

    auto try_call() -> bool override;

private:
    Log_window& m_log_window;
};

class Log_window
    : public erhe::commands::Command_host
    , public Imgui_window
{
public:
    Log_window(
        erhe::commands::Commands& commands,
        Imgui_renderer&           imgui_renderer,
        Imgui_windows&            imgui_windows
    );

    // Implements Imgui_window
    void imgui() override;

    // Commands
    void toggle_pause();

private:
    [[nodiscard]] auto get_color(spdlog::level::level_enum level) const -> unsigned int;
    void log_entry(erhe::log::Entry& entry);

    Log_window_toggle_pause_command m_toggle_pause_command;
    int                             m_tail_buffer_show_size{10000};
    int                             m_tail_buffer_trim_size{10000};
    bool                            m_paused           {false};
    bool                            m_last_on_top      {false};
    bool                            m_follow           {true};
    uint64_t                        m_pause_serial     {0};
    int                             m_min_level_to_show{SPDLOG_LEVEL_TRACE};
};

} // namespace erhe::imgui
