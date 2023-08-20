#include "erhe_file/file_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::file {

std::shared_ptr<spdlog::logger> log_file;

void initialize_logging()
{
    using namespace erhe::log;
    log_file = make_logger("erhe.file");
}

}
