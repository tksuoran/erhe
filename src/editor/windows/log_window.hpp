#pragma once

#include "command.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/log/log.hpp"

#include <imgui.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <deque>
#include <memory>
#include <vector>

namespace editor
{

class Log_window;

class Log_window_toggle_pause_command
    : public Command
{
public:
    explicit Log_window_toggle_pause_command(Log_window& log_window)
        : Command     {"Log_window.toggle_pause"}
        , m_log_window{log_window}
    {
    }

    auto try_call(Command_context& context) -> bool override;

private:
    Log_window& m_log_window;
};

class Log_window
    : public erhe::components::Component
    , public Imgui_window
    , public erhe::log::ILog_sink

{
public:
    static constexpr std::string_view c_name {"Log_window"};
    static constexpr std::string_view c_title{"Log"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Log_window ();
    ~Log_window() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    // Implements erhe::log::ILog_sink
    void write(const erhe::log::Color& color, const std::string_view text) override;

    // Public API

    // Commands
    void toggle_pause();

    template <typename... Args>
    void frame_log(const char* format, const Args& ... args)
    {
        frame_write(format, fmt::make_format_args(args...));
    }

    template <typename... Args>
    void tail_log(const char* format, const Args& ... args)
    {
        tail_write(format, fmt::make_format_args(args...));
    }

    template <typename... Args>
    void tail_log(const ImVec4 color, const char* format, const Args& ... args)
    {
        tail_write(color, format, fmt::make_format_args(args...));
    }

private:
    void frame_write(const char* format, fmt::format_args args);
    void tail_write (const char* format, fmt::format_args args);
    void tail_write (const ImVec4 color, const char* format, fmt::format_args args);

    class Tail_entry
    {
    public:
        Tail_entry(
            const ImVec4&      color,
            const std::string& message,
            const unsigned int repeat_count = 0
        )
        : color       {color}
        , message     {message}
        , repeat_count{repeat_count}
        {
        }
        ImVec4       color;
        std::string  message;
        unsigned int repeat_count{0};
    };

    Log_window_toggle_pause_command m_toggle_pause_command;
    std::vector<std::string>        m_frame_entries;
    std::deque<Tail_entry>          m_tail_entries;
    int                             m_tail_buffer_show_size{7};
    int                             m_tail_buffer_trim_size{1000};
    bool                            m_paused{false};
};

} // namespace editor
