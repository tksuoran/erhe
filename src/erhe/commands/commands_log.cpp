#include "erhe/commands/commands_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::commands {

std::shared_ptr<spdlog::logger> log_command_state_transition;
std::shared_ptr<spdlog::logger> log_input                   ;
std::shared_ptr<spdlog::logger> log_input_event             ;
std::shared_ptr<spdlog::logger> log_input_event_consumed    ;
std::shared_ptr<spdlog::logger> log_input_event_filtered    ;
std::shared_ptr<spdlog::logger> log_input_events            ;

void initialize_logging()
{
    log_command_state_transition = erhe::log::make_logger("command_state_transition", spdlog::level::info);
    log_input                    = erhe::log::make_logger("input",                    spdlog::level::info);
    log_input_event              = erhe::log::make_logger("input_event",              spdlog::level::info);
    log_input_event_consumed     = erhe::log::make_logger("input_event_consumed",     spdlog::level::info);
    log_input_event_filtered     = erhe::log::make_logger("input_event_filtered",     spdlog::level::info);
    log_input_events             = erhe::log::make_logger("input_events",             spdlog::level::info);
}

} // namespace erhe::commands
