#pragma once

#include "erhe_graphics/command_encoder.hpp"

#include <cstdint>

namespace erhe::graphics {

class Buffer;
class Device;
class Compute_pipeline_state;
class Compute_command_encoder_impl;

class Compute_command_encoder final : public Command_encoder
{
public:
    explicit Compute_command_encoder(Device& device);
    Compute_command_encoder(const Compute_command_encoder&) = delete;
    Compute_command_encoder& operator=(const Compute_command_encoder&) = delete;
    Compute_command_encoder(Compute_command_encoder&&) = delete;
    Compute_command_encoder& operator=(Compute_command_encoder&&) = delete;
    ~Compute_command_encoder() noexcept;

    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) override;
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer) override;
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    std::unique_ptr<Compute_command_encoder_impl> m_impl;
};

} // namespace erhe::graphics
