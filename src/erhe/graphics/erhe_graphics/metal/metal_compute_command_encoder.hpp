#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/metal/metal_command_encoder.hpp"

#include <array>
#include <cstdint>

namespace MTL { class CommandBuffer; }
namespace MTL { class ComputeCommandEncoder; }
namespace MTL { class ComputePipelineState; }
namespace MTL { class Fence; }

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

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void set_compute_pipeline      (const Compute_pipeline& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    MTL::ComputeCommandEncoder*  m_encoder             {nullptr};
    MTL::ComputePipelineState*   m_pipeline_state      {nullptr};
    bool                         m_owns_pipeline_state {false};
    // Per-group thread dimensions for dispatchThreadgroups(), taken from the
    // bound shader's layout(local_size_*). Defaults to 1x1x1 until a pipeline
    // is set.
    std::array<uint32_t, 3>      m_threadgroup_size    {1, 1, 1};
    // Non-owning borrow of the cb's MTL::CommandBuffer (for setting the
    // debug label) and inter-encoder fence (for ordering against other
    // encoders on the same cb).
    MTL::CommandBuffer*          m_mtl_command_buffer  {nullptr};
    MTL::Fence*                  m_inter_encoder_fence {nullptr};
};

} // namespace erhe::graphics
