#pragma once

#include <chrono>

namespace erhe::toolkit
{

auto sleep_initialize() -> bool;
void sleep_for(std::chrono::duration<float, std::milli> time_to_sleep);

}