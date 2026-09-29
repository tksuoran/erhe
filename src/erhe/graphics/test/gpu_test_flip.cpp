#include "gpu_test_flip.hpp"

// FLIP.h is third-party; it defines file-static tables and the Max / Min
// macros, so it stays confined to this translation unit. The target's
// CMakeLists exempts this file from warnings-as-errors.
#include <FLIP.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace erhe::graphics::test {

auto evaluate_flip(
    const std::span<const float> reference_rgb,
    const std::span<const float> test_rgb,
    const int                    width,
    const int                    height,
    const Flip_range             range
) -> Flip_result
{
    Flip_result result{};
    const std::size_t texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if ((width <= 0) || (height <= 0) || (reference_rgb.size() != (texel_count * 3u)) || (test_rgb.size() != (texel_count * 3u))) {
        result.mean_error = 1.0f;
        result.max_error  = 1.0f;
        return result;
    }

    FLIP::Parameters parameters{};
    float  mean_error = 0.0f;
    float* error_map  = nullptr;
    const bool use_hdr = (range == Flip_range::hdr);
    FLIP::evaluate(
        reference_rgb.data(),
        test_rgb.data(),
        width,
        height,
        use_hdr,
        parameters,
        false, // applyMagmaMapToOutput: the grayscale map is needed for the max; magma is applied below
        true,  // computeMeanFLIPError
        mean_error,
        &error_map
    );
    // FLIP allocates the grayscale error map with new[] and hands ownership over.
    const std::unique_ptr<float[]> error_map_owner{error_map};

    result.mean_error = mean_error;
    result.error_map_rgba8.resize(texel_count * 4u);
    float max_error = 0.0f;
    for (std::size_t i = 0; i < texel_count; ++i) {
        const float error = std::clamp(error_map[i], 0.0f, 1.0f);
        max_error = std::max(max_error, error);
        const int           magma_index = static_cast<int>(std::lround(error * 255.0f));
        const FLIP::color3& magma       = FLIP::MapMagma[magma_index];
        // MapMagma entries are sRGB-encoded values in [0, 1].
        result.error_map_rgba8[(i * 4u) + 0u] = static_cast<uint8_t>(std::lround(std::clamp(magma.x, 0.0f, 1.0f) * 255.0f));
        result.error_map_rgba8[(i * 4u) + 1u] = static_cast<uint8_t>(std::lround(std::clamp(magma.y, 0.0f, 1.0f) * 255.0f));
        result.error_map_rgba8[(i * 4u) + 2u] = static_cast<uint8_t>(std::lround(std::clamp(magma.z, 0.0f, 1.0f) * 255.0f));
        result.error_map_rgba8[(i * 4u) + 3u] = 255u;
    }
    result.max_error = max_error;
    return result;
}

} // namespace erhe::graphics::test
