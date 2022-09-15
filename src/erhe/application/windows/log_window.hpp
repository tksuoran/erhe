#pragma once

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/log/log.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <spdlog/sinks/sink.h>

#include <deque>
#include <memory>
#include <vector>

namespace erhe::application
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

{
public:
    static constexpr std::string_view c_type_name{"Log_window"};
    static constexpr std::string_view c_title{"Log"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Log_window ();
    ~Log_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

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
    int                             m_tail_buffer_show_size{25};
    int                             m_tail_buffer_trim_size{1000};
    bool                            m_paused{false};
};

} // namespace erhe::application
