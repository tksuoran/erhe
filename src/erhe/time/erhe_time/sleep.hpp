#pragma once

#include <chrono>
#include <cstdint>

namespace erhe::time {

auto sleep_initialize() -> bool;
void sleep_for(std::chrono::duration<float, std::milli> time_to_sleep);
void sleep_for_100ns(int64_t time_to_sleep_in_100ns);

} // namespace erhe::time
