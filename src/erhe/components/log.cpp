#include "erhe/components/log.hpp"

namespace erhe::components
{

using erhe::log::Log;

Log::Category log_components(Log::Color::WHITE, Log::Color::GRAY, Log::Level::LEVEL_INFO);

} // namespace erhe::components
