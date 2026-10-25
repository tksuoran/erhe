// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/display_refresh_resolver.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/present_calibration_cache.hpp"
#include "erhe_graphics/thermal_state.hpp"

#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_source_cache.hpp"
#if defined(ERHE_SPIRV)
#   include "erhe_graphics/spirv_cache.hpp"
#endif
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL4)
# include "erhe_graphics/metal4/metal4_device.hpp"
#elif defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_device.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_device.hpp"
#endif

#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace erhe::graphics {

namespace {

// The present calibration cache (doc/erhe/graphics.md "Present calibration
// cache"): reads follow the graphics config, and the environment variable
// ERHE_PRESENT_CALIBRATION_CACHE=0 disables them for scripts.
auto make_present_calibration_store(const Graphics_config& graphics_config) -> std::unique_ptr<Present_calibration_store>
{
    bool reads = graphics_config.present_calibration_cache;
    const char* const env = std::getenv("ERHE_PRESENT_CALIBRATION_CACHE");
    if ((env != nullptr) && (std::string_view{env} == "0")) {
        reads = false;
    }
    return std::make_unique<Present_calibration_store>(
        reads ? Present_calibration_cache_reads::enabled : Present_calibration_cache_reads::disabled,
        std::filesystem::path{"cache"} / "present_calibration"
    );
}

} // anonymous namespace

Device::Device(
    const Surface_create_info&      surface_create_info,
    const Graphics_config&          graphics_config,
    Device_message_callback         device_message_callback,
    const Vulkan_external_creators* vulkan_external_creators
)
    : m_device_message_callback{std::move(device_message_callback)}
    , m_frame_time_recorder{std::make_unique<erhe::frame_pacing::Frame_time_recorder>()}
    , m_display_refresh_resolver{make_display_refresh_resolver(graphics_config)}
    , m_present_calibration_store{make_present_calibration_store(graphics_config)}
#if defined(ERHE_GRAPHICS_API_VULKAN)
    , m_impl      {std::make_unique<Device_impl>(*this, surface_create_info, graphics_config, vulkan_external_creators)}
#else
    , m_impl      {std::make_unique<Device_impl>(*this, surface_create_info, graphics_config)}
#endif
#if defined(ERHE_SPIRV)
    , m_spirv_cache{std::make_unique<Spirv_cache>(std::filesystem::path{"spirv_cache"})}
#endif
    , m_shader_source_cache{std::make_unique<Shader_source_cache>()}
{
#if !defined(ERHE_GRAPHICS_API_VULKAN)
    static_cast<void>(vulkan_external_creators);
#endif
    // Runs after the member-init list, so m_impl is wired and per-context
    // resources whose lifetime reaches back through Device::get_impl()
    // (the GL default vertex input state) can exist. No-op on backends
    // with no per-context objects.
    m_impl->create_per_context_resources();
    start_thermal_state_logging();
    m_next_frame_start_id = m_impl->get_frame_index();
}

Device::~Device() noexcept = default;

auto Device::get_surface() -> Surface*
{
    return m_impl->get_surface();
}
auto Device::get_surface_transform() -> Surface_transform
{
    Surface* surface = m_impl->get_surface();
    if (surface == nullptr) {
        return Surface_transform::identity;
    }
    return surface->get_surface_transform();
}
auto Device::capture_last_frame(
    int&                      out_width,
    int&                      out_height,
    erhe::dataformat::Format& out_format,
    std::vector<std::byte>&   out_pixels
) -> bool
{
#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_METAL) || defined(ERHE_GRAPHICS_API_OPENGL)
    return m_impl->capture_last_frame(out_width, out_height, out_format, out_pixels);
#else
    // Frame capture is implemented for the Vulkan, Metal and OpenGL backends.
    static_cast<void>(out_width);
    static_cast<void>(out_height);
    static_cast<void>(out_format);
    static_cast<void>(out_pixels);
    return false;
#endif
}
auto Device::request_frame_capture() -> bool
{
#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_METAL) || defined(ERHE_GRAPHICS_API_OPENGL)
    return m_impl->request_frame_capture();
#else
    return false;
#endif
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
    // The main thread records nothing during M1 (doc/editor/render_thread.md
    // section 2.3).
    erhe::utility::Thread_roles::assert_command_recording_allowed();
    return m_impl->get_command_buffer(thread_slot);
}
void Device::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    m_impl->submit_command_buffers(command_buffers);
}
void Device::submit_command_buffer_and_wait(Command_buffer& command_buffer)
{
    m_impl->submit_command_buffer_and_wait(command_buffer);
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
auto Device::supports_worker_contexts() const -> bool
{
#if defined(ERHE_GRAPHICS_API_OPENGL)
    return m_impl->supports_worker_contexts();
#else
    return true;
#endif
}
auto Device::get_buffer_alignment(Buffer_target target) -> std::size_t
{
    return m_impl->get_buffer_alignment(target);
}
auto Device::wait_for_frame_start() -> uint64_t
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_frame_start_pending);
    const uint64_t frame_id = m_next_frame_start_id;
    ++m_next_frame_start_id;
    m_frame_start_pending = true;

    // CPU slot begin (frame pacing data requirements): the frame start is
    // the first per-frame device call on the app's frame path.
    m_frame_time_recorder->begin_frame(static_cast<std::int64_t>(frame_id), erhe::frame_pacing::Frame_time_recorder::now());
    m_impl->wait_for_frame_start(frame_id);
    return frame_id;
}
auto Device::acquire_frame_slot(const uint64_t frame_id) -> bool
{
    ERHE_VERIFY(m_frame_start_pending);
    // The started frame is the frame the slot is acquired for; the backend
    // verifies the order rule (frame_id follows the newest acquired frame,
    // at most get_number_of_frames_in_flight() frames open).
    ERHE_VERIFY(frame_id + 1 == m_next_frame_start_id);
    m_frame_start_pending = false;
    return m_impl->acquire_frame_slot(frame_id);
}
auto Device::wait_frame() -> bool
{
    const uint64_t frame_id = wait_for_frame_start();
    return acquire_frame_slot(frame_id);
}
auto Device::begin_frame() -> bool
{
    return m_impl->begin_frame();
}
auto Device::end_frame() -> bool
{
    return m_impl->end_frame();
}
auto Device::end_frame(const uint64_t frame_id) -> bool
{
    return m_impl->end_frame(frame_id);
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
void Device::clear_render_pipeline_cache()
{
    m_impl->clear_render_pipeline_cache();
}
void Device::warmup_render_pipeline(const Render_pipeline_create_info& create_info)
{
    // Construct + immediately destruct. The driver-level pipeline cache
    // retains the compiled binary across the destruction; the discarded
    // Render_pipeline only loses its (cheap) handle wrapper.
    Render_pipeline pipeline{*this, create_info};
}
auto Device::recreate_surface_for_new_window() -> bool
{
    return m_impl->recreate_surface_for_new_window();
}
auto Device::is_in_swapchain_frame() const -> bool
{
    return m_impl->is_in_swapchain_frame();
}
auto Device::get_frame_index() const -> uint64_t
{
    return m_impl->get_frame_index();
}
auto Device::get_number_of_frames_in_flight() const -> std::size_t
{
    return m_impl->get_number_of_frames_in_flight();
}
auto Device::is_frame_completed(const uint64_t frame) const -> bool
{
    return m_impl->is_frame_completed(frame);
}
auto Device::allocate_ring_buffer_entry(Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range
{
    return m_impl->allocate_ring_buffer_entry(buffer_target, usage, byte_count);
}
auto Device::allocate_upload_staging(const Buffer_target buffer_target, const std::size_t byte_count, const std::size_t capacity_byte_count) -> Ring_buffer_range
{
    return m_impl->allocate_upload_staging(buffer_target, byte_count, capacity_byte_count);
}
auto Device::get_upload_staging_usage() const -> Upload_staging_usage
{
    return m_impl->get_upload_staging_usage();
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
auto Device::get_memory_budget() const -> Memory_budget
{
    return m_impl->get_memory_budget();
}
auto Device::get_debug_state() const -> Device_debug_state
{
    return m_impl->get_debug_state();
}
auto Device::get_reverse_depth() const -> bool
{
    const bool api_supports_reverse_depth =
        (get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
    return !get_graphics_config().force_disable_reverse_depth && api_supports_reverse_depth;
}
auto Device::get_impl() -> Device_impl&
{
    return *m_impl.get();
}
auto Device::get_impl() const -> const Device_impl&
{
    return *m_impl.get();
}
#if defined(ERHE_SPIRV)
auto Device::get_spirv_cache() -> Spirv_cache&
{
    return *m_spirv_cache;
}
#endif
auto Device::get_shader_source_cache() -> Shader_source_cache&
{
    return *m_shader_source_cache;
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
auto Device::get_frame_time_recorder() -> erhe::frame_pacing::Frame_time_recorder&
{
    return *m_frame_time_recorder;
}

void Device::set_display_refresh_duration_seconds(const double seconds)
{
    m_frame_time_recorder->set_display_refresh_duration(seconds);
}

auto Device::get_display_refresh_duration_seconds() const -> double
{
    return m_frame_time_recorder->get_display_refresh_duration();
}

auto Device::make_display_refresh_resolver(const Graphics_config& graphics_config) -> std::unique_ptr<Display_refresh_resolver>
{
    // Called from the member-init list after m_frame_time_recorder and
    // before m_impl; the callbacks run later, on the resolver's callers'
    // threads, and touch only the recorder (which locks).
    std::unique_ptr<Display_refresh_resolver> resolver = std::make_unique<Display_refresh_resolver>(log_display_refresh);
    resolver->set_apply_callback(
        [this](const double period_seconds) {
            set_display_refresh_duration_seconds(period_seconds);
        }
    );
    Display_refresh_policy policy = Display_refresh_policy::automatic;
    if (!parse_display_refresh_policy(graphics_config.display_refresh_source, policy)) {
        log_display_refresh->warn(
            "display refresh: unknown display_refresh_source '{}' in the graphics config (auto, window, vulkan, calibration, override); using auto",
            graphics_config.display_refresh_source
        );
        policy = Display_refresh_policy::automatic;
    }
    const double override_hz = static_cast<double>(graphics_config.display_refresh_override_hz);
    resolver->set_policy(policy, (override_hz > 0.0) ? (1.0 / override_hz) : 0.0);
    return resolver;
}

auto Device::get_display_refresh_resolver() -> Display_refresh_resolver&
{
    return *m_display_refresh_resolver;
}

auto Device::get_display_refresh_report() const -> Display_refresh_report
{
    return m_display_refresh_resolver->get_report();
}

auto Device::is_display_refresh_calibrating() const -> bool
{
    return m_display_refresh_resolver->is_calibrating();
}

auto Device::wait_for_displayed_frame(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    if (m_display_refresh_resolver->is_calibrating()) {
        // The calibration waits on the presenting thread; see
        // is_display_refresh_calibrating().
        return Present_wait_result::unsupported;
    }
    return m_impl->wait_for_displayed_frame(frame_id, timeout_ns);
}

void Device::set_present_target_time(const std::int64_t frame_id, const double target_time_seconds, const double hold_until_seconds)
{
    m_impl->set_present_target_time(frame_id, target_time_seconds, hold_until_seconds);
}

auto Device::get_frame_pacing_tier() const -> Frame_pacing_tier
{
    return m_impl->get_frame_pacing_tier();
}

auto Device::is_present_target_time_honored() const -> bool
{
    return m_impl->is_present_target_time_honored();
}

void Device::set_present_prewarm(const std::int64_t frame_id, const double seconds)
{
    m_present_prewarm_frame_id = frame_id;
    m_present_prewarm_seconds  = seconds;
}

auto Device::get_present_prewarm(const std::int64_t frame_id) const -> double
{
    return (frame_id == m_present_prewarm_frame_id) ? m_present_prewarm_seconds : 0.0;
}

auto Device::get_present_calibration() const -> Present_calibration
{
    return m_present_calibration_store->get_calibration();
}

auto Device::get_present_calibration_state() const -> Present_calibration_state
{
    return m_present_calibration_store->get_state();
}

void Device::get_present_calibration_summary(Present_calibration_summary& out) const
{
    m_present_calibration_store->get_summary(out);
}

auto Device::get_present_request_offset() const -> std::optional<double>
{
    return m_present_calibration_store->get_request_offset();
}

auto Device::begin_present_calibration(
    erhe::window::Context_window* const window,
    const std::string_view              gpu_name,
    const Present_path                  present_path,
    Present_calibration&                in_out
) -> bool
{
    m_present_calibration_store->set_key_source(window, gpu_name, present_path);
    return m_present_calibration_store->begin(m_present_calibration_store->query_key(), in_out);
}

void Device::finish_present_calibration(const Present_calibration& result)
{
    m_present_calibration_store->finish_measured(result);
}

auto Device::refresh_present_calibration_for_display() -> bool
{
    if (!m_present_calibration_store->is_keyed()) {
        return false; // no calibration on this path
    }
    return m_present_calibration_store->refresh_for_display(m_present_calibration_store->query_key());
}

auto Device::remove_present_calibration_cache_entry(const std::string_view reason) -> std::string
{
    return m_present_calibration_store->remove_entry(reason);
}

auto c_str(const Present_calibration_phase value) -> const char*
{
    switch (value) {
        case Present_calibration_phase::grid:           return "grid";
        case Present_calibration_phase::request_window: return "request_window";
        case Present_calibration_phase::deadline:       return "deadline";
        default:                                        return "?";
    }
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


auto c_str(const Record_indexing record_indexing) -> const char*
{
    switch (record_indexing) {
        case Record_indexing::draw_position:  return "draw_position";
        case Record_indexing::first_instance: return "first_instance";
        default:                              return "?";
    }
}

} // namespace erhe::graphics
