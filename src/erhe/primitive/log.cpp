#include "erhe/primitive/log.hpp"

namespace erhe::primitive
{

std::shared_ptr<spdlog::logger> log_primitive_builder;
std::shared_ptr<spdlog::logger> log_primitive        ;

void initialize_logging()
{
    log_primitive_builder = erhe::log::make_logger("erhe::primitive::primitive_builder", spdlog::level::warn);
    log_primitive         = erhe::log::make_logger("erhe::primitive::primitive",         spdlog::level::warn);
}

} // namespace erhe::primitive
