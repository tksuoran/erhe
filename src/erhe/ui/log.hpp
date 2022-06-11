#pragma once

#include "erhe/log/log.hpp"

namespace erhe::ui
{

extern std::shared_ptr<spdlog::logger> log_text_buffer;
extern std::shared_ptr<spdlog::logger> log_font;

void initialize_logging();

} // namespace erhe::ui
