#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/indirect_count_draws.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_compute_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_compute_command_encoder.hpp"
#endif

namespace erhe::graphics {

Compute_command_encoder::Compute_command_encoder(Device& device, Command_buffer& command_buffer)
    : m_impl{device, command_buffer}
{
    static_assert(sizeof(Compute_command_encoder_impl) <= 128);
    static_assert(alignof(Compute_command_encoder_impl) <= 16);
}
Compute_command_encoder::~Compute_command_encoder() noexcept
{
}
void Compute_command_encoder::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    m_impl->set_bind_group_layout(bind_group_layout);
}
void Compute_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    m_impl->set_buffer(buffer_target, buffer, offset, length, index);
}
void Compute_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    m_impl->set_buffer(buffer_target, buffer);
}
void Compute_command_encoder::set_storage_image(uint32_t binding_point, const Texture& texture)
{
    m_impl->set_storage_image(binding_point, texture);
}
void Compute_command_encoder::set_sampled_image(uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    m_impl->set_sampled_image(binding_point, texture, sampler);
}
void Compute_command_encoder::set_acceleration_structure(uint32_t binding_point, const Acceleration_structure& acceleration_structure)
{
    m_impl->set_acceleration_structure(binding_point, acceleration_structure);
}
void Compute_command_encoder::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    m_impl->set_compute_pipeline_state(pipeline);
}
void Compute_command_encoder::set_compute_pipeline(const Compute_pipeline& pipeline)
{
    m_impl->set_compute_pipeline(pipeline);
}
auto Compute_command_encoder::prepare_indexed_indirect_count_draws(
    Indirect_count_draws&                    draws,
    const Indirect_count_draws_prepare_info& prepare_info
) -> Indirect_count_draw_set
{
    ERHE_VERIFY(draws.get_device().get_info().use_draw_indirect_count);
    const Indirect_count_draw_set set = draws.store(prepare_info);
#if defined(ERHE_GRAPHICS_API_METAL)
    // Metal encodes the surviving commands into the slot's indirect command
    // buffer; the other backends draw from the recorded parameters.
    m_impl->encode_indirect_count_draws(draws, set.slot);
#endif
    return set;
}

void Compute_command_encoder::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    m_impl->dispatch_compute(x_size, y_size, z_size);
}
auto Compute_command_encoder::get_command_buffer() -> Command_buffer&
{
    return m_impl->get_command_buffer();
}

auto Compute_command_encoder::get_impl() -> Compute_command_encoder_impl&
{
    return *m_impl;
}

auto Compute_command_encoder::get_impl() const -> const Compute_command_encoder_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
