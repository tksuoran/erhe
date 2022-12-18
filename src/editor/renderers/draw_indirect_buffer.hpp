#pragma once

#include "erhe/application/renderers/multi_buffer.hpp"
#include "erhe/primitive/enums.hpp"

namespace erhe::scene
{
    class Mesh;
    class Item_filter;
}

namespace editor
{

class Draw_indirect_buffer_range
{
public:
    erhe::application::Buffer_range range;
    std::size_t                     draw_indirect_count{0};
};

class Draw_indirect_buffer
    : public erhe::application::Multi_buffer
{
public:
    explicit Draw_indirect_buffer(std::size_t max_draw_count);

    // Can discard return value
    auto update(
        const gsl::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
        erhe::primitive::Primitive_mode                            primitive_mode,
        const erhe::scene::Item_filter&                            filter
    ) -> Draw_indirect_buffer_range;

    void debug_properties_window();

private:
    bool m_max_index_count_enable{false};
    int  m_max_index_count       {256};
};

} // namespace editor
