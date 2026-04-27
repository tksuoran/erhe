// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/device.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_METAL)
# include "erhe_graphics/metal/metal_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_NONE)
# include "erhe_graphics/null/null_device.hpp"
#endif

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::graphics {

Device::Device(
    const Surface_create_info&      surface_create_info,
    const Graphics_config&          graphics_config,
    Device_message_callback         device_message_callback,
    const Vulkan_external_creators* vulkan_external_creators
)
    : m_device_message_callback{std::move(device_message_callback)}
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    , m_impl      {std::make_unique<Device_impl>(*this, surface_create_info, graphics_config, vulkan_external_creators)}
#else
    , m_impl      {std::make_unique<Device_impl>(*this, surface_create_info, graphics_config)}
#endif
    , m_spirv_cache{std::filesystem::path{"spirv_cache"}}
{
#if !defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    static_cast<void>(vulkan_external_creators);
#endif
}

Device::~Device() noexcept = default;

auto Device::get_surface() -> Surface*
{
    return m_impl->get_surface();
}
auto Device::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    return m_impl->get_handle(texture, sampler);
}
auto Device::create_dummy_texture(Command_buffer& init_command_buffer, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    return m_impl->create_dummy_texture(init_command_buffer, format);
}
auto Device::get_command_buffer(unsigned int thread_slot) -> Command_buffer&
{
    return m_impl->get_command_buffer(thread_slot);
}
void Device::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    m_impl->submit_command_buffers(command_buffers);
}
void Device::add_completion_handler(std::function<void()> callback)
{
    m_impl->add_completion_handler(
        [cb = std::move(callback)](Device_impl&) {
            cb();
        }
    );
}
void Device::on_thread_enter()
{
    m_impl->on_thread_enter();
}
auto Device::get_buffer_alignment(Buffer_target target) -> std::size_t
{
    return m_impl->get_buffer_alignment(target);
}
auto Device::wait_frame() -> bool
{
    return m_impl->wait_frame();
}
auto Device::begin_frame() -> bool
{
    return m_impl->begin_frame();
}
auto Device::end_frame() -> bool
{
    return m_impl->end_frame();
}
auto Device::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    return m_impl->begin_frame(frame_begin_info);
}
auto Device::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    return m_impl->end_frame(frame_end_info);
}
void Device::wait_idle()
{
    m_impl->wait_idle();
}
auto Device::is_in_swapchain_frame() const -> bool
{
    return m_impl->is_in_swapchain_frame();
}
auto Device::get_frame_index() const -> uint64_t
{
    return m_impl->get_frame_index();
}
auto Device::allocate_ring_buffer_entry(Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range
{
    return m_impl->allocate_ring_buffer_entry(buffer_target, usage, byte_count);
}
auto Device::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    return m_impl->get_format_properties(format);
}
auto Device::probe_image_format_support(erhe::dataformat::Format format, uint64_t usage_mask) const -> bool
{
    return m_impl->probe_image_format_support(format, usage_mask);
}
auto Device::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    return m_impl->get_supported_depth_stencil_formats();
}
void Device::sort_depth_stencil_formats(std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const
{
    m_impl->sort_depth_stencil_formats(formats, sort_flags, requested_sample_count);
}
auto Device::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    return m_impl->choose_depth_stencil_format(formats);
}
auto Device::choose_depth_stencil_format(const unsigned int sort_flags, const int requested_sample_count) const -> erhe::dataformat::Format
{
    return m_impl->choose_depth_stencil_format(sort_flags, requested_sample_count);
}
void Device::start_frame_capture()
{
    m_impl->start_frame_capture();
}
void Device::end_frame_capture()
{
    m_impl->end_frame_capture();
}
auto Device::make_render_command_encoder(Command_buffer& command_buffer) -> Render_command_encoder
{
    return m_impl->make_render_command_encoder(command_buffer);
}

auto Device::create_render_pipeline(const Render_pipeline_create_info& create_info) -> std::unique_ptr<Render_pipeline>
{
    return std::make_unique<Render_pipeline>(*this, create_info);
}
auto Device::make_blit_command_encoder(Command_buffer& command_buffer) -> Blit_command_encoder
{
    return m_impl->make_blit_command_encoder(command_buffer);
}
auto Device::make_compute_command_encoder(Command_buffer& command_buffer) -> Compute_command_encoder
{
    return m_impl->make_compute_command_encoder(command_buffer);
}
auto Device::get_shader_monitor() -> Shader_monitor&
{
    return m_impl->get_shader_monitor();
}
auto Device::get_info() const -> const Device_info&
{
    return m_impl->get_info();
}
auto Device::get_graphics_config() const -> const Graphics_config&
{
    return m_impl->get_graphics_config();
}
auto Device::get_impl() -> Device_impl&
{
    return *m_impl.get();
}
auto Device::get_impl() const -> const Device_impl&
{
    return *m_impl.get();
}
auto Device::get_spirv_cache() -> Spirv_cache&
{
    return m_spirv_cache;
}
void Device::set_shader_error_callback(Shader_error_callback callback)
{
    m_shader_error_callback = std::move(callback);
}
void Device::shader_error(const std::string& error_log, const std::string& shader_source)
{
    if (m_shader_error_callback) {
        m_shader_error_callback(error_log, shader_source, erhe_get_callstack());
    }
}
void Device::device_message(Message_severity severity, const std::string& message)
{
    if (m_device_message_callback) {
        m_device_message_callback(severity, message, erhe_get_callstack());
    }
}
void Device::set_state_dump_callback(State_dump_callback callback)
{
    m_state_dump_callback = std::move(callback);
}
void Device::state_dump(const std::string& dump)
{
    if (m_state_dump_callback) {
        m_state_dump_callback(dump);
    }
}
void Device::set_trace_callback(Trace_callback callback)
{
    m_trace_callback = std::move(callback);
}
void Device::trace(const std::string& message)
{
    if (m_trace_callback) {
        m_trace_callback(message);
    }
}
auto Device::get_active_render_pass() const -> Render_pass*
{
    return m_active_render_pass;
}
void Device::set_active_render_pass(Render_pass* render_pass)
{
    m_active_render_pass = render_pass;
}
auto Device::get_native_handles() const -> Native_device_handles
{
    return m_impl->get_native_handles();
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
