#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/shader_resource.hpp"

namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
}
namespace erhe::ui {
    class Glyph_outline_set;
}

namespace erhe::scene_renderer {

// Offsets of Glyph_meta struct members.
class Glyph_meta_struct
{
public:
    std::size_t start;   // int   - first curve index (in curve units, 3 vec2 per curve)
    std::size_t count;   // int   - number of curves
    std::size_t bearing; // vec2  - horizontal bearing x, y in em units
    std::size_t size;    // vec2  - width, height in em units
    std::size_t advance; // float - horizontal advance in em units
};

// Shader interface for GPU curve-based glyph rendering (adapted from
// https://github.com/GreenLightning/gpu-font-rendering, MIT).
//
// The "glyph" block holds a fixed array of glyph metadata entries plus a
// flat array of quadratic bezier control points (3 consecutive vec2 per
// curve). Glyph slots follow a fixed convention shared with the shaders:
// slots 0..9 = digits '0'..'9', slot 10 = '-', slot 11 = '.', 12..15 spare.
//
// The feature requires shader storage buffers (unsized vec2 array with
// std430 stride). When the device lacks SSBO support the block falls back
// to a dummy uniform block so the shared bind group layout stays uniform,
// `supported` is false and shaders are compiled without ERHE_GRID_LABELS.
class Glyph_interface
{
public:
    explicit Glyph_interface(erhe::graphics::Device& graphics_device);

    static constexpr std::size_t glyph_slot_count = 16;

    bool                             supported{false};
    erhe::graphics::Shader_resource  glyph_block;
    erhe::graphics::Shader_resource  glyph_meta_struct;
    Glyph_meta_struct                glyph_meta_offsets;
    erhe::graphics::Shader_resource* glyphs_member{nullptr};
    erhe::graphics::Shader_resource* curves_member{nullptr};
};

// Static GPU buffer holding the glyph metadata and curve data described by
// Glyph_interface. Built once at init from erhe::ui::Glyph_outline_set
// (slot order = codepoint order used at extraction). Always creates a
// bindable buffer - empty when unsupported, when the outline set is
// invalid, or when glyph_outline_set is nullptr (executables that never
// draw glyphs) - so bind() is unconditionally legal.
class Glyph_buffer
{
public:
    Glyph_buffer(
        erhe::graphics::Device&            graphics_device,
        Glyph_interface&                   glyph_interface,
        const erhe::ui::Glyph_outline_set* glyph_outline_set
    );

    void bind(erhe::graphics::Render_command_encoder& encoder);

private:
    Glyph_interface&       m_glyph_interface;
    erhe::graphics::Buffer m_buffer;
};

} // namespace erhe::scene_renderer
