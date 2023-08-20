#include "erhe_log/log.hpp"
#include "erhe_configuration/configuration.hpp"
//#include "erhe_hash/hash.hpp"
#include "erhe_time/timestamp.hpp"
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

namespace erhe::log
{

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

auto Store_log_sink::get_log() -> std::deque<Entry>&
{
    return m_entries;
}

void Store_log_sink::trim(const std::size_t trim_size)
{
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
            .timestamp    = erhe::time::timestamp_short(),
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

namespace {

#if defined _WIN32
std::shared_ptr<spdlog::sinks::msvc_sink_mt>         sink_msvc;
#endif
std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> sink_console;
std::shared_ptr<spdlog::sinks::basic_file_sink_mt>   sink_log_file;
std::shared_ptr<Store_log_sink>                      tail_store_log;
std::shared_ptr<Store_log_sink>                      frame_store_log;
bool s_log_to_console{false};

}

auto get_tail_store_log() -> const std::shared_ptr<Store_log_sink>&
{
    return tail_store_log;
}
auto get_frame_store_log() -> const std::shared_ptr<Store_log_sink>&
{
    return frame_store_log;
}

void log_to_console()
{
    s_log_to_console = true;
}

void initialize_log_sinks()
{
#if defined _WIN32
    sink_msvc       = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#endif
    sink_console    = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink_log_file   = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt", true);
    tail_store_log  = std::make_shared<Store_log_sink>();
    frame_store_log = std::make_shared<Store_log_sink>();

    sink_log_file->set_pattern("[%H:%M:%S %z] [%n] [%L] [%t] %v");
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

auto make_logger(
    const std::string& name,
    const bool         tail
) -> std::shared_ptr<spdlog::logger>
{
    ERHE_VERIFY(!name.empty());
    const auto groupname = get_groupname(name);
    const auto basename  = get_basename(name);
    auto ini = erhe::configuration::get_ini("logging.ini", groupname.c_str());
    std::string levelname;
    ini->get(basename.c_str(), levelname);
    const spdlog::level::level_enum level_parsed = spdlog::level::from_str(levelname);

    auto logger = std::make_shared<spdlog::logger>(
        name,
        spdlog::sinks_init_list{
#if defined _WIN32
            sink_msvc,
#endif
            //sink_console,
            sink_log_file,
            tail ? tail_store_log : frame_store_log
        }
    );
    if (s_log_to_console) {
        logger->sinks().push_back(sink_console);
    }
    spdlog::register_logger(logger);
    logger->set_level(level_parsed);
    logger->flush_on(spdlog::level::trace);
    return logger;
}

} // namespace erhe::log

