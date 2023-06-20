#include "erhe/imgui/imgui_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::imgui {

std::shared_ptr<spdlog::logger> log_imgui;
std::shared_ptr<spdlog::logger> log_performance;
std::shared_ptr<spdlog::logger> log_windows                 ;

void initialize_logging()
{
    log_imgui       = erhe::log::make_logger("imgui",       spdlog::level::info);
    log_performance = erhe::log::make_logger("performance", spdlog::level::info);
    log_windows     = erhe::log::make_logger("windows",     spdlog::level::info);
}

}
