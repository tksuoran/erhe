#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/metal/metal_command_encoder.hpp"

namespace MTL { class ComputeCommandEncoder; }
namespace MTL { class ComputePipelineState; }
namespace MTL { class CommandBuffer; }

namespace erhe::graphics {

class Compute_command_encoder_impl final : public Command_encoder_impl
{
public:
    explicit Compute_command_encoder_impl(Device& device);
    Compute_command_encoder_impl(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl& operator=(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl(Compute_command_encoder_impl&&) = delete;
    Compute_command_encoder_impl& operator=(Compute_command_encoder_impl&&) = delete;
    ~Compute_command_encoder_impl() noexcept;

    void set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer(Buffer_target buffer_target, const Buffer* buffer);

    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    MTL::ComputeCommandEncoder* m_encoder       {nullptr};
    MTL::ComputePipelineState*  m_pipeline_state{nullptr};
    MTL::CommandBuffer*         m_command_buffer{nullptr};
};

} // namespace erhe::graphics
