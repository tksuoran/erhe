#include "erhe/scene/log.hpp"

namespace erhe::scene
{

using erhe::log::Log;

Log::Category log(Log::Color::YELLOW, Log::Color::GRAY, Log::Level::LEVEL_INFO);

} // namespace erhe::scene
