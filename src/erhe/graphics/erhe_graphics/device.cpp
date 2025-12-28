// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/device.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_device.hpp"
#endif

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"

namespace erhe::graphics {

Device::Device(const Surface_create_info& surface_create_info)
    : m_impl{std::make_unique<Device_impl>(*this, surface_create_info)}
{
}
Device::~Device() = default;
auto Device::get_surface() -> Surface*
{
    return m_impl->get_surface();
}
void Device::add_completion_handler(std::function<void()> callback)
{
    m_impl->add_completion_handler(callback);
}
void Device::on_thread_enter()
{
    m_impl->on_thread_enter();
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
auto Device::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    return m_impl->get_format_properties(format);
}
auto Device::make_render_command_encoder(Render_pass& render_pass) -> Render_command_encoder
{
    return m_impl->make_render_command_encoder(render_pass);
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
