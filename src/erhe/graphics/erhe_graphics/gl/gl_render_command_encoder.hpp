#pragma once

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/gl/gl_command_encoder.hpp"

namespace erhe::graphics {

class Render_command_encoder_impl final : public Command_encoder_impl
{
public:
    Render_command_encoder_impl(Device& device);
    Render_command_encoder_impl(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl& operator=(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl(Render_command_encoder_impl&&) = delete;
    Render_command_encoder_impl& operator=(Render_command_encoder_impl&&) = delete;
    ~Render_command_encoder_impl() noexcept;

    void set_bind_group_layout    (const Bind_group_layout* bind_group_layout);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline, const Shader_stages* override_shader_stages);
    void set_viewport_rect        (int x, int y, int width, int height);
    void set_viewport_depth_range (float min_depth, float max_depth);
    void set_scissor_rect         (int x, int y, int width, int height);
    void set_index_buffer         (const Buffer* buffer);
    void set_vertex_buffer        (const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index);
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count, std::uintptr_t instance_count) const;
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count) const;
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset, std::uintptr_t instance_count) const;
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset) const;

    void multi_draw_indexed_primitives_indirect(
        Primitive_type           primitive_type,
        erhe::dataformat::Format index_type,
        std::uintptr_t           indirect_offset,
        std::uintptr_t           drawcount,
        std::uintptr_t           stride
    ) const;

    void dump_state(const char* label) const;
};

} // namespace erhe::graphics
