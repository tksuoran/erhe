#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace erhe::graphics::test {

// Which FLIP variant evaluates the pair: LDR-FLIP for [0, 1] content (RGBA8
// outputs), HDR-FLIP for unbounded float content (RGBA32F outputs).
enum class Flip_range : unsigned int {
    ldr,
    hdr
};

class Flip_result
{
public:
    float                mean_error{0.0f};
    float                max_error {0.0f};
    std::vector<uint8_t> error_map_rgba8; // magma-colored error map, width * height * 4, rows top-down
};

// Evaluates NVIDIA FLIP (NVlabs/flip, cpp/FLIP.h) with default parameters
// between a reference and a test image. Both inputs are interleaved RGB
// floats, width * height * 3, treated as linear RGB (for LDR, in [0, 1]).
// This translation unit is the only one that includes FLIP.h.
[[nodiscard]] auto evaluate_flip(
    std::span<const float> reference_rgb,
    std::span<const float> test_rgb,
    int                    width,
    int                    height,
    Flip_range             range
) -> Flip_result;

} // namespace erhe::graphics::test
