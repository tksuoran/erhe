#pragma once

#include "erhe_graphics/render_command_encoder.hpp"

#include "volk.h"

namespace erhe::graphics {

class Command_buffer;

class Render_command_encoder_impl final
{
public:
    Render_command_encoder_impl(Device& device, Command_buffer& command_buffer);
    Render_command_encoder_impl(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl& operator=(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl(Render_command_encoder_impl&&) = delete;
    Render_command_encoder_impl& operator=(Render_command_encoder_impl&&) = delete;
    ~Render_command_encoder_impl() noexcept;

    void set_bind_group_layout    (const Bind_group_layout* bind_group_layout);
    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer);
    void set_sampled_image        (uint32_t binding_point, const Texture& texture, const Sampler& sampler);
    void set_render_pipeline      (const Render_pipeline& pipeline);
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

    [[nodiscard]] auto get_command_buffer() -> Command_buffer& { return m_command_buffer; }

private:
    Device&                  m_device;
    Command_buffer&          m_command_buffer;
    const Bind_group_layout* m_bind_group_layout{nullptr};
    VkPipelineLayout         m_pipeline_layout  {VK_NULL_HANDLE};
    VkBuffer                 m_index_buffer     {VK_NULL_HANDLE};
    VkBuffer                 m_indirect_buffer  {VK_NULL_HANDLE};
    VkIndexType              m_index_type       {VK_INDEX_TYPE_UINT32};
    float                    m_viewport_znear   {0.0};
    float                    m_viewport_zfar    {1.0};
};

} // namespace erhe::graphics
