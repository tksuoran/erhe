#pragma once

#include <memory>

namespace spdlog { class logger; }

namespace erhe::graph {

extern std::shared_ptr<spdlog::logger> log_graph;

void initialize_logging();

}
