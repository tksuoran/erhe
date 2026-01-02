#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_command_encoder.hpp"

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

    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);
};

} // namespace erhe::graphics
