#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
#include "erhe/log/log.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <deque>
#include <memory>
#include <vector>

namespace editor
{

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
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    // Implements erhe::log::ILog_sink
    void write(std::string_view text) override;

    // Public API
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

private:
    void frame_write(const char* format, fmt::format_args args);
    void tail_write (const char* format, fmt::format_args args);

    class Tail_entry
    {
    public:
        std::string  message;
        unsigned int repeat_count{0};
    };

    std::vector<std::string> m_frame_entries;
    std::deque<Tail_entry>   m_tail_entries;
    int                      m_tail_buffer_show_size{7};
    int                      m_tail_buffer_trim_size{1000};
};

} // namespace editor
