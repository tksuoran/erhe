#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>
#include <span>

namespace erhe {
    class Item_filter;
}
namespace erhe::scene {
    class Mesh;
}

namespace erhe::renderer {

class Draw_indirect_buffer_range
{
public:
    erhe::graphics::Buffer_range range;
    std::size_t                  draw_indirect_count{0};
};

class Draw_indirect_buffer : public erhe::graphics::GPU_ring_buffer_client
{
public:
    explicit Draw_indirect_buffer(erhe::graphics::Device& graphics_device);

    // Can discard return value
    auto update(
        const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const erhe::Item_filter&                                   filter
    ) -> Draw_indirect_buffer_range;

    //// void debug_properties_window();

private:
    [[nodiscard]] static auto get_max_draw_count() -> int;
 
    bool m_max_index_count_enable{false};
    int  m_max_index_count       {256};
    int  m_max_draw_count        {8000};
};

} // namespace erhe::renderer
