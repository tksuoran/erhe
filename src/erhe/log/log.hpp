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
void log_to_console();

// creates and configures logger
auto make_logger(
    std::string               name,
    spdlog::level::level_enum level,
    bool                      tail = true
) -> std::shared_ptr<spdlog::logger>;

class Entry
{
public:
    uint64_t                  serial;
    bool                      selected{false};
    std::string               timestamp;
    std::string               message;
    std::string               logger;
    unsigned int              repeat_count{0};
    spdlog::level::level_enum level       {2/*spdlog::level::level_enum::SPDLOG_LEVEL_INFO*/};
};

// Sink that keeps log entries in deqeue
class store_log_sink final
    : public spdlog::sinks::base_sink<std::mutex>
{
public:
    store_log_sink();

    [[nodiscard]] auto get_serial() const -> uint64_t;
    [[nodiscard]] auto get_log   () -> std::deque<Entry>&;
    void trim(std::size_t count);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_  ()                                    override;

private:
    uint64_t          m_serial{0};
    std::deque<Entry> m_entries;
};

auto get_tail_store_log () -> const std::shared_ptr<store_log_sink>&;
auto get_frame_store_log() -> const std::shared_ptr<store_log_sink>&;

} // namespace erhe::log

