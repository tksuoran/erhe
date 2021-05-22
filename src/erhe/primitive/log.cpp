#include "erhe/primitive/log.hpp"


namespace erhe::primitive
{

using namespace erhe::log;

Category log_primitive_builder(Color::RED,     Color::GRAY, Level::LEVEL_WARN);
Category log_primitive        (Color::MAGENTA, Color::GRAY, Level::LEVEL_WARN);

} // namespace erhe::primitive
