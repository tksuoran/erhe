#include "rendering_test_app.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture_heap.hpp"

#include <filesystem>

namespace rendering_test {

void Rendering_test::make_quad_pipeline()
{
    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};

    // Shared between the textured-quad and multi-texture cells, so the
    // s_textures array is sized to the largest caller (3, for multi-texture).
    // The quad cell only uses s_textures[0]; the extra slots are unused
    // but harmless.
    m_test_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        m_graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {
                    .binding_point   = 0,
                    .type            = erhe::graphics::Binding_type::uniform_buffer
                },
                {
                    .binding_point   = 0,
                    .type            = erhe::graphics::Binding_type::combined_image_sampler,
                    .sampler_aspect  = erhe::graphics::Sampler_aspect::color,
                    .name            = "s_textures",
                    .glsl_type       = erhe::graphics::Glsl_type::sampler_2d,
                    .is_texture_heap = true,
                    .array_size      = 3
                }
            },
            .debug_label = "Rendering test"
        }
    );
    m_quad_block = std::make_unique<erhe::graphics::Shader_resource>(
        m_graphics_device, "quad", 0, erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_quad_texture_handle = m_quad_block->add_uvec2("texture_handle"); // offset  0, size 8
    m_quad_uv_min         = m_quad_block->add_vec2("uv_min");       // offset  8, size 8
    m_quad_uv_max         = m_quad_block->add_vec2("uv_max");       // offset 16, size 8
    m_quad_padding        = m_quad_block->add_vec2("padding");      // offset 24, size 8 -> total 32 = 2 x vec4

    erhe::graphics::Shader_stages_create_info create_info{
        .name              = "textured_quad",
        .interface_blocks  = { m_quad_block.get() },
        .fragment_outputs  = &m_quad_fragment_outputs,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   shader_path / "textured_quad.vert" },
            { erhe::graphics::Shader_type::fragment_shader, shader_path / "textured_quad.frag" }
        },
        .bind_group_layout = m_test_bind_group_layout.get()
    };
    m_quad_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
        m_graphics_device,
        erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info))
    );

    m_quad_pipeline = erhe::graphics::Lazy_render_pipeline{
        m_graphics_device,
        erhe::graphics::Render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Textured Quad Pipeline"},
            .shader_stages  = m_quad_shader_stages.get(),
            .vertex_input   = &m_empty_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
        }
    };
}

// Cell (1,0): render-to-texture displayed via a fullscreen quad. The
// rendering_test scene was rendered into m_color_texture earlier in the
// frame; here we sample it through the texture heap and draw it as a quad
// in the swapchain pass.
void Rendering_test::draw_textured_quad_cell(
    erhe::graphics::Render_command_encoder& encoder,
    const erhe::math::Viewport&             viewport
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const auto& conventions = m_graphics_device.get_info().coordinate_conventions;
    const bool top_left = (conventions.texture_origin == erhe::math::Texture_origin::top_left);
    const glm::vec2 uv_min = top_left ? glm::vec2{0.0f, 1.0f} : glm::vec2{0.0f, 0.0f};
    const glm::vec2 uv_max = top_left ? glm::vec2{1.0f, 0.0f} : glm::vec2{1.0f, 1.0f};

    erhe::graphics::Ring_buffer_range quad_buffer_range = m_quad_buffer.acquire(
        erhe::graphics::Ring_buffer_usage::CPU_write,
        m_quad_block->get_size_bytes()
    );
    const std::span<std::byte> gpu_data   = quad_buffer_range.get_span();
    const std::span<float>     float_data{reinterpret_cast<float*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(float)};
    const std::span<uint32_t>  uint_data {reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device, *m_color_texture.get(), m_nearest_sampler,
        m_test_bind_group_layout.get()
    };
    const uint64_t handle = texture_heap.allocate(m_color_texture.get(), &m_nearest_sampler);
    const uint32_t texture_handle[2] = {
        static_cast<uint32_t>(handle & 0xffffffffu),
        static_cast<uint32_t>(handle >> 32u)
    };
    write(uint_data,  m_quad_texture_handle->get_offset_in_parent(), std::span<const uint32_t>{&texture_handle[0], 2});
    write(float_data, m_quad_uv_min->get_offset_in_parent(),         as_span(uv_min));
    write(float_data, m_quad_uv_max->get_offset_in_parent(),         as_span(uv_max));
    quad_buffer_range.bytes_written(m_quad_block->get_size_bytes());
    quad_buffer_range.close();

    encoder.set_bind_group_layout(m_test_bind_group_layout.get());
    {
        erhe::graphics::Render_pipeline* p = m_quad_pipeline.get_pipeline_for(m_swapchain_render_pass->get_descriptor());
        if (p != nullptr) {
            encoder.set_render_pipeline(*p);
        }
    }
    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    m_quad_buffer.bind(encoder, quad_buffer_range);
    texture_heap.bind(encoder);

    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

    texture_heap.unbind();
    quad_buffer_range.release();
}

} // namespace rendering_test
