#include "erhe_graphics/vulkan/vulkan_pipeline_cache_file.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <fmt/format.h>

#include <cstring>
#include <fstream>
#include <string>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   include <unistd.h>
#endif

namespace erhe::graphics {

namespace {

// VkPipelineCacheHeaderVersionOne, as laid out in the data returned by
// vkGetPipelineCacheData: every field little-endian, in this order.
constexpr std::size_t c_header_size_offset    = 0;
constexpr std::size_t c_header_version_offset = 4;
constexpr std::size_t c_vendor_id_offset      = 8;
constexpr std::size_t c_device_id_offset      = 12;
constexpr std::size_t c_uuid_offset           = 16;
constexpr std::size_t c_header_version_one_size = 32;
constexpr uint32_t    c_header_version_one      = 1; // VK_PIPELINE_CACHE_HEADER_VERSION_ONE

auto read_u32_le(const uint8_t* bytes) -> uint32_t
{
    return
        (static_cast<uint32_t>(bytes[0])      ) |
        (static_cast<uint32_t>(bytes[1]) <<  8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

auto process_suffix() -> std::string
{
#if defined(_WIN32)
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

} // anonymous namespace

auto make_pipeline_cache_path(
    const std::filesystem::path&   directory,
    const Pipeline_cache_identity& identity
) -> std::filesystem::path
{
    std::string uuid_hex;
    uuid_hex.reserve(32);
    for (const uint8_t byte : identity.uuid) {
        uuid_hex += fmt::format("{:02x}", byte);
    }
    return directory / fmt::format("vk_{:08x}_{:08x}_{}.bin", identity.vendor_id, identity.device_id, uuid_hex);
}

auto read_pipeline_cache_file(
    const std::filesystem::path&   path,
    const Pipeline_cache_identity& identity
) -> std::vector<uint8_t>
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return {};
    }
    const std::uintmax_t file_size = std::filesystem::file_size(path, ec);
    if (ec || (file_size < c_header_version_one_size)) {
        return {};
    }

    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return {};
    }
    std::vector<uint8_t> data(static_cast<std::size_t>(file_size));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size));
    if (!file.good()) {
        return {};
    }

    const uint32_t header_size    = read_u32_le(data.data() + c_header_size_offset);
    const uint32_t header_version = read_u32_le(data.data() + c_header_version_offset);
    const uint32_t vendor_id      = read_u32_le(data.data() + c_vendor_id_offset);
    const uint32_t device_id      = read_u32_le(data.data() + c_device_id_offset);
    if (
        (header_size    < c_header_version_one_size) ||
        (header_size    > data.size()) ||
        (header_version != c_header_version_one) ||
        (vendor_id      != identity.vendor_id) ||
        (device_id      != identity.device_id) ||
        (std::memcmp(data.data() + c_uuid_offset, identity.uuid.data(), identity.uuid.size()) != 0)
    ) {
        log_context->info("Pipeline cache file {} is for another device or driver; ignoring it", path.string());
        return {};
    }
    return data;
}

auto write_pipeline_cache_file(
    const std::filesystem::path& path,
    const std::span<const uint8_t> data
) -> bool
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        log_context->warn("Failed to create pipeline cache directory '{}': {}", path.parent_path().string(), ec.message());
        return false;
    }

    const std::filesystem::path temporary_path = path.parent_path() / (path.filename().string() + ".tmp." + process_suffix());
    {
        std::ofstream file{temporary_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            log_context->warn("Failed to write pipeline cache file: {}", temporary_path.string());
            return false;
        }
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file.good()) {
            log_context->warn("Failed to write pipeline cache file: {}", temporary_path.string());
            file.close();
            std::error_code remove_ec;
            std::filesystem::remove(temporary_path, remove_ec);
            return false;
        }
    }

    // rename() replaces an existing file atomically on POSIX and on NTFS,
    // so a reader never sees a partially written file.
    std::filesystem::rename(temporary_path, path, ec);
    if (ec) {
        log_context->warn("Failed to store pipeline cache file {}: {}", path.string(), ec.message());
        std::error_code remove_ec;
        std::filesystem::remove(temporary_path, remove_ec);
        return false;
    }
    return true;
}

} // namespace erhe::graphics
