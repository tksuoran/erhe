#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::imgui {

extern std::shared_ptr<spdlog::logger> log_performance;
extern std::shared_ptr<spdlog::logger> log_imgui;
extern std::shared_ptr<spdlog::logger> log_windows;
extern std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging();

}
