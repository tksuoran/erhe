#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/log/log.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <memory>

namespace editor
{

class Frame_log_window
    : public erhe::components::Component
    , public Imgui_window
    , public erhe::log::ILog_sink

{
public:
    static constexpr std::string_view c_name {"Frame_log_window"};
    static constexpr std::string_view c_title{"Frame log"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Frame_log_window ();
    ~Frame_log_window() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    // Implements erhe::log::ILog_sink
    void write(std::string_view text) override;

    void new_frame();

    template <typename... Args>
    void log(const char* format, const Args& ... args)
    {
        write(format, fmt::make_format_args(args...));
    }

private:
    void write(const char* format, fmt::format_args args);

    std::vector<std::string> m_entries;

};

} // namespace editor
