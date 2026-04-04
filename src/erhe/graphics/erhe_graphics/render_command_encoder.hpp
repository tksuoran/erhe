#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <cstdint>
#include <memory>

namespace erhe::graphics {

class Bind_group_layout;
class Buffer;
class Device;
class Render_pass;
class Render_pipeline_state;
class Shader_stages;
class Render_command_encoder_impl;

class Render_command_encoder final : public Command_encoder
{
public:
    Render_command_encoder(Device& device);
    Render_command_encoder(const Render_command_encoder&) = delete;
    Render_command_encoder& operator=(const Render_command_encoder&) = delete;
    Render_command_encoder(Render_command_encoder&&) = delete;
    Render_command_encoder& operator=(Render_command_encoder&&) = delete;
    ~Render_command_encoder() noexcept override;

    void set_bind_group_layout    (const Bind_group_layout* bind_group_layout);
    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) override;
    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer) override;
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

private:
    erhe::utility::pimpl_ptr<Render_command_encoder_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
