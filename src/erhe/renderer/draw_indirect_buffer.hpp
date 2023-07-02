#pragma once

#include "erhe/renderer/multi_buffer.hpp"
#include "erhe/primitive/enums.hpp"

namespace erhe::scene
{
    class Mesh;
    class Item_filter;
}

namespace erhe::renderer
{

class Draw_indirect_buffer_range
{
public:
    Buffer_range range;
    std::size_t  draw_indirect_count{0};
};

class Draw_indirect_buffer
    : public Multi_buffer
{
public:
    explicit Draw_indirect_buffer(
        erhe::graphics::Instance& graphics_instance
    );

    // Can discard return value
    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const erhe::scene::Item_filter&                            filter
    ) -> Draw_indirect_buffer_range;

    //// void debug_properties_window();

private:
    bool m_max_index_count_enable{false};
    int  m_max_index_count       {256};
    int  m_max_draw_count        {8000};
};

} // namespace erhe::renderer
