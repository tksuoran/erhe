#pragma once

#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace erhe::log {

void console_init();
void initialize_log_sinks();
void log_to_console();
void configure_log_levels(const std::vector<std::pair<std::string, std::string>>& name_level_pairs);

// Redirect the process's stderr to the given file path (line-buffered) on
// platforms where stderr would otherwise be awkward to capture from a GUI
// launch -- today that means macOS, where Metal API validation and MoltenVK
// diagnostics write directly to stderr rather than through spdlog. No-op on
// other platforms. Best-effort: failure to open the file is silently
// ignored, so this is safe to call unconditionally from application startup.
void redirect_stderr_to_file(const std::string& path);

// Parse JSON string with {"loggers":[{"name":"...","level":"..."},...]} and apply log levels.
// The caller is responsible for reading the file contents (e.g. via erhe::file::read()).
void load_log_configuration(const std::string& json_contents);

// creates and configures logger
auto make_logger(const std::string& name, bool tail = true) -> std::shared_ptr<spdlog::logger>;
auto make_frame_logger(const std::string& name) -> std::shared_ptr<spdlog::logger>;

// Creates a logger that writes to the given file IN ADDITION to the shared
// sinks used by make_logger (console / MSVC debug / main log.txt / store).
// Useful when a high-volume diagnostic stream (e.g. Vulkan barrier traces)
// should get its own file but still show up alongside the rest of the logs.
auto make_file_logger(const std::string& name, const std::string& file_path) -> std::shared_ptr<spdlog::logger>;

class Entry
{
public:
    uint64_t                  serial  {0};
    bool                      selected{false};
    std::string               timestamp;
    std::string               message;
    std::string               logger;
    unsigned int              repeat_count{0};
    spdlog::level::level_enum level       {2/*spdlog::level::level_enum::SPDLOG_LEVEL_INFO*/};
};

// Sink that keeps log entries in deqeue
class Store_log_sink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    Store_log_sink();

    [[nodiscard]] auto get_serial() const -> uint64_t;
    void trim(std::size_t count);
    void access_entries(std::function<void(std::deque<Entry>& entries)> op);

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_  ()                                    override;

private:
    uint64_t           m_serial{0};
    std::deque<Entry>  m_entries;
};

// --- Diagnostic breadcrumbs ---------------------------------------------
//
// A lightweight, mutex-guarded record of the most recent named execution
// phase, plus a small ring of recent phases (with thread id and a monotonic
// timestamp). Intended for a watchdog thread to report WHERE a stalled or
// CPU-spinning thread last was: the spinning thread cannot log for itself
// (it never returns to a logging point), so the watchdog reads the last
// breadcrumb and the recent ring instead. See
// doc/intermittent_main_loop_hang.md.
//
// set_breadcrumb() is cheap (one uncontended mutex lock; the current text
// reuses its buffer) and safe to call from any thread.
class Breadcrumb
{
public:
    std::string  text;
    std::size_t  thread_hash {0};  // std::hash<std::thread::id>
    std::int64_t monotonic_ns{0};  // steady_clock since process start-ish
};

void set_breadcrumb         (std::string_view text);
[[nodiscard]] auto get_current_breadcrumb() -> Breadcrumb;
[[nodiscard]] auto get_recent_breadcrumbs() -> std::vector<Breadcrumb>;
// steady_clock "now" in the same units as Breadcrumb::monotonic_ns, so a
// watchdog can compute how long the current breadcrumb has been stuck.
[[nodiscard]] auto breadcrumb_now_ns      () -> std::int64_t;

[[nodiscard]] auto get_tail_store_log () -> Store_log_sink&;
[[nodiscard]] auto get_frame_store_log() -> Store_log_sink&;
[[nodiscard]] auto get_groupname      (const std::string& s) -> std::string;
[[nodiscard]] auto get_basename       (const std::string& s) -> std::string;
[[nodiscard]] auto get_levelname      (spdlog::level::level_enum level) -> std::string;

} // namespace erhe::log

