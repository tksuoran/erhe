#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::ui {

extern std::shared_ptr<spdlog::logger> log_text_buffer;
extern std::shared_ptr<spdlog::logger> log_font;

void initialize_logging();

} // namespace erhe::ui
