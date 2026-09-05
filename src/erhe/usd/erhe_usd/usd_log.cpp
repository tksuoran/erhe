#include "erhe_usd/usd_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::usd {

std::shared_ptr<spdlog::logger> log_usd;

void initialize_logging()
{
    using namespace erhe::log;
    log_usd = make_logger("erhe.usd");
}

}
