#include "erhe_math/math_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::math {

std::shared_ptr<spdlog::logger> log_input_axis;
std::shared_ptr<spdlog::logger> log_input_axis_frame;

void initialize_logging()
{
    using namespace erhe::log;
    log_input_axis = make_logger("erhe.math.input_axis");
    log_input_axis_frame = make_frame_logger("erhe.math.input_axis_frame");
}

} // namespace erhe::math
