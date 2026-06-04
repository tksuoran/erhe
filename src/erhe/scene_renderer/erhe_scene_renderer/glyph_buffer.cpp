#include "erhe_scene_renderer/glyph_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_ui/glyph_outlines.hpp"

#include <algorithm>
#include <vector>

namespace erhe::scene_renderer {

Glyph_interface::Glyph_interface(erhe::graphics::Device& graphics_device)
    : supported{graphics_device.get_info().use_shader_storage_buffers}
    , glyph_block{
        graphics_device,
        {
            .name          = "glyph",
            .binding_point = glyph_buffer_binding_point,
            .type          = supported
                ? erhe::graphics::Shader_resource::Type::shader_storage_block
                : erhe::graphics::Shader_resource::Type::uniform_block,
            .readonly      = true
        }
    }
    , glyph_meta_struct{graphics_device, "Glyph_meta"}
    , glyph_meta_offsets{
        .start   = glyph_meta_struct.add_int  ("start"  )->get_offset_in_parent(),
        .count   = glyph_meta_struct.add_int  ("count"  )->get_offset_in_parent(),
        .bearing = glyph_meta_struct.add_vec2 ("bearing")->get_offset_in_parent(),
        .size    = glyph_meta_struct.add_vec2 ("size"   )->get_offset_in_parent(),
        .advance = glyph_meta_struct.add_float("advance")->get_offset_in_parent(),
    }
{
    glyphs_member = glyph_block.add_struct("glyphs", &glyph_meta_struct, glyph_slot_count);

    // The curves array must be the last member: it is unsized in the SSBO
    // case. In the dummy uniform block fallback (no SSBO support) the
    // array gets a fixed size of one element; it is never read because
    // shaders are compiled without ERHE_GRID_LABELS.
    curves_member = glyph_block.add_vec2(
        "curves",
        supported ? erhe::graphics::Shader_resource::unsized_array : std::size_t{1}
    );
}

namespace {

// Builds the full CPU-side byte image of the glyph block: glyph metadata
// array followed by the flat curve vec2 array. Uploaded once at buffer
// creation via Buffer_create_info::init_data.
auto build_glyph_buffer_data(
    erhe::graphics::Device&            graphics_device,
    Glyph_interface&                   glyph_interface,
    const erhe::ui::Glyph_outline_set& glyph_outline_set
) -> std::vector<std::byte>
{
    // Block size already accounts for one (unsized) curve array element
    // and device offset alignment padding; use it as the minimum so the
    // whole declared block always fits in the buffer.
    const std::size_t block_byte_count = glyph_interface.glyph_block.get_size_bytes();
    if (!glyph_interface.supported || !glyph_outline_set.valid) {
        return std::vector<std::byte>(block_byte_count, std::byte{0});
    }

    const erhe::graphics::Shader_resource::Layout layout = (graphics_device.get_info().glsl_version >= 430)
        ? erhe::graphics::Shader_resource::Layout::std430
        : erhe::graphics::Shader_resource::Layout::std140;

    const std::size_t curves_offset     = glyph_interface.curves_member->get_offset_in_parent();
    const std::size_t curve_vec2_stride = glyph_interface.curves_member->get_size_bytes(layout);
    const std::size_t vec2_count        = glyph_outline_set.curves.size() * 3;
    const std::size_t byte_count        = std::max(block_byte_count, curves_offset + (vec2_count * curve_vec2_stride));

    std::vector<std::byte> data(byte_count, std::byte{0});
    std::span<std::byte>   data_span{data};

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    // Glyph metadata: slot order == extraction codepoint order.
    // Slots beyond the extracted glyph count stay zero (count == 0).
    const Glyph_meta_struct& offsets       = glyph_interface.glyph_meta_offsets;
    const std::size_t        glyphs_offset = glyph_interface.glyphs_member->get_offset_in_parent();
    const std::size_t        glyph_stride  = glyph_interface.glyph_meta_struct.get_size_bytes(layout);
    const std::size_t        glyph_count   = std::min(glyph_outline_set.glyphs.size(), Glyph_interface::glyph_slot_count);
    for (std::size_t slot = 0; slot < glyph_count; ++slot) {
        const erhe::ui::Glyph_outline& outline     = glyph_outline_set.glyphs[slot];
        const std::size_t              slot_offset = glyphs_offset + (slot * glyph_stride);
        // The shader indexes curves[] in vec2 units: curve i of a
        // glyph spans curves[3 * (start + i) + 0..2].
        const int32_t start = static_cast<int32_t>(outline.curve_start);
        const int32_t count = static_cast<int32_t>(outline.curve_count);
        write(data_span, slot_offset + offsets.start,   as_span(start));
        write(data_span, slot_offset + offsets.count,   as_span(count));
        write(data_span, slot_offset + offsets.bearing, as_span(outline.bearing));
        write(data_span, slot_offset + offsets.size,    as_span(outline.size));
        write(data_span, slot_offset + offsets.advance, as_span(outline.advance));
    }

    // Curve control points: 3 consecutive vec2 entries per curve.
    std::size_t write_offset = curves_offset;
    for (const erhe::ui::Glyph_curve& curve : glyph_outline_set.curves) {
        write(data_span, write_offset, as_span(curve.p0)); write_offset += curve_vec2_stride;
        write(data_span, write_offset, as_span(curve.p1)); write_offset += curve_vec2_stride;
        write(data_span, write_offset, as_span(curve.p2)); write_offset += curve_vec2_stride;
    }

    return data;
}

} // anonymous namespace

Glyph_buffer::Glyph_buffer(
    erhe::graphics::Device&            graphics_device,
    Glyph_interface&                   glyph_interface,
    const erhe::ui::Glyph_outline_set& glyph_outline_set
)
    : m_glyph_interface{glyph_interface}
    , m_buffer{graphics_device}
{
    const std::vector<std::byte> data = build_glyph_buffer_data(graphics_device, glyph_interface, glyph_outline_set);
    m_buffer = erhe::graphics::Buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = data.size(),
            .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::none,
            .usage                                  = erhe::graphics::get_buffer_usage(glyph_interface.glyph_block.get_binding_target()),
            .required_memory_property_bit_mask      =
                erhe::graphics::Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .init_data                              = data.data(),
            .debug_label                            = "Glyph_buffer"
        }
    };
}

void Glyph_buffer::bind(erhe::graphics::Render_command_encoder& encoder)
{
    encoder.set_buffer(
        m_glyph_interface.glyph_block.get_binding_target(),
        &m_buffer,
        0,
        m_buffer.get_capacity_byte_count(),
        m_glyph_interface.glyph_block.get_binding_point()
    );
}

} // namespace erhe::scene_renderer
