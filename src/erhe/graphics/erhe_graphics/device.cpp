// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/device.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_device.hpp"
#endif

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::graphics {

Device::Device(const Surface_create_info& surface_create_info)
    : m_impl{std::make_unique<Device_impl>(*this, surface_create_info)}
{
}
Device::~Device() = default;
auto Device::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> erhe::dataformat::Format
{
    return m_impl->choose_depth_stencil_format(flags, sample_count);
}
auto Device::get_surface() -> Surface*
{
    return m_impl->get_surface();
}
auto Device::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    return m_impl->get_handle(texture, sampler);
}
auto Device::create_dummy_texture() -> std::shared_ptr<Texture>
{
    return m_impl->create_dummy_texture();
}
void Device::upload_to_buffer(Buffer& buffer, size_t offset, const void* data, size_t length)
{
    m_impl->upload_to_buffer(buffer, offset, data, length);
}
void Device::add_completion_handler(std::function<void()> callback)
{
    m_impl->add_completion_handler(callback);
}
void Device::on_thread_enter()
{
    m_impl->on_thread_enter();
}
auto Device::get_buffer_alignment(Buffer_target target) -> std::size_t
{
    return m_impl->get_buffer_alignment(target);
}
void Device::wait_frame(Frame_state& out_frame_state)
{
    m_impl->wait_frame(out_frame_state);
}
void Device::begin_frame(const Frame_begin_info& frame_begin_info)
{
    m_impl->begin_frame(frame_begin_info);
}
void Device::end_frame(const Frame_end_info& frame_end_info)
{
    m_impl->end_frame(frame_end_info);
}
auto Device::get_frame_index() const -> uint64_t
{
    return m_impl->get_frame_index();
}
auto Device::allocate_ring_buffer_entry(Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range
{
    return m_impl->allocate_ring_buffer_entry(buffer_target, usage, byte_count);
}
void Device::memory_barrier(Memory_barrier_mask barriers)
{
    m_impl->memory_barrier(barriers);
}
auto Device::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    return m_impl->get_format_properties(format);
}
void Device::clear_texture(Texture& texture, std::array<double, 4> value)
{
    m_impl->clear_texture(texture, value);
}
auto Device::make_render_command_encoder(Render_pass& render_pass) -> Render_command_encoder
{
    return m_impl->make_render_command_encoder(render_pass);
}
auto Device::make_blit_command_encoder() -> Blit_command_encoder
{
    return m_impl->make_blit_command_encoder();
}
auto Device::make_compute_command_encoder() -> Compute_command_encoder
{
    return m_impl->make_compute_command_encoder();
}
auto Device::get_shader_monitor() -> Shader_monitor&
{
    return m_impl->get_shader_monitor();
}
auto Device::get_info() const -> const Device_info&
{
    return m_impl->get_info();
}
auto Device::get_impl() -> Device_impl&
{
    return *m_impl.get();
}
auto Device::get_impl() const -> const Device_impl&
{
    return *m_impl.get();
}

// // // // //

constexpr float c_float_zero_value{0.0f};
constexpr float c_float_one_value {1.0f};

auto get_depth_clear_value_pointer(const bool reverse_depth) -> const float*
{
    return reverse_depth ? &c_float_zero_value : &c_float_one_value;
}

auto get_depth_function(const Compare_operation depth_function, const bool reverse_depth) -> Compare_operation
{
    return reverse_depth ? reverse(depth_function) : depth_function;
}

//
//


} // namespace erhe::graphics
