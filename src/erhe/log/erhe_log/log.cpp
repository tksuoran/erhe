#include "erhe_log/log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_log/timestamp.hpp"
#include "erhe_verify/verify.hpp"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#if defined _WIN32
#   include <spdlog/sinks/msvc_sink.h>
#endif

#if defined _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif

#include <vector>

namespace erhe::log {

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
    auto get_file_sink       () -> spdlog::sinks::basic_file_sink_mt& { return *m_sink_log_file.get(); }
    auto get_tail_store_sink () -> Store_log_sink& { return *m_tail_store_log.get(); }
    auto get_frame_store_sink() -> Store_log_sink& { return *m_frame_store_log.get(); }
    auto get_log_to_console  () const -> bool { return m_log_to_console; }
    void set_log_to_console  (bool value) { m_log_to_console = value; }

    auto make_logger(const std::string& name, const bool tail) -> std::shared_ptr<spdlog::logger>
    {
        ERHE_VERIFY(!name.empty());
        const std::string groupname = get_groupname(name);
        const std::string basename  = get_basename(name);
        const erhe::configuration::Ini_section& ini = erhe::configuration::get_ini_file_section(c_logging_configuration_file_path, groupname);

        std::string levelname;
        ini.get(basename.c_str(), levelname);

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
        m_sink_log_file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt", true);

        // If you get a crash here:
        // - Install / repair latest VC redistributable from
        //   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170
        // - See also: https://github.com/gabime/spdlog/issues/3145
        m_sink_log_file->set_pattern("[%H:%M:%S %z] [%n] [%L] [%t] %v");

#if defined _WIN32
        m_sink_msvc = std::make_shared<spdlog::sinks::msvc_sink_mt>();
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
    std::shared_ptr<spdlog::sinks::basic_file_sink_mt>   m_sink_log_file  {};
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

} // namespace erhe::log

