#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace erhe::graphics {

// Persistence of the driver-level VkPipelineCache across runs. The device
// loads the file of its own identity as the cache's initial data at
// creation and writes the cache data back at destruction, so pipelines
// compiled in one run skip the driver's compilation step in the next.
// The functions take no Vulkan handles, so they are tested without a
// device (erhe_graphics_tests).

// VkPhysicalDeviceProperties vendorID / deviceID / pipelineCacheUUID: the
// fields VkPipelineCacheHeaderVersionOne carries, which name the only
// device + driver build the data is valid for.
class Pipeline_cache_identity
{
public:
    uint32_t                vendor_id{0};
    uint32_t                device_id{0};
    std::array<uint8_t, 16> uuid{};
};

// <directory>/vk_<vendor>_<device>_<uuid>.bin: one file per identity, so
// machines with several Vulkan devices keep one cache each.
[[nodiscard]] auto make_pipeline_cache_path(
    const std::filesystem::path&   directory,
    const Pipeline_cache_identity& identity
) -> std::filesystem::path;

// Reads the file and returns its bytes when its header is a version-one
// pipeline cache header for the given identity; returns an empty vector
// when the file is absent, truncated, or carries another identity (another
// device, or a driver update changed the UUID).
[[nodiscard]] auto read_pipeline_cache_file(
    const std::filesystem::path&   path,
    const Pipeline_cache_identity& identity
) -> std::vector<uint8_t>;

// Writes the bytes atomically (temporary file in the same directory, then
// rename), creating the directory when needed. Returns false, after
// logging, when the file could not be written.
[[nodiscard]] auto write_pipeline_cache_file(
    const std::filesystem::path& path,
    std::span<const uint8_t>     data
) -> bool;

} // namespace erhe::graphics
