#include "erhe_log/log.hpp"
#include "erhe_log/timestamp.hpp"
#include "erhe_verify/verify.hpp"

#include <simdjson.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#if defined _WIN32
#   include <spdlog/sinks/msvc_sink.h>
#endif

#if defined(ERHE_OS_ANDROID)
#   include <spdlog/sinks/android_sink.h>
#endif

#if defined _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <vector>

namespace erhe::log {

// --- Diagnostic breadcrumbs ---------------------------------------------

namespace {

constexpr std::size_t c_breadcrumb_ring_size = 32;

class Breadcrumb_state
{
public:
    std::mutex                                     mutex;
    Breadcrumb                                     current;
    std::array<Breadcrumb, c_breadcrumb_ring_size> ring;
    std::size_t                                    ring_next {0};
    std::size_t                                    ring_count{0};
    std::chrono::steady_clock::time_point          epoch{std::chrono::steady_clock::now()};
};

auto get_breadcrumb_state() -> Breadcrumb_state&
{
    static Breadcrumb_state state;
    return state;
}

} // namespace

auto breadcrumb_now_ns() -> std::int64_t
{
    Breadcrumb_state& state = get_breadcrumb_state();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - state.epoch
    ).count();
}

void set_breadcrumb(std::string_view text)
{
    Breadcrumb_state& state = get_breadcrumb_state();
    const std::size_t  thread_hash  = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::int64_t monotonic_ns = breadcrumb_now_ns();

    std::lock_guard<std::mutex> lock{state.mutex};
    // assign() reuses the string's existing capacity, so steady-state calls
    // do not allocate.
    state.current.text.assign(text);
    state.current.thread_hash  = thread_hash;
    state.current.monotonic_ns = monotonic_ns;

    Breadcrumb& slot = state.ring[state.ring_next];
    slot.text.assign(text);
    slot.thread_hash  = thread_hash;
    slot.monotonic_ns = monotonic_ns;
    state.ring_next = (state.ring_next + 1) % c_breadcrumb_ring_size;
    if (state.ring_count < c_breadcrumb_ring_size) {
        ++state.ring_count;
    }
}

auto get_current_breadcrumb() -> Breadcrumb
{
    Breadcrumb_state& state = get_breadcrumb_state();
    std::lock_guard<std::mutex> lock{state.mutex};
    return state.current;
}

auto get_recent_breadcrumbs() -> std::vector<Breadcrumb>
{
    Breadcrumb_state& state = get_breadcrumb_state();
    std::lock_guard<std::mutex> lock{state.mutex};
    std::vector<Breadcrumb> result;
    result.reserve(state.ring_count);
    // Walk oldest -> newest.
    const std::size_t start = (state.ring_count < c_breadcrumb_ring_size)
        ? 0
        : state.ring_next;
    for (std::size_t i = 0; i < state.ring_count; ++i) {
        result.push_back(state.ring[(start + i) % c_breadcrumb_ring_size]);
    }
    return result;
}

static auto get_log_level_map() -> std::unordered_map<std::string, std::string>&
{
    static std::unordered_map<std::string, std::string> map;
    return map;
}

void configure_log_levels(
    const std::vector<std::pair<std::string, std::string>>& name_level_pairs
)
{
    std::unordered_map<std::string, std::string>& map = get_log_level_map();
    for (const auto& pair : name_level_pairs) {
        map[pair.first] = pair.second;
    }

    // Update any loggers that were already created
    for (const auto& pair : name_level_pairs) {
        std::shared_ptr<spdlog::logger> logger = spdlog::get(pair.first);
        if (logger) {
            auto from_str = [](const std::string& name) -> spdlog::level::level_enum {
                auto it = std::find(
                    std::begin(spdlog::level::level_string_views),
                    std::end(spdlog::level::level_string_views),
                    name
                );
                if (it != std::end(spdlog::level::level_string_views)) {
                    return static_cast<spdlog::level::level_enum>(
                        std::distance(std::begin(spdlog::level::level_string_views), it)
                    );
                }
                if (name == "warn") return spdlog::level::warn;
                if (name == "err")  return spdlog::level::err;
                return spdlog::level::err;
            };
            logger->set_level(from_str(pair.second));
        }
    }
}

void load_log_configuration(const std::string& json_contents)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded{json_contents};
    simdjson::ondemand::document doc;
    simdjson::error_code error = parser.iterate(padded).get(doc);
    if (error) {
        return;
    }
    simdjson::ondemand::object obj;
    error = doc.get_object().get(obj);
    if (error) {
        return;
    }
    simdjson::ondemand::object loggers;
    error = obj["loggers"].get_object().get(loggers);
    if (error) {
        return;
    }
    std::vector<std::pair<std::string, std::string>> levels;
    for (auto logger_field : loggers) {
        std::string_view name;
        if (logger_field.unescaped_key().get(name) != simdjson::SUCCESS) {
            continue;
        }
        std::string_view level;
        if (logger_field.value().get_string().get(level) != simdjson::SUCCESS) {
            continue;
        }
        levels.emplace_back(std::string{name}, std::string{level});
    }
    configure_log_levels(levels);
}

void console_init()
{
#if defined _WIN32
    HWND   hwnd           = GetConsoleWindow();
    HICON  icon           = LoadIcon(NULL, MAKEINTRESOURCE(32516));
    HANDLE hConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode           = 0;
    GetConsoleMode(hConsoleHandle, &mode);
    SetConsoleMode(hConsoleHandle, (mode & ~ENABLE_MOUSE_INPUT) | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS);

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)(icon));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(icon));

    std::setlocale(LC_CTYPE, ".UTF8");
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
}

Store_log_sink::Store_log_sink()
{
}

auto Store_log_sink::get_serial() const -> uint64_t
{
    return m_serial;
}

void Store_log_sink::access_entries(std::function<void(std::deque<Entry>& entries)> op)
{
    std::lock_guard<std::mutex> lock{mutex_};
    op(m_entries);
}

void Store_log_sink::trim(const std::size_t trim_size)
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (m_entries.size() > trim_size) {
        const auto trim_count = m_entries.size() - trim_size;
        m_entries.erase(
            m_entries.begin(),
            m_entries.begin() + trim_count
        );
        assert(m_entries.size() == trim_size);
    }
}

void Store_log_sink::sink_it_(const spdlog::details::log_msg& msg)
{
    ++m_serial;
    m_entries.push_back(
        Entry{
            .serial       = m_serial,
            .timestamp    = erhe::log::timestamp_short(),
            .message      = std::string{msg.payload.begin(), msg.payload.end()},
            .logger       = std::string{msg.logger_name.begin(), msg.logger_name.end()},
            .repeat_count = 0,
            .level        = msg.level,
        }
    );
}

void Store_log_sink::flush_()
{
}

class Log_sinks
{
public:
    static Log_sinks& get_instance()
    {
        static Log_sinks static_instance;
        return static_instance;
    }

#if defined _WIN32
    auto get_msvc_sink       () -> spdlog::sinks::msvc_sink_mt& { return *m_sink_msvc.get(); }
#endif
    auto get_console_sink    () -> spdlog::sinks::stdout_color_sink_mt& { return *m_sink_console.get(); }
    auto get_file_sink       () -> spdlog::sinks::sink&                 { return *m_sink_log_file.get(); }
    auto get_tail_store_sink () -> Store_log_sink& { return *m_tail_store_log.get(); }
    auto get_frame_store_sink() -> Store_log_sink& { return *m_frame_store_log.get(); }
    auto get_log_to_console  () const -> bool { return m_log_to_console; }
    void set_log_to_console  (bool value) { m_log_to_console = value; }

    auto make_logger(const std::string& name, const bool tail) -> std::shared_ptr<spdlog::logger>
    {
        ERHE_VERIFY(!name.empty());

        std::string levelname;
        {
            const std::unordered_map<std::string, std::string>& levels = get_log_level_map();
            const auto it = levels.find(name);
            if (it != levels.end()) {
                levelname = it->second;
            }
        }

        // Forked from spdlog::level::from_str(levelname) because it defaults to off if parse fails,
        // while we want to set the default to err.
        auto from_str = [](const std::string &name) -> spdlog::level::level_enum {
            auto it = std::find(std::begin(spdlog::level::level_string_views), std::end(spdlog::level::level_string_views), name);
            if (it != std::end(spdlog::level::level_string_views))
                return static_cast<spdlog::level::level_enum>(std::distance(std::begin(spdlog::level::level_string_views), it));
            if (name == "warn") {
                return spdlog::level::warn;
            }
            if (name == "err") {
                return spdlog::level::err;
            }
            return spdlog::level::err;
        };
        const spdlog::level::level_enum level_parsed = from_str(levelname);

        std::shared_ptr<spdlog::logger> logger = std::make_shared<spdlog::logger>(
            name,
            spdlog::sinks_init_list{
#if defined _WIN32
                m_sink_msvc,
#else
                m_sink_console,
#endif
                m_sink_log_file,
                tail ? m_tail_store_log : m_frame_store_log
            }
        );
        if (m_log_to_console) {
            logger->sinks().push_back(m_sink_console);
        }
        std::shared_ptr<spdlog::logger> logger_copy = logger;
        spdlog::register_logger(logger_copy);
        logger->set_level(level_parsed);
        logger->flush_on(spdlog::level::trace);
        return logger;
    }

    void create_sinks()
    {
#if defined(ERHE_OS_ANDROID)
        // No file I/O on Android; route through __android_log_print under
        // the "erhe" tag so output shows up in `adb logcat`.
        m_sink_log_file = std::make_shared<spdlog::sinks::android_sink_mt>("erhe");
#else
        m_sink_log_file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/log.txt", true);
#endif

        // If you get a crash here:
        // - Install / repair latest VC redistributable from
        //   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170
        // - See also: https://github.com/gabime/spdlog/issues/3145
        //m_sink_log_file->set_pattern("[%H:%M:%S %z] [%n] [%L] [%t] %v");
        m_sink_log_file->set_pattern("[%H:%M:%S.%e] [%L] [%n] %v");

#if defined _WIN32
        m_sink_msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        m_sink_msvc->set_pattern("[%n] [%L] %v");
#endif
        m_sink_console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        m_tail_store_log = std::make_shared<Store_log_sink>();
        m_frame_store_log = std::make_shared<Store_log_sink>();
    }

private:
    Log_sinks()
    {
    }
    ~Log_sinks() {}

#if defined _WIN32
    std::shared_ptr<spdlog::sinks::msvc_sink_mt>         m_sink_msvc      {};
#endif
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> m_sink_console   {};
    // Holds basic_file_sink_mt on desktop, android_sink_mt on Android. Both
    // inherit from spdlog::sinks::sink so the rest of the wiring is shared.
    spdlog::sink_ptr                                     m_sink_log_file  {};
    std::shared_ptr<Store_log_sink>                      m_tail_store_log {};
    std::shared_ptr<Store_log_sink>                      m_frame_store_log{};
    bool                                                 m_log_to_console {false};
};

auto get_tail_store_log() -> Store_log_sink&
{
    return Log_sinks::get_instance().get_tail_store_sink();
}

auto get_frame_store_log() -> Store_log_sink&
{
    return Log_sinks::get_instance().get_frame_store_sink();
}

void log_to_console()
{
    Log_sinks::get_instance().set_log_to_console(true);
}

void initialize_log_sinks()
{
    Log_sinks::get_instance().create_sinks();
}

void redirect_stderr_to_file(const std::string& path)
{
#if defined(ERHE_OS_MACOS)
    // Line-buffer so a fatal validator abort does not lose the trailing
    // message to a not-yet-flushed block buffer. See the header for the
    // reason this is macOS-only.
    if (std::freopen(path.c_str(), "w", stderr) != nullptr) {
        std::setvbuf(stderr, nullptr, _IOLBF, 0);
    }
#else
    static_cast<void>(path);
#endif
}

auto get_groupname(const std::string& s) -> std::string
{
    std::string::size_type pos = s.find_last_of('.');
    if (pos != std::string::npos) {
        return s.substr(0, pos);
    } else {
        return std::string{};
    }
}

auto get_basename(const std::string& s) -> std::string
{
    std::string::size_type pos = s.find_last_of('.');
    if (pos != std::string::npos && ((pos + 1) < s.size())) {
        return s.substr(pos + 1);
    } else {
        return std::string{};
    }
}

auto get_levelname(spdlog::level::level_enum level) -> std::string
{
    const auto sv = spdlog::level::to_string_view(level);;
    return std::string{sv.begin(), sv.begin() + sv.size()};
}

auto make_frame_logger(const std::string& name) -> std::shared_ptr<spdlog::logger>
{
    return make_logger(name, false);
}

auto make_logger(const std::string& name, const bool tail) -> std::shared_ptr<spdlog::logger>
{
    return Log_sinks::get_instance().make_logger(name, tail);
}

auto make_file_logger(const std::string& name, const std::string& file_path) -> std::shared_ptr<spdlog::logger>
{
    ERHE_VERIFY(!name.empty());
    ERHE_VERIFY(!file_path.empty());

    // Share a single file sink per path so multiple loggers can target the
    // same output file without each opening the file in truncate mode and
    // clobbering the others.
    static std::mutex s_file_sink_mutex;
    static std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::basic_file_sink_mt>> s_file_sinks;
    std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink;
    {
        std::lock_guard<std::mutex> lock{s_file_sink_mutex};
        auto it = s_file_sinks.find(file_path);
        if (it == s_file_sinks.end()) {
            file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_path, true);
            file_sink->set_pattern("[%H:%M:%S.%e] [%L] [%n] %v");
            s_file_sinks.emplace(file_path, file_sink);
        } else {
            file_sink = it->second;
        }
    }

    // Build on top of the standard make_logger so the result has the full
    // set of shared sinks (console / MSVC / main log.txt / store). The
    // dedicated per-file sink is appended so the output lands in its own
    // file AND in all the usual places.
    std::shared_ptr<spdlog::logger> logger = Log_sinks::get_instance().make_logger(name, /*tail=*/true);
    logger->sinks().push_back(file_sink);
    return logger;
}

} // namespace erhe::log

