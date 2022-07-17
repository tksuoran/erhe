#pragma once

#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <deque>
#include <memory>

namespace erhe::log
{

void console_init();
void initialize_log_sinks();

// creates and configures logger
auto make_logger(
    std::string               name,
    spdlog::level::level_enum level,
    bool                      tail = true
) -> std::shared_ptr<spdlog::logger>;

// Sink that keeps log entries in deqeue
class store_log_sink final
    : public spdlog::sinks::base_sink<std::mutex>
{
public:
    class Entry
    {
    public:
        std::string               timestamp;
        std::string               message;
        unsigned int              repeat_count{0};
        spdlog::level::level_enum level;
    };

    store_log_sink();

    [[nodiscard]] auto get_log() const -> const std::deque<Entry>&;
    void trim      (size_t count);
    void set_paused(bool paused);
    auto is_paused () const -> bool;

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_  ()                                    override;

private:
    std::deque<Entry> m_entries;
    bool              m_is_paused{false};
};

auto get_tail_store_log () -> const std::shared_ptr<store_log_sink>&;
auto get_frame_store_log() -> const std::shared_ptr<store_log_sink>&;

} // namespace erhe::log

