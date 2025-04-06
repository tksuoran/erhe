#include "erhe_graph/graph_log.hpp"
#include "erhe_log/log.hpp"

//#include <spdlog/spdlog.h>

namespace erhe::graph {

std::shared_ptr<spdlog::logger> log_graph;

void initialize_logging()
{
    using namespace erhe::log;
    log_graph = make_logger("erhe.graph");
}

}
