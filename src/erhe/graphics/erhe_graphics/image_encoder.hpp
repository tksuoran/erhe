#pragma once

#include "erhe_graphics/image_loader.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace erhe::graphics {

// Target of a load-time texture reduction (doc/plans/memory_budget.md).
enum class Block_encode_format : unsigned int {
    none = 0, // keep RGBA8
    bc7,
    astc_4x4
};

class Image_reduction
{
public:
    Block_encode_format encode_format   {Block_encode_format::none};
    int                 downscale_levels{0}; // halvings of the source before anything else
};

[[nodiscard]] auto is_identity(const Image_reduction& reduction) -> bool;

// The full mip chain of the downscaled size, in the encoded or source format.
[[nodiscard]] auto get_reduced_byte_count(const Image_info& source_info, const Image_reduction& reduction) -> std::size_t;

// False unless source_info is a single-level RGBA8 image (doc/plans/memory_budget.md step 6).
[[nodiscard]] auto reduce_rgba8_image(
    const Image_info&             source_info,
    std::span<const std::uint8_t> source_pixels,
    const Image_reduction&        reduction,
    Image_info&                   out_info,
    std::vector<std::uint8_t>&    out_pixels
) -> bool;

} // namespace erhe::graphics
