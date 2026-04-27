#pragma once

#include "erhe_graphics/command_encoder.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <cstdint>
#include <memory>

namespace erhe::graphics {

class Bind_group_layout;
class Buffer;
class Command_buffer;
class Compute_pipeline;
class Compute_pipeline_state;
class Compute_command_encoder_impl;
class Device;

class Compute_command_encoder final : public Command_encoder
{
public:
    Compute_command_encoder(Device& device, Command_buffer& command_buffer);
    Compute_command_encoder(const Compute_command_encoder&) = delete;
    Compute_command_encoder& operator=(const Compute_command_encoder&) = delete;
    Compute_command_encoder(Compute_command_encoder&&) = delete;
    Compute_command_encoder& operator=(Compute_command_encoder&&) = delete;
    ~Compute_command_encoder() noexcept override;

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) override;
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer) override;
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void set_compute_pipeline      (const Compute_pipeline& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    erhe::utility::pimpl_ptr<Compute_command_encoder_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
