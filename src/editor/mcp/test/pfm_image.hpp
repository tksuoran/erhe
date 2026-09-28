#pragma once

// Reader for the portable float map (.pfm) images render_scene_image writes
// with output "linear" (doc/agents/mcp_server_usage.md "Image Tools").

#include <filesystem>
#include <string>
#include <vector>

namespace mcp_test {

class Pfm_image
{
public:
    // Reads a "PF" (RGB) or "Pf" (grey) float map of either byte order.
    // Returns false with `error` set when the file is missing or malformed.
    auto read(const std::filesystem::path& path, std::string& error) -> bool;

    [[nodiscard]] auto get_width   () const -> int { return m_width; }
    [[nodiscard]] auto get_height  () const -> int { return m_height; }
    [[nodiscard]] auto get_channels() const -> int { return m_channels; }

    // Pixel (x, y) in file row order (PFM stores the bottom row first); two
    // images of the same render size pair up pixel by pixel either way.
    [[nodiscard]] auto get(int x, int y, int channel) const -> float;

private:
    int                m_width   {0};
    int                m_height  {0};
    int                m_channels{0};
    std::vector<float> m_values;
};

} // namespace mcp_test
