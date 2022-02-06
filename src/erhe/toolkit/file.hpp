#pragma once

#include "erhe/toolkit/filesystem.hpp"
#include "erhe/toolkit/optional.hpp"

#include <string>

namespace erhe::toolkit
{

// return value will be empty if file does not exist, or is not regular file, or is empty
[[nodiscard]]
auto read(const fs::path& path) -> nonstd::optional<std::string>;

} // namespace erhe::toolkit
