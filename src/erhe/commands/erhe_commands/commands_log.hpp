#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::commands {

extern std::shared_ptr<spdlog::logger> log_command_state_transition;
extern std::shared_ptr<spdlog::logger> log_input;
extern std::shared_ptr<spdlog::logger> log_input_event;
extern std::shared_ptr<spdlog::logger> log_input_event_consumed;
extern std::shared_ptr<spdlog::logger> log_input_event_filtered;
extern std::shared_ptr<spdlog::logger> log_input_events;
extern std::shared_ptr<spdlog::logger> log_input_frame;

void initialize_logging();

} // namespace namespace erhe::commands
