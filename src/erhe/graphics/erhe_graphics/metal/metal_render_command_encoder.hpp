#pragma once

#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/metal/metal_command_encoder.hpp"

namespace MTL { class Buffer; }
namespace MTL { class IndirectCommandBuffer; }
namespace MTL { class RenderCommandEncoder; }

namespace erhe::graphics {

class Indirect_count_draws;

class Render_command_encoder_impl final : public Command_encoder_impl
{
public:
    Render_command_encoder_impl(Device& device, Command_buffer& command_buffer);
    Render_command_encoder_impl(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl& operator=(const Render_command_encoder_impl&) = delete;
    Render_command_encoder_impl(Render_command_encoder_impl&&) = delete;
    Render_command_encoder_impl& operator=(Render_command_encoder_impl&&) = delete;
    ~Render_command_encoder_impl() noexcept;

    void set_bind_group_layout    (const Bind_group_layout* bind_group_layout);
    void set_sampled_image        (uint32_t binding_point, const Texture& texture, const Sampler& sampler);
    void set_render_pipeline      (const Render_pipeline& pipeline);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline);
    void set_render_pipeline_state(const Render_pipeline_state& pipeline, const Shader_stages* override_shader_stages);
    void set_viewport_rect        (int x, int y, int width, int height);
    void set_viewport_depth_range (float min_depth, float max_depth);
    void set_scissor_rect         (int x, int y, int width, int height);
    void set_depth_bias           (float constant_factor, float slope_factor, float clamp);
    void set_index_buffer         (const Buffer* buffer);
    void set_vertex_buffer        (const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index);
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count, std::uintptr_t instance_count) const;
    void draw_primitives          (Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count) const;
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset, std::uintptr_t instance_count) const;
    void draw_indexed_primitives  (Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset) const;

    void dump_state(const char* label) const;

    void multi_draw_indexed_primitives_indirect(
        Primitive_type           primitive_type,
        erhe::dataformat::Format index_type,
        std::uintptr_t           indirect_offset,
        std::uintptr_t           drawcount,
        std::uintptr_t           stride
    ) const;
    void multi_draw_indexed_primitives_indirect_count(
        Primitive_type           primitive_type,
        erhe::dataformat::Format index_type,
        std::uintptr_t           indirect_offset,
        const Buffer&            count_buffer,
        std::uintptr_t           count_offset,
        std::uintptr_t           max_draw_count,
        std::uintptr_t           stride
    ) const;

    // Render_command_encoder::multi_draw_indexed_primitives_indirect_count
    // (prepared set): executes the job's range of the slot's indirect
    // command buffer.
    void execute_indirect_count_draws(const Indirect_count_draws& draws, std::size_t slot, uint32_t job_index);

private:
    void apply_viewport();

    const Bind_group_layout* m_bind_group_layout{nullptr};
    MTL::Buffer* m_index_buffer    {nullptr};
    // Prepared count draws: whether the bound pipeline supports indirect
    // command buffers, and the ICB and index buffer last made resident on
    // m_resident_encoder (execute_indirect_count_draws()).
    bool                              m_pipeline_supports_indirect_count_draws{false};
    const MTL::RenderCommandEncoder*  m_resident_encoder     {nullptr};
    const MTL::IndirectCommandBuffer* m_resident_icb         {nullptr};
    const MTL::Buffer*                m_resident_index_buffer{nullptr};
    int          m_viewport_x      {0};
    int          m_viewport_y      {0};
    int          m_viewport_width  {0};
    int          m_viewport_height {0};
    double       m_viewport_znear  {0.0};
    double       m_viewport_zfar   {1.0};
};

} // namespace erhe::graphics
