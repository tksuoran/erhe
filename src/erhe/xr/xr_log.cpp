#include "erhe/xr/xr_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::xr
{

std::shared_ptr<spdlog::logger> log_xr;

void initialize_logging()
{
    using namespace erhe::log;
    log_xr = make_logger("erhe.xr.log");
}

} // namespace erhe::ui
