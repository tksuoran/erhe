#pragma once

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/log/log.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <spdlog/sinks/sink.h>

#include <deque>
#include <vector>

namespace spdlog::level {
    enum level_enum : int;
}
namespace erhe::imgui
{

class Imgui_windows;
class Logs;

class Logs_toggle_pause_command
    : public erhe::commands::Command
{
public:
    Logs_toggle_pause_command(
        erhe::commands::Commands& commands,
        Logs&                     logs
    );

    auto try_call() -> bool override;

private:
    Logs& m_logs;
};

class Logs
    : public erhe::commands::Command_host
{
public:
    Logs(erhe::commands::Commands& commands, Imgui_renderer& imgui_renderer);

    // Commands
    void toggle_pause();

    // Public API
    void tail_log_imgui ();
    void frame_log_imgui();
    void log_settings_ui();

private:
    void save_settings();
    void log_entry(erhe::log::Entry& entry);

    Imgui_renderer&           m_imgui_renderer;
    Logs_toggle_pause_command m_toggle_pause_command;
    int                       m_tail_buffer_show_size{10000};
    int                       m_tail_buffer_trim_size{10000};
    bool                      m_paused           {false};
    bool                      m_last_on_top      {false};
    bool                      m_follow           {false};
    uint64_t                  m_pause_serial     {0};
    spdlog::level::level_enum m_min_level_to_show{spdlog::level::trace};
};

class Log_settings_window : public Imgui_window
{
public:
    Log_settings_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs);
    void imgui() override;

private:
    Logs& m_logs;
};

class Tail_log_window : public Imgui_window
{
public:
    Tail_log_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs);
    void imgui() override;

private:
    Logs& m_logs;
};

class Frame_log_window : public Imgui_window
{
public:
    Frame_log_window(Imgui_renderer& imgui_renderer, Imgui_windows& imgui_windows, Logs& logs);
    void imgui() override;

private:
    Logs& m_logs;
};


[[nodiscard]] auto get_log_level_color(spdlog::level::level_enum level) -> uint32_t;

} // namespace erhe::imgui
