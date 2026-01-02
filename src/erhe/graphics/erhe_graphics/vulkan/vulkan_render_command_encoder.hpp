#include "erhe_graphics/Render_command_encoder.hpp"

#include "volk.h"

namespace erhe::graphics {

class Render_command_encoder_impl final
{
public:
    Render_command_encoder_impl(Device& device, Render_pass& render_pass);
    Render_command_encoder_impl(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl& operator=(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl(Render_command_encoder_impl&&) = delete;
    Render_command_encoder_impl& operator=(Render_command_encoder_impl&&) = delete;
    ~Render_command_encoder_impl() noexcept;

    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer               (Buffer_target buffer_target, const Buffer* buffer);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline, const Shader_stages* override_shader_stages);
    void set_viewport_rect        (int x, int y, int width, int height);
    void set_viewport_depth_range (float min_depth, float max_depth);
    void set_scissor_rect         (int x, int y, int width, int height);
    void set_index_buffer         (const Buffer* buffer);
    void set_vertex_buffer        (const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index);
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count, std::uintptr_t instance_count);
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count);
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset, std::uintptr_t instance_count);
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset);

    void multi_draw_indexed_primitives_indirect(
        Primitive_type           primitive_type,
        erhe::dataformat::Format index_type,
        std::uintptr_t           indirect_offset,
        std::uintptr_t           drawcount,
        std::uintptr_t           stride
    );

private:
    void start_render_pass();
    void end_render_pass();

    Device&         m_device;
    Render_pass&    m_render_pass;
    VkCommandBuffer m_command_buffer{VK_NULL_HANDLE};
};

}