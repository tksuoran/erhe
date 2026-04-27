#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/metal/metal_command_encoder.hpp"

#include <mutex>

namespace MTL { class ComputeCommandEncoder; }
namespace MTL { class ComputePipelineState; }
namespace MTL { class CommandBuffer; }

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
    MTL::CommandBuffer*          m_command_buffer      {nullptr};
    // True when m_command_buffer was allocated by this encoder (no
    // device frame was active). In that case the destructor commits it.
    // False when it's the device frame cb borrowed from Device_impl;
    // Device_impl::end_frame commits.
    bool                         m_owns_command_buffer {false};
    // Held while recording into the device frame cb so other encoders
    // don't interleave with us. Released after endEncoding in the dtor.
    std::unique_lock<std::mutex> m_recording_lock      {};
};

} // namespace erhe::graphics
