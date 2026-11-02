#pragma once

#include "erhe_graphics/image_encoder.hpp"
#include "erhe_graphics/image_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace erhe::gltf {

// Reduced image disk cache (doc/plans/memory_budget.md step 7).
class Image_reduction_cache_key
{
public:
    std::uint64_t value{0};
};

[[nodiscard]] auto make_image_reduction_cache_key(
    std::span<const std::byte>             encoded_stream,
    const erhe::graphics::Image_reduction& reduction,
    bool                                   linear
) -> Image_reduction_cache_key;

[[nodiscard]] auto load_reduced_image(
    const std::filesystem::path&     cache_directory,
    const Image_reduction_cache_key& key,
    erhe::graphics::Image_info&      out_info,
    std::vector<std::uint8_t>&       out_pixels
) -> bool;

void store_reduced_image(
    const std::filesystem::path&      cache_directory,
    const Image_reduction_cache_key&  key,
    const erhe::graphics::Image_info& info,
    std::span<const std::uint8_t>     pixels
);

} // namespace erhe::gltf
