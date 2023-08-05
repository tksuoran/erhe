#include "erhe/primitive/primitive_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::primitive
{

std::shared_ptr<spdlog::logger> log_primitive_builder;
std::shared_ptr<spdlog::logger> log_primitive        ;

void initialize_logging()
{
    using namespace erhe::log;
    log_primitive_builder = make_logger("erhe.primitive.primitive_builder");
    log_primitive         = make_logger("erhe.primitive.primitive"        );
}

} // namespace erhe::primitive
