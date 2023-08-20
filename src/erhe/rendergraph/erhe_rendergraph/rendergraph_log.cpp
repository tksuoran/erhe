#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::rendergraph {

std::shared_ptr<spdlog::logger> log_tail;
std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging()
{
    using namespace erhe::log;
    log_tail  = make_logger      ("erhe.rendergraph.tail" );
    log_frame = make_frame_logger("erhe.rendergraph.frame");
}

} // namespace erhe::rendergraph
