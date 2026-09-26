#pragma once

#include <filesystem>

namespace erhe_usd_test {

// A directory under the system temporary directory that belongs to this test
// process alone. ctest runs every discovered case as its own process and runs
// them concurrently under -j, so cases (even different cases of one fixture)
// that write a fixed file name must not share a directory with another
// process. Created on first use; removed by the test environment at exit.
[[nodiscard]] auto process_temporary_directory() -> const std::filesystem::path&;

void remove_process_temporary_directory();

} // namespace erhe_usd_test
