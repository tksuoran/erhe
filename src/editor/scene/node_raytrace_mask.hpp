#pragma once

#include <cstdint>

namespace editor
{

class Raytrace_node_mask
{
public:
    static constexpr uint32_t none        = 0u;
    static constexpr uint32_t content     = (1u << 0);
    static constexpr uint32_t shadow_cast = (1u << 1);
    static constexpr uint32_t tool        = (1u << 2);
    static constexpr uint32_t brush       = (1u << 3);
    static constexpr uint32_t gui         = (1u << 4);
    static constexpr uint32_t controller  = (1u << 5);
};

} // namespace editor
