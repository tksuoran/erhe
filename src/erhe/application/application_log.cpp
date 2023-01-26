#include "erhe/application/application_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::application {

std::shared_ptr<spdlog::logger> log_command_state_transition;
std::shared_ptr<spdlog::logger> log_frame                   ;
std::shared_ptr<spdlog::logger> log_imgui                   ;
std::shared_ptr<spdlog::logger> log_imnodes                 ;
std::shared_ptr<spdlog::logger> log_input                   ;
std::shared_ptr<spdlog::logger> log_input_event             ;
std::shared_ptr<spdlog::logger> log_input_event_consumed    ;
std::shared_ptr<spdlog::logger> log_input_event_filtered    ;
std::shared_ptr<spdlog::logger> log_input_events            ;
std::shared_ptr<spdlog::logger> log_multi_buffer            ;
std::shared_ptr<spdlog::logger> log_performance             ;
std::shared_ptr<spdlog::logger> log_renderdoc               ;
std::shared_ptr<spdlog::logger> log_rendergraph             ;
std::shared_ptr<spdlog::logger> log_shader_monitor          ;
std::shared_ptr<spdlog::logger> log_startup                 ;
std::shared_ptr<spdlog::logger> log_tools                   ;
std::shared_ptr<spdlog::logger> log_update                  ;
std::shared_ptr<spdlog::logger> log_windows                 ;

void initialize_logging()
{
    log_command_state_transition = erhe::log::make_logger("command_state_transition", spdlog::level::info);
    log_frame                    = erhe::log::make_logger("frame",                    spdlog::level::info, false);
    log_imgui                    = erhe::log::make_logger("imgui",                    spdlog::level::info);
    log_imnodes                  = erhe::log::make_logger("imnodes",                  spdlog::level::info);
    log_input                    = erhe::log::make_logger("input",                    spdlog::level::info);
    log_input_event              = erhe::log::make_logger("input_event",              spdlog::level::info);
    log_input_event_consumed     = erhe::log::make_logger("input_event_consumed",     spdlog::level::info);
    log_input_event_filtered     = erhe::log::make_logger("input_event_filtered",     spdlog::level::info);
    log_input_events             = erhe::log::make_logger("input_events",             spdlog::level::info);
    log_multi_buffer             = erhe::log::make_logger("multi_buffer",             spdlog::level::info);
    log_performance              = erhe::log::make_logger("performance",              spdlog::level::info);
    log_renderdoc                = erhe::log::make_logger("renderdoc",                spdlog::level::info);
    log_rendergraph              = erhe::log::make_logger("rendergraph",              spdlog::level::info);
    log_shader_monitor           = erhe::log::make_logger("shader_monitor",           spdlog::level::info);
    log_startup                  = erhe::log::make_logger("startup",                  spdlog::level::info);
    log_tools                    = erhe::log::make_logger("tools",                    spdlog::level::info);
    log_update                   = erhe::log::make_logger("update",                   spdlog::level::info);
    log_windows                  = erhe::log::make_logger("windows",                  spdlog::level::info);
}

}
