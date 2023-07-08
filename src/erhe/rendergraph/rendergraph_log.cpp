#include "erhe/rendergraph/rendergraph_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::rendergraph {

std::shared_ptr<spdlog::logger> log_tail;
std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging()
{
    log_tail  = erhe::log::make_logger("tail",  spdlog::level::trace);
    log_frame = erhe::log::make_logger("frame", spdlog::level::trace, false);
}

} // namespace erhe::rendergraph
