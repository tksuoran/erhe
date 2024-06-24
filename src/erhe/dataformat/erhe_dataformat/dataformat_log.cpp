#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::dataformat
{

std::shared_ptr<spdlog::logger> log_dataformat;

void initialize_logging()
{
    using namespace erhe::log;
    log_dataformat = make_logger("erhe.dataformat");
}

} // namespace erhe::dataformat
