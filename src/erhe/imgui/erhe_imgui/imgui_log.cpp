#include "erhe_imgui/imgui_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::imgui {

std::shared_ptr<spdlog::logger> log_imgui;
std::shared_ptr<spdlog::logger> log_performance;
std::shared_ptr<spdlog::logger> log_windows;
std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging()
{
    using namespace erhe::log;
    log_imgui       = make_logger      ("erhe.imgui.imgui"      );
    log_performance = make_logger      ("erhe.imgui.performance");
    log_windows     = make_logger      ("erhe.imgui.windows"    );
    log_frame       = make_frame_logger("erhe.imgui.frame"      );
}

}
