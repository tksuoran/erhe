#include "erhe/primitive/log.hpp"


namespace erhe::primitive
{

using erhe::log::Log;

Log::Category log_primitive_builder(Log::Color::RED,     Log::Color::GRAY, Log::Level::LEVEL_WARN);
Log::Category log_primitive        (Log::Color::MAGENTA, Log::Color::GRAY, Log::Level::LEVEL_WARN);

} // namespace erhe::primitive
