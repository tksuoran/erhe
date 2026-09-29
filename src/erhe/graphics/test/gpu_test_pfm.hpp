#pragma once

#include <filesystem>
#include <vector>

namespace erhe::graphics::test {

// A three-channel float image, rows top-down, 3 floats per texel (R, G, B).
class Pfm_image
{
public:
    int                width {0};
    int                height{0};
    std::vector<float> rgb;
};

// Portable float map (PFM) I/O for the float goldens of the GPU tests. Only the
// color variant ("PF", three channels) is read and written. PFM stores rows
// bottom-up; both functions convert, so Pfm_image rows are top-down. The writer
// emits little-endian samples (negative scale); the reader accepts either
// byte order.
[[nodiscard]] auto read_pfm (const std::filesystem::path& path, Pfm_image& image) -> bool;
[[nodiscard]] auto write_pfm(const std::filesystem::path& path, const Pfm_image& image) -> bool;

} // namespace erhe::graphics::test
