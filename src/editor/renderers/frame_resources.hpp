#pragma once

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/pipeline.hpp"

namespace erhe::graphics
{
    class Vertex_attribute_mappings;
    class Vertex_format;
    class Shader_stages;
}

namespace editor
{

class Frame_resources
{
public:
    static constexpr gl::Buffer_storage_mask storage_mask{
        gl::Buffer_storage_mask::map_coherent_bit   |
        gl::Buffer_storage_mask::map_persistent_bit |
        gl::Buffer_storage_mask::map_write_bit
    };

    static constexpr gl::Map_buffer_access_mask access_mask{
        gl::Map_buffer_access_mask::map_coherent_bit   |
        gl::Map_buffer_access_mask::map_persistent_bit |
        gl::Map_buffer_access_mask::map_write_bit
    };

    explicit Frame_resources(
        const size_t material_stride,  const size_t material_count,
        const size_t light_stride,     const size_t light_count,
        const size_t camera_stride,    const size_t camera_count,
        const size_t primitive_stride, const size_t primitive_count,
        const size_t draw_stride,      const size_t draw_count
    )
        : material_buffer     {gl::Buffer_target::uniform_buffer,        material_count  * material_stride,  storage_mask, access_mask}
        , light_buffer        {gl::Buffer_target::uniform_buffer,        light_count     * light_stride,     storage_mask, access_mask}
        , camera_buffer       {gl::Buffer_target::uniform_buffer,        camera_count    * camera_stride,    storage_mask, access_mask}
        , primitive_buffer    {gl::Buffer_target::shader_storage_buffer, primitive_count * primitive_stride, storage_mask, access_mask}
        , draw_indirect_buffer{gl::Buffer_target::draw_indirect_buffer,  draw_count      * draw_stride,      storage_mask, access_mask}
    {
        material_buffer     .set_debug_label("Material");
        light_buffer        .set_debug_label("Light");
        camera_buffer       .set_debug_label("Camera");
        primitive_buffer    .set_debug_label("Primitive");
        draw_indirect_buffer.set_debug_label("Draw Indirect");
    }

    Frame_resources(const Frame_resources&) = delete;
    void operator= (const Frame_resources&) = delete;

    Frame_resources(Frame_resources&& other) noexcept
        : material_buffer     {std::move(other.material_buffer     )}
        , light_buffer        {std::move(other.light_buffer        )}
        , camera_buffer       {std::move(other.camera_buffer       )}
        , primitive_buffer    {std::move(other.primitive_buffer    )}
        , draw_indirect_buffer{std::move(other.draw_indirect_buffer)}
    {
    }

    auto operator=(Frame_resources&& other) noexcept
    -> Frame_resources&
    {
        material_buffer      = std::move(other.material_buffer);
        light_buffer         = std::move(other.light_buffer);
        camera_buffer        = std::move(other.camera_buffer);
        primitive_buffer     = std::move(other.primitive_buffer);
        draw_indirect_buffer = std::move(other.draw_indirect_buffer);
        return *this;
    }

    erhe::graphics::Buffer material_buffer;
    erhe::graphics::Buffer light_buffer;
    erhe::graphics::Buffer camera_buffer;
    erhe::graphics::Buffer primitive_buffer;
    erhe::graphics::Buffer draw_indirect_buffer;
};

} // namespace editor
