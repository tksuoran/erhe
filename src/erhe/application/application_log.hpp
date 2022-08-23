#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::application {

extern std::shared_ptr<spdlog::logger> log_command_state_transition;
extern std::shared_ptr<spdlog::logger> log_input;
extern std::shared_ptr<spdlog::logger> log_input_event;
extern std::shared_ptr<spdlog::logger> log_input_event_consumed;
extern std::shared_ptr<spdlog::logger> log_input_event_filtered;
extern std::shared_ptr<spdlog::logger> log_input_events;
extern std::shared_ptr<spdlog::logger> log_multi_buffer;
extern std::shared_ptr<spdlog::logger> log_performance;
extern std::shared_ptr<spdlog::logger> log_rendergraph;
extern std::shared_ptr<spdlog::logger> log_renderdoc;
extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_tools;
extern std::shared_ptr<spdlog::logger> log_windows;
extern std::shared_ptr<spdlog::logger> log_shader_monitor;
extern std::shared_ptr<spdlog::logger> log_imgui;
extern std::shared_ptr<spdlog::logger> log_frame;
extern std::shared_ptr<spdlog::logger> log_imnodes;

void initialize_logging();

}
