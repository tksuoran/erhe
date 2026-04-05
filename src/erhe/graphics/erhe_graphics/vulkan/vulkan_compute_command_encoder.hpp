#include "erhe_graphics/compute_command_encoder.hpp"

#include "volk.h"

namespace erhe::graphics {

class Bind_group_layout;

class Compute_command_encoder_impl final
{
public:
    explicit Compute_command_encoder_impl(Device& device);
    Compute_command_encoder_impl(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl& operator=(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl(Compute_command_encoder_impl&&) = delete;
    Compute_command_encoder_impl& operator=(Compute_command_encoder_impl&&) = delete;
    ~Compute_command_encoder_impl() noexcept;

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer);
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    Device&                      m_device;
    const Bind_group_layout*     m_bind_group_layout{nullptr};
    VkPipelineLayout             m_pipeline_layout  {VK_NULL_HANDLE};
};

} // namespace erhe::graphics
