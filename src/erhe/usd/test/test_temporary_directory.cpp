#include "test_temporary_directory.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <random>
#include <stdexcept>
#include <system_error>

namespace erhe_usd_test {

namespace {

[[nodiscard]] auto claim_directory() -> std::filesystem::path
{
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    std::random_device          random_device{};
    std::mt19937_64             generator{(static_cast<std::uint64_t>(random_device()) << 32) | random_device()};
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path candidate = base / fmt::format("erhe_usd_tests_{:016x}", generator());
        std::error_code             error_code{};
        // create_directory() reports false when the directory already exists,
        // so a true result means no other process holds this name.
        if (std::filesystem::create_directory(candidate, error_code) && !error_code) {
            return candidate;
        }
    }
    throw std::runtime_error{"erhe_usd_tests: could not create a process temporary directory"};
}

auto directory_storage() -> std::filesystem::path&
{
    static std::filesystem::path directory = claim_directory();
    return directory;
}

} // namespace

auto process_temporary_directory() -> const std::filesystem::path&
{
    return directory_storage();
}

void remove_process_temporary_directory()
{
    std::error_code error_code{};
    std::filesystem::remove_all(directory_storage(), error_code);
}

} // namespace erhe_usd_test
