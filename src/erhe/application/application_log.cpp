#include "erhe/application/application_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::application {

std::shared_ptr<spdlog::logger> log_command_state_transition;
std::shared_ptr<spdlog::logger> log_input                   ;
std::shared_ptr<spdlog::logger> log_input_event             ;
std::shared_ptr<spdlog::logger> log_input_event_consumed    ;
std::shared_ptr<spdlog::logger> log_input_event_filtered    ;
std::shared_ptr<spdlog::logger> log_input_events            ;
std::shared_ptr<spdlog::logger> log_multi_buffer            ;
std::shared_ptr<spdlog::logger> log_performance             ;
std::shared_ptr<spdlog::logger> log_rendergraph             ;
std::shared_ptr<spdlog::logger> log_renderdoc               ;
std::shared_ptr<spdlog::logger> log_startup                 ;
std::shared_ptr<spdlog::logger> log_tools                   ;
std::shared_ptr<spdlog::logger> log_windows                 ;
std::shared_ptr<spdlog::logger> log_shader_monitor          ;
std::shared_ptr<spdlog::logger> log_imgui                   ;
std::shared_ptr<spdlog::logger> log_frame                   ;
std::shared_ptr<spdlog::logger> log_imnodes                 ;

void initialize_logging()
{
    log_command_state_transition = erhe::log::make_logger("erhe::application::command_state_transition", spdlog::level::info);
    log_input                    = erhe::log::make_logger("erhe::application::input",                    spdlog::level::info);
    log_input_event              = erhe::log::make_logger("erhe::application::input_event",              spdlog::level::info);
    log_input_event_consumed     = erhe::log::make_logger("erhe::application::input_event_consumed",     spdlog::level::info);
    log_input_event_filtered     = erhe::log::make_logger("erhe::application::input_event_filtered",     spdlog::level::info);
    log_input_events             = erhe::log::make_logger("erhe::application::input_events",             spdlog::level::info);
    log_multi_buffer             = erhe::log::make_logger("erhe::application::multi_buffer",             spdlog::level::info);
    log_performance              = erhe::log::make_logger("erhe::application::performance",              spdlog::level::info);
    log_rendergraph              = erhe::log::make_logger("erhe::application::rendergraph",              spdlog::level::info);
    log_renderdoc                = erhe::log::make_logger("erhe::application::renderdoc",                spdlog::level::info);
    log_startup                  = erhe::log::make_logger("erhe::application::startup",                  spdlog::level::info);
    log_tools                    = erhe::log::make_logger("erhe::application::tools",                    spdlog::level::info);
    log_windows                  = erhe::log::make_logger("erhe::application::windows",                  spdlog::level::info);
    log_shader_monitor           = erhe::log::make_logger("erhe::application::shader_monitor",           spdlog::level::info);
    log_imgui                    = erhe::log::make_logger("erhe::application::imgui",                    spdlog::level::info);
    log_frame                    = erhe::log::make_logger("erhe::application::frame",                    spdlog::level::info, false);
    log_imnodes                  = erhe::log::make_logger("imnodes",                                     spdlog::level::info);
}

}
