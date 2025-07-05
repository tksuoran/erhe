#pragma once

#include "erhe_graphics/command_encoder.hpp"

#include <cstdint>

namespace erhe::graphics {

class Buffer;
class Device;
class Compute_pipeline_state;

class Compute_command_encoder final : public Command_encoder
{
public:
    explicit Compute_command_encoder(Device& device);
    Compute_command_encoder(const Compute_command_encoder&) = delete;
    Compute_command_encoder& operator=(const Compute_command_encoder&) = delete;
    Compute_command_encoder(Compute_command_encoder&&) = delete;
    Compute_command_encoder& operator=(Compute_command_encoder&&) = delete;
    ~Compute_command_encoder() override;

    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
};

} // namespace erhe::graphics
