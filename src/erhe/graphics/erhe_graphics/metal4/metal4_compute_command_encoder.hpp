#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/metal4/metal4_command_encoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MTL  { class ComputePipelineState; }
namespace MTL4 { class ArgumentTable; }
namespace MTL4 { class CommandBuffer; }
namespace MTL4 { class ComputeCommandEncoder; }
namespace MTL4 { class CounterHeap; }

namespace erhe::graphics {

class Compute_command_encoder_impl final : public Command_encoder_impl
{
public:
    Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer);
    Compute_command_encoder_impl(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl& operator=(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl(Compute_command_encoder_impl&&) = delete;
    Compute_command_encoder_impl& operator=(Compute_command_encoder_impl&&) = delete;
    ~Compute_command_encoder_impl() noexcept;

    void set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer(Buffer_target buffer_target, const Buffer* buffer);
    void set_storage_image         (uint32_t binding_point, const Texture& texture);
    void set_sampled_image         (uint32_t binding_point, const Texture& texture, const Sampler& sampler);
    void set_acceleration_structure(uint32_t binding_point, const Acceleration_structure& acceleration_structure);

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void set_compute_pipeline      (const Compute_pipeline& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);
    // Compute_command_encoder::prepare_indexed_indirect_count_draws: the
    // encoding dispatch of the prepared slot; unbinds the erhe pipeline.
    void encode_indirect_count_draws(Indirect_count_draws& draws, std::size_t slot);

    // Backend access for Texture_heap_impl::bind(Compute_command_encoder&),
    // which binds the texture argument buffer in the encoder's argument
    // table, and for Command_buffer_impl::memory_barrier. Static because
    // Texture_heap only sees the public pimpl'd Compute_command_encoder;
    // mirrors Render_pass_impl::get_active_mtl_encoder() (erhe opens one
    // encoder at a time on a cb, so at most one is live).
    [[nodiscard]] static auto get_active_mtl_encoder   () noexcept -> MTL4::ComputeCommandEncoder*;
    [[nodiscard]] static auto get_active_argument_table() noexcept -> MTL4::ArgumentTable*;

private:
    // Per thread, as Device_impl::s_active_render_pass.
    static thread_local Compute_command_encoder_impl* s_active_encoder;

    // Dispatches of one MTL4 compute encoder run concurrently: every
    // dispatch after the first starts with an intra-encoder barrier.
    void before_dispatch();
    void release_owned_pipeline_state();

    MTL4::ComputeCommandEncoder* m_encoder             {nullptr};
    MTL4::ArgumentTable*         m_argument_table      {nullptr};
    std::size_t                  m_dispatch_count      {0};
    MTL4::CounterHeap*           m_timer_counter_heap  {nullptr};
    std::size_t                  m_timer_end_sample    {0};
    const Bind_group_layout*     m_bind_group_layout   {nullptr};
    MTL::ComputePipelineState*   m_pipeline_state      {nullptr};
    bool                         m_owns_pipeline_state {false};
    // Per-group thread dimensions for dispatchThreadgroups(), taken from the
    // bound shader's layout(local_size_*). Defaults to 1x1x1 until a pipeline
    // is set.
    std::array<uint32_t, 3>      m_threadgroup_size    {1, 1, 1};
    // Non-owning borrow of the cb's MTL4::CommandBuffer (for setting the
    // debug label).
    MTL4::CommandBuffer*         m_mtl_command_buffer  {nullptr};
};

} // namespace erhe::graphics
