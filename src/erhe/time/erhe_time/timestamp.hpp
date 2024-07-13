#pragma once

#include <string>

namespace erhe::time {

[[nodiscard]] auto timestamp      () -> std::string;
[[nodiscard]] auto timestamp_short() -> std::string;

} // namespace erhe::time
