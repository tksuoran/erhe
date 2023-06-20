#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::rendergraph {

extern std::shared_ptr<spdlog::logger> log_tail;
extern std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging();

} // namespace erhe::rendergraph
