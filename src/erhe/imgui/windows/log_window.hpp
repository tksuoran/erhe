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
    explicit Log_window_toggle_pause_command(erhe::commands::Commands& commands);

    auto try_call() -> bool override;
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
    //class Entry
    //{
    //public:
    //    Entry(
    //        const ImVec4&      color,
    //        const std::string& message,
    //        const unsigned int repeat_count = 0
    //    );
    //    ImVec4       color;
    //    std::string  timestamp;
    //    std::string  message;
    //    unsigned int repeat_count{0};
    //};

    Log_window_toggle_pause_command m_toggle_pause_command;
    int                             m_tail_buffer_show_size{10000};
    int                             m_tail_buffer_trim_size{10000};
    bool                            m_paused     {false};
    bool                            m_last_on_top{true};
};

} // namespace erhe::imgui
