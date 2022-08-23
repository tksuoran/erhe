#include "erhe/xr/xr_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::xr
{

std::shared_ptr<spdlog::logger> log_xr;

void initialize_logging()
{
    log_xr = erhe::log::make_logger("erhe::xr::log", spdlog::level::info);
}

} // namespace erhe::ui
