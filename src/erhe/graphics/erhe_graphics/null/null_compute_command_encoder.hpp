#pragma once

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/null/null_command_encoder.hpp"

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

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void set_compute_pipeline      (const Compute_pipeline& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);
};

} // namespace erhe::graphics
