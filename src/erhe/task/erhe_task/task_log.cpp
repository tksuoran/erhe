#include "erhe_task/task_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::task {

std::shared_ptr<spdlog::logger> log_task;

void initialize_logging()
{
    log_task = erhe::log::make_logger("erhe.task.log_task");
}

} // namespace erhe::task
