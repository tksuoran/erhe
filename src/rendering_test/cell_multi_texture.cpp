#include "rendering_test_app.hpp"

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_graphics/texture_heap.hpp"

#include <filesystem>

namespace rendering_test {

void Rendering_test::make_multi_texture_pipeline()
{
    const std::filesystem::path shader_path = std::filesystem::path{"res"} / std::filesystem::path{"rendering_test"} / std::filesystem::path{"shaders"};

    m_multi_tex_block = std::make_unique<erhe::graphics::Shader_resource>(
        m_graphics_device, "multi_tex", 0, erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_multi_tex_handle_0 = m_multi_tex_block->add_uvec2("texture_handle_0"); // offset  0, size  8
    m_multi_tex_handle_1 = m_multi_tex_block->add_uvec2("texture_handle_1"); // offset  8, size  8
    m_multi_tex_handle_2 = m_multi_tex_block->add_uvec2("texture_handle_2"); // offset 16, size  8
    m_multi_tex_count    = m_multi_tex_block->add_uint ("texture_count");    // offset 24, size  4
    m_multi_tex_padding0 = m_multi_tex_block->add_uint ("padding0");         // offset 28, size  4
                                                                              // total  32 = 2 x vec4

    // s_textures is declared in m_test_bind_group_layout (see
    // cell_textured_quad.cpp::make_quad_pipeline) with array_size=3.
    erhe::graphics::Shader_stages_create_info create_info{
        .name                  = "multi_texture_test",
        .interface_blocks      = { m_multi_tex_block.get() },
        .fragment_outputs      = &m_quad_fragment_outputs,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   shader_path / "multi_texture_test.vert" },
            { erhe::graphics::Shader_type::fragment_shader, shader_path / "multi_texture_test.frag" }
        },
        .bind_group_layout = m_test_bind_group_layout.get()
    };
    m_multi_tex_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(
        m_graphics_device,
        erhe::graphics::build_shader_stages(m_graphics_device, std::move(create_info))
    );

    m_multi_tex_pipeline = erhe::graphics::Lazy_render_pipeline{
        m_graphics_device,
        erhe::graphics::Render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Multi Texture Test Pipeline"},
            .shader_stages  = m_multi_tex_shader_stages.get(),
            .vertex_input   = &m_empty_vertex_input,
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
        }
    };
}

void Rendering_test::draw_multi_texture_quad(
    erhe::graphics::Render_command_encoder&                      encoder,
    const erhe::math::Viewport&                                  viewport,
    const std::vector<std::shared_ptr<erhe::graphics::Texture>>& textures,
    const erhe::graphics::Render_pass&                           render_pass
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const uint32_t tex_count = static_cast<uint32_t>(textures.size());

    // Create texture heap first so we can get descriptor indices for the UBO
    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device, *m_red_texture.get(), m_nearest_sampler,
        m_test_bind_group_layout.get()
    };
    uint64_t handles[3] = {0, 0, 0};
    for (uint32_t i = 0; i < tex_count; ++i) {
        handles[i] = texture_heap.allocate(textures[i].get(), &m_nearest_sampler);
    }

    erhe::graphics::Ring_buffer_range buffer_range = m_quad_buffer.acquire(
        erhe::graphics::Ring_buffer_usage::CPU_write,
        m_multi_tex_block->get_size_bytes()
    );
    const std::span<std::byte> gpu_data   = buffer_range.get_span();
    const std::span<uint32_t>  uint_data  {reinterpret_cast<uint32_t*>(gpu_data.data()), gpu_data.size_bytes() / sizeof(uint32_t)};

    auto write_handle = [&](erhe::graphics::Shader_resource* field, uint64_t handle) {
        const uint32_t h[2] = {
            static_cast<uint32_t>(handle & 0xffffffffu),
            static_cast<uint32_t>(handle >> 32u)
        };
        write(uint_data, field->get_offset_in_parent(), std::span<const uint32_t>{&h[0], 2});
    };

    if (tex_count > 0) write_handle(m_multi_tex_handle_0, handles[0]);
    if (tex_count > 1) write_handle(m_multi_tex_handle_1, handles[1]);
    if (tex_count > 2) write_handle(m_multi_tex_handle_2, handles[2]);
    write(uint_data, m_multi_tex_count->get_offset_in_parent(), std::span<const uint32_t>{&tex_count, 1});

    buffer_range.bytes_written(m_multi_tex_block->get_size_bytes());
    buffer_range.close();

    encoder.set_bind_group_layout(m_test_bind_group_layout.get());
    {
        erhe::graphics::Render_pipeline* p = m_multi_tex_pipeline.get_pipeline_for(render_pass.get_descriptor());
        if (p != nullptr) {
            encoder.set_render_pipeline(*p);
        }
    }
    encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
    m_quad_buffer.bind(encoder, buffer_range);
    texture_heap.bind(encoder);

    encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);

    texture_heap.unbind();
    buffer_range.release();
}

} // namespace rendering_test
