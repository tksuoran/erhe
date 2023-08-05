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
    using namespace erhe::log;
    log_command_state_transition = make_logger("erhe.commands.command_state_transition");
    log_input                    = make_logger("erhe.commands.input"                   );
    log_input_event              = make_logger("erhe.commands.input_event"             );
    log_input_event_consumed     = make_logger("erhe.commands.input_event_consumed"    );
    log_input_event_filtered     = make_logger("erhe.commands.input_event_filtered"    );
    log_input_events             = make_logger("erhe.commands.input_events"            );
}

} // namespace erhe::commands
