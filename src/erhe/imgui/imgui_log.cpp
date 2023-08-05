#include "erhe/imgui/imgui_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::imgui {

std::shared_ptr<spdlog::logger> log_imgui;
std::shared_ptr<spdlog::logger> log_performance;
std::shared_ptr<spdlog::logger> log_windows;

void initialize_logging()
{
    using namespace erhe::log;
    log_imgui       = make_logger("erhe.imgui.imgui"      );
    log_performance = make_logger("erhe.imgui.performance");
    log_windows     = make_logger("erhe.imgui.windows"    );
}

}
