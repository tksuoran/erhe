#include "erhe_time/time_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::time
{

std::shared_ptr<spdlog::logger> log_time;

void initialize_logging()
{
    using namespace erhe::log;
    log_time = make_logger("erhe.time");
}

}
