#include "rendering_test_log.hpp"
#include "erhe_log/log.hpp"

namespace rendering_test {

std::shared_ptr<spdlog::logger> log_test;

void initialize_logging()
{
    using namespace erhe::log;
    log_test = make_logger("rendering_test");
}

}
