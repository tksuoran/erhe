#include "erhe_graphics/compute_command_encoder.hpp"

#include "volk.h"

namespace erhe::graphics {

class Bind_group_layout;
class Command_buffer;

class Compute_command_encoder_impl final
{
public:
    Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer);
    Compute_command_encoder_impl(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl& operator=(const Compute_command_encoder_impl&) = delete;
    Compute_command_encoder_impl(Compute_command_encoder_impl&&) = delete;
    Compute_command_encoder_impl& operator=(Compute_command_encoder_impl&&) = delete;
    ~Compute_command_encoder_impl() noexcept;

    void set_bind_group_layout     (const Bind_group_layout* bind_group_layout);
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer                (Buffer_target buffer_target, const Buffer* buffer);
    void set_compute_pipeline_state(const Compute_pipeline_state& pipeline);
    void set_compute_pipeline      (const Compute_pipeline& pipeline);
    void dispatch_compute          (std::uintptr_t x_size, std::uintptr_t y_size, std::uintptr_t z_size);

private:
    // Returns the VkCommandBuffer this encoder records into.
    // Sourced from m_command_buffer; this encoder no longer falls back
    // to an internal immediate-commands cb -- the caller must supply a
    // Command_buffer that is already in the recording state.
    auto get_active_vk_command_buffer() -> VkCommandBuffer;

    Device&                  m_device;
    Command_buffer&          m_command_buffer;
    const Bind_group_layout* m_bind_group_layout{nullptr};
    VkPipelineLayout         m_pipeline_layout  {VK_NULL_HANDLE};
};

} // namespace erhe::graphics
