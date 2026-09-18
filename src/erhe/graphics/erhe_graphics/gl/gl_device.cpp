// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_command_buffer.hpp"
#include "erhe_graphics/gl/gl_gpu_timer.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/command_buffer.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_debug.hpp"
#include "erhe_graphics/gl/gl_render_command_encoder.hpp"
#include "erhe_graphics/gl/gl_scoped_debug_group.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_container_access.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/renderdoc_app.h"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"

#if !defined(WIN32)
#   include <csignal>
#endif

#include <sstream>
#include <vector>

typedef unsigned char GLubyte;

namespace erhe::graphics {

namespace {

auto to_int(const std::string& text) -> int
{
    return stoi(text);
}

} // namespace

auto split(const std::string& text, const char separator) -> std::vector<std::string>
{
    std::vector<std::string> result;
    const std::size_t length = text.size();
    std::size_t span_start = std::string::npos;
    for (std::size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c == separator) {
            if (span_start != std::string::npos) {
                const std::size_t span_length = i - span_start;
                if (span_length > 0) {
                    result.emplace_back(text.substr(span_start, span_length));
                }
                span_start = std::string::npos;
            }
        } else if (span_start == std::string::npos) {
            span_start = i;
        }
    }
    if (span_start != std::string::npos) {
        if (length > span_start) {
            const std::size_t span_length = length - span_start;
            result.emplace_back(text.substr(span_start, span_length));
        }
    }
    return result;
}

auto digits_only(const std::string& s) -> std::string
{
    const std::size_t size = s.size();
    for (std::size_t i = 0; i < size; ++i) {
        if (::isdigit(s[i]) == 0) {
            return (i == 0) ? "" : s.substr(0, i);
        }
    }
    return s;
}

auto contains(const std::vector<std::string>& collection, const std::string& key) -> bool
{
    const auto i = std::find(collection.cbegin(), collection.cend(), key);
    return i != collection.cend();
}

auto get_string(const gl::String_name string_name) -> std::string
{
    const GLubyte* gl_str = gl::get_string(string_name);
    const char*    c_str  = reinterpret_cast<const char*>(gl_str);
    return (c_str != nullptr) ? std::string{c_str} : std::string{};
}

//

Device_impl::Device_impl(Device& device, const Surface_create_info& surface_create_info, const Graphics_config& graphics_config)
    : m_device             {device}
    , m_graphics_config    {graphics_config}
    , m_shader_monitor     {device}
{
    ERHE_PROFILE_FUNCTION();

    // The constructing thread owns the drawing context, context index 0.
    set_gl_context_index(0);
    m_context_slot_live[0].store(true, std::memory_order_release);

    // Wire each context slot's {tracker, binding state} pair. Within one
    // context the binding state is the single source of truth for the bound
    // VAO: the per-draw tracker and the VAO-setup push/pop guards in
    // Vertex_input_state_impl must share state, or the per-draw cache can
    // skip a needed bind after a guard restores 0.
    for (int slot = 0; slot < gl_context_slot_count; ++slot) {
        m_gl_state_trackers[slot].set_binding_state(&m_gl_binding_states[slot]);
    }

    gl_helpers::set_error_callback(
        [&device](const std::string& message) {
            device.device_message(Message_severity::error, message);
        }
    );

    if (surface_create_info.context_window != nullptr) {
        m_surface = std::make_unique<Surface>(
            std::make_unique<Surface_impl>(*this, surface_create_info)
        );
    }

    std::vector<std::string> extensions;
    const auto gl_vendor      = (get_string)(gl::String_name::vendor);
    const auto gl_renderer    = (get_string)(gl::String_name::renderer);
    const auto gl_version_str = (get_string)(gl::String_name::version);

    log_startup->info("GL Vendor:     {}", gl_vendor);
    log_startup->info("GL Renderer:   {}", gl_renderer);
    log_startup->info("GL Version:    {}", gl_version_str.c_str());

    auto gl_vendor_lc = gl_vendor;
    std::transform(
        gl_vendor_lc.begin(),
        gl_vendor_lc.end(),
        gl_vendor_lc.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        }
    );
    if (gl_vendor_lc.find("nvidia") != std::string::npos) {
        m_info.vendor = Vendor::Nvidia;
    } else if (gl_vendor_lc.find("amd") != std::string::npos) {
        m_info.vendor = Vendor::Amd;
    } else if (gl_vendor_lc.find("intel") != std::string::npos) {
        m_info.vendor = Vendor::Intel;
    } else {
        m_info.vendor = Vendor::Unknown;
    }

    auto versions = split(gl_version_str, '.');

    int major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
    int minor = versions.size() > 1 ? to_int(digits_only(versions[1])) : 0;

    m_info.gl_version = (major * 100) + (minor * 10);

    gl::get_integer_v(gl::Get_p_name::max_vertex_attribs, &m_info.max_vertex_attribs);
    log_startup->info("max vertex attribs: {}", m_info.max_vertex_attribs);

    log_startup->debug("GL Extensions:");
    {
        ERHE_PROFILE_SCOPE("Extensions");

        int num_extensions{0};

        gl::get_integer_v(gl::Get_p_name::num_extensions, &num_extensions);
        if (num_extensions > 0) {
            extensions.reserve(num_extensions);
            for (unsigned int i = 0; i < static_cast<unsigned int>(num_extensions); ++i) {
                const auto* extension_str = gl::get_string_i(gl::String_name::extensions, i);
                auto e = std::string(reinterpret_cast<const char*>(extension_str));
                log_startup->debug("    {}", e);
                extensions.push_back(e);
            }
        }
    }

    {
        ERHE_PROFILE_SCOPE("GLSL");
        auto shading_language_version = (get_string)(gl::String_name::shading_language_version);
        log_startup->info("GLSL Version:  {}", shading_language_version);
        versions = split(shading_language_version, '.');

        major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
        minor = (versions.size() > 1) ? to_int(digits_only(versions[1])) : 0;
        m_info.glsl_version = (major * 100) + minor;
    }

    log_startup->info("glVersion:   {}", m_info.gl_version);
    log_startup->info("glslVersion: {}", m_info.glsl_version);

    // OpenGL 4.5 minimum: DSA, clip control, compute shaders, SSBOs, debug
    // output and internalformat queries are all relied on unconditionally;
    // the pre-4.5 emulation layer has been removed.
    if (m_info.gl_version < 450) {
        ERHE_FATAL(
            "OpenGL 4.5 or newer is required; the driver reported version %d.%d (\"%s\")",
            m_info.gl_version / 100,
            (m_info.gl_version / 10) % 10,
            gl_version_str.c_str()
        );
    }

    const bool force_bindless_textures_off       = graphics_config.opengl.force_bindless_textures_off;
    const bool force_no_persistent_buffers       = graphics_config.opengl.force_no_persistent_buffers;
    const bool force_emulate_multi_draw_indirect = graphics_config.opengl.force_emulate_multi_draw_indirect;
    const bool initial_clear                     = graphics_config.initial_clear;

    m_context_window = surface_create_info.context_window;

    {
        ERHE_PROFILE_SCOPE("Query GL");

        {
            ERHE_PROFILE_SCOPE("gl::command_info_init");
            gl::command_info_init(m_info.glsl_version, extensions);
        }

        if (gl::is_extension_supported(gl::Extension::Extension_GL_EXT_texture_filter_anisotropic) || (m_info.gl_version >= 460)) {
            gl::get_float_v(gl::Get_p_name::max_texture_max_anisotropy, &m_info.max_texture_max_anisotropy);
        }
        gl::get_integer_v(gl::Get_p_name::max_samples,                &m_info.max_samples);
        gl::get_integer_v(gl::Get_p_name::max_color_texture_samples,  &m_info.max_color_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_depth_texture_samples,  &m_info.max_depth_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_framebuffer_samples, &m_info.max_framebuffer_samples);
        gl::get_integer_v(gl::Get_p_name::max_integer_samples,        &m_info.max_integer_samples);

        log_startup->info(
            "max samples = {}, max color texture samples = {}, max depth texture samples = {}, "
            "max framebuffer samples = {}, max integer samples = {}",
            m_info.max_samples,
            m_info.max_color_texture_samples,
            m_info.max_depth_texture_samples,
            m_info.max_framebuffer_samples,
            m_info.max_integer_samples
        );

        gl::get_integer_v(gl::Get_p_name::max_texture_size,          &m_info.max_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_3d_texture_size,       &m_info.max_3d_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_cube_map_texture_size, &m_info.max_cube_map_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_array_texture_layers,  &m_info.max_array_texture_layers);

        log_startup->info("max texture size:          {}", m_info.max_texture_size);
        log_startup->info("max 3d texture size:       {}", m_info.max_3d_texture_size);
        log_startup->info("max cube map texture size: {}", m_info.max_cube_map_texture_size);
        log_startup->info("max array texture layers:  {}", m_info.max_array_texture_layers);

        int max_texture_image_units{0};
        gl::get_integer_v(gl::Get_p_name::max_texture_image_units,          &max_texture_image_units);
        m_info.max_per_stage_descriptor_samplers = static_cast<uint32_t>(max_texture_image_units);
        gl::get_integer_v(gl::Get_p_name::max_combined_texture_image_units, &m_info.max_combined_texture_image_units);

        // GL 3.0 introduced context flags
        int context_flags = 0;
        gl::get_integer_v(gl::Get_p_name::context_flags, &context_flags);
        if ((static_cast<unsigned int>(context_flags) & static_cast<unsigned int>(GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT)) != 0) {
            m_info.forward_compatible = true;
            log_startup->info("forward compatible");
        }

        // GL 3.3 introduced context profile mask
        int context_profile_mask = 0;
        gl::get_integer_v(gl::Get_p_name::context_profile_mask, &context_profile_mask);
        if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_CORE_PROFILE_BIT)) != 0) {
            m_info.core_profile = true;
            log_startup->info("core profile");
        }
        if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)) != 0) {
            m_info.compatibility_profile = true;
            log_startup->info("compatibility profile");
        }

        gl::get_integer_v(gl::Get_p_name::max_texture_buffer_size, &m_info.max_texture_buffer_size);
    }

    {
        const int resolved_major = m_info.gl_version / 100;
        const int resolved_minor = (m_info.gl_version / 10) % 10;
        const char* profile_str =
            m_info.core_profile          ? " Core" :
            m_info.compatibility_profile ? " Compatibility" :
                                           "";
        m_info.api_info = fmt::format("OpenGL {}.{}{}", resolved_major, resolved_minor, profile_str);
    }

    // GL 4.3 core has debug_message_callback and push/pop_debug_group.
    // ARB_debug_output has glDebugMessageCallbackARB but not push/pop_debug_group,
    // and the ARB-suffixed functions are not in the generated GL wrapper.
    // For now, only use debug output when GL >= 4.3 (core functions available).
    m_info.use_debug_output = (m_info.gl_version >= 430);
    m_info.use_debug_groups = (m_info.gl_version >= 430);
    Scoped_debug_group_impl::s_enabled = m_info.use_debug_groups;
    log_startup->info("Debug output supported: {} (groups: {})", m_info.use_debug_output, m_info.use_debug_groups);

    install_gl_debug_callback();

    if (m_info.use_debug_groups) {
        GLint max_debug_message_length = 0;
        gl::get_integer_v(gl::Get_p_name::max_debug_message_length, &max_debug_message_length);
        Scoped_debug_group_impl::s_max_message_length = max_debug_message_length;
        // NVIDIA driver bug workaround: see gl_scoped_debug_group.cpp and
        // https://developer.nvidia.com/bugs/6216668
        Scoped_debug_group_impl::s_clamp_to_max_length = (m_info.vendor == Vendor::Nvidia);
        log_startup->info(
            "GL_MAX_DEBUG_MESSAGE_LENGTH: {} (clamp: {})",
            max_debug_message_length,
            Scoped_debug_group_impl::s_clamp_to_max_length
        );
    }

    gl::get_integer_v(gl::Get_p_name::max_uniform_block_size,             &m_info.max_uniform_block_size);
    gl::get_integer_v(gl::Get_p_name::max_uniform_buffer_bindings,        &m_info.max_uniform_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_blocks,          &m_info.max_vertex_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_vectors,         &m_info.max_vertex_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_blocks,        &m_info.max_fragment_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_vectors,       &m_info.max_fragment_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_geometry_uniform_blocks,        &m_info.max_geometry_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_control_uniform_blocks,    &m_info.max_tess_control_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_uniform_blocks, &m_info.max_tess_evaluation_uniform_blocks);

    {
        bool bindless_supported = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_bindless_texture);
        if (m_info.vendor == Vendor::Intel) {
            bindless_supported = false;
        }
        log_startup->info("GL_ARB_bindless_texture supported : {}", bindless_supported);
        bool use_bindless = bindless_supported;
        if (use_bindless) {
#if defined(ERHE_SPIRV)
            // 'GL_ARB_bindless_texture' : not allowed when using generating SPIR-V codes
            use_bindless = false;
            log_startup->warn("Force disabled GL_ARB_bindless_texture due to ERHE_SPIRV cmake setting");
#else
            if (force_bindless_textures_off) {
                use_bindless = false;
                log_startup->warn("Force disabled GL_ARB_bindless_texture due to config setting force_bindless_textures_off");
            }
            else
            if (graphics_config.renderdoc_capture_support) {
                use_bindless = false;
                log_startup->warn("Force disabled GL_ARB_bindless_texture due to config enabling RenderDoc capture");
            }
#endif
        }
        m_info.texture_heap_path = use_bindless
            ? Texture_heap_path::opengl_bindless_textures
            : Texture_heap_path::opengl_sampler_array;
    }
    m_info.use_clear_texture = (m_info.gl_version >= 440) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clear_texture);
    log_startup->info("GL_ARB_clear_texture supported : {}", m_info.use_clear_texture);

    m_info.use_texture_view = (m_info.gl_version >= 430) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_texture_view);
    log_startup->info("Texture View supported: {}", m_info.use_texture_view);

    // The SOLID_WIREFRAME standard-shader variant needs several flat varyings
    // at high explicit locations (v_bary / v_edge_mask / v_wire_color /
    // v_wire_width at locations 13..16). The macOS OpenGL 4.1 (GLSL 410) Apple
    // GLSL compiler cannot allocate them and the link fails ("Implementation
    // limit of 128 varying components exceeded ... v_edge_mask"). Enable only on
    // GL newer than 4.1 (every non-Apple desktop driver); the editor falls back
    // to the wide-line edge renderer where this is off.
    m_info.use_solid_wireframe = (m_info.gl_version > 410);
    log_startup->info("Solid wireframe variant supported: {}", m_info.use_solid_wireframe);

    // GL 4.5 is a hard requirement (checked above), so DSA is always present.
    m_info.use_direct_state_access = true;
    log_startup->info("Direct State Access supported: {}", m_info.use_direct_state_access);

    m_info.use_clip_control =
        (m_info.gl_version >= 450) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clip_control);
    log_startup->info("Clip Control supported: {}", m_info.use_clip_control);

    {
        int shader_storage_buffer_offset_alignment{0};
        gl::get_integer_v(gl::Get_p_name::shader_storage_buffer_offset_alignment, &shader_storage_buffer_offset_alignment);
        m_info.shader_storage_buffer_offset_alignment = static_cast<unsigned int>(shader_storage_buffer_offset_alignment);
    }
 
    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_sparse_texture)) {
        ERHE_PROFILE_SCOPE("Sparse texture");

        m_info.use_sparse_texture = true;
        gl::get_integer_v(gl::Get_p_name::max_sparse_texture_size_arb, &m_info.max_sparse_texture_size);
        log_startup->info("max sparse texture size : {}", m_info.max_sparse_texture_size);
    }
    log_startup->info("GL_ARB_sparse_texture supported : {}", m_info.use_sparse_texture);

    // glBufferStorage is GL 4.4 core; also accept the extension string for
    // completeness (a conformant 4.5 driver need not advertise it).
    m_info.use_persistent_buffers =
        (m_info.gl_version >= 440) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
    if (m_info.gl_version >= 430) {
        m_info.use_multi_draw_indirect_core = true;
        m_info.use_multi_draw_indirect_arb  = false;
        m_info.emulate_multi_draw_indirect  = false;
        log_startup->info("Multi Draw Indirect: OpenGL core 4.3+");
    } else if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_multi_draw_indirect)) {
        m_info.use_multi_draw_indirect_core = false;
        m_info.use_multi_draw_indirect_arb  = true;
        m_info.emulate_multi_draw_indirect  = false;
        log_startup->info("Multi Draw Indirect: GL_ARB_multi_draw_indirect");
    } else {
        m_info.emulate_multi_draw_indirect = true;
        log_startup->info("Multi Draw Indirect: emulation");
    }
    log_startup->info("Persistent Buffers supported: {}", m_info.use_persistent_buffers);
    if (force_emulate_multi_draw_indirect) {
        m_info.use_multi_draw_indirect_core = false;
        m_info.use_multi_draw_indirect_arb  = false;
        m_info.emulate_multi_draw_indirect  = true;
        log_startup->warn("Forced emulation for Draw Indirect due to config setting");
    }

    m_info.use_base_instance = (m_info.gl_version >= 420) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_base_instance);
    log_startup->info("Base Instance supported: {}", m_info.use_base_instance);

    {
        for (GLuint i = 0; i < 3; ++i) {
            gl::get_integer_iv(gl::Get_p_name::max_compute_work_group_count, i, &m_info.max_compute_workgroup_count[i]);
            gl::get_integer_iv(gl::Get_p_name::max_compute_work_group_size,  i, &m_info.max_compute_workgroup_size[i]);
        }
        gl::get_integer_v(gl::Get_p_name::max_compute_work_group_invocations, &m_info.max_compute_work_group_invocations);
        gl::get_integer_v(gl::Get_p_name::max_compute_shared_memory_size,     &m_info.max_compute_shared_memory_size);
        log_startup->info(
            "Max compute workgroup count = {} x {} x {}",
            m_info.max_compute_workgroup_count[0],
            m_info.max_compute_workgroup_count[1],
            m_info.max_compute_workgroup_count[2]
        );
        log_startup->info(
            "Max compute workgroup size = {} x {} x {}",
            m_info.max_compute_workgroup_size[0],
            m_info.max_compute_workgroup_size[1],
            m_info.max_compute_workgroup_size[2]
        );
        log_startup->info(
            "Max compute workgroup invocations = {}",
            m_info.max_compute_work_group_invocations
        );
        log_startup->info(
            "Max compute shared memory size = {}",
            m_info.max_compute_shared_memory_size
        );

        gl::get_integer_v(gl::Get_p_name::max_shader_storage_buffer_bindings,        &m_info.max_shader_storage_buffer_bindings);
        gl::get_integer_v(gl::Get_p_name::max_compute_shader_storage_blocks,         &m_info.max_compute_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_compute_uniform_blocks,                &m_info.max_compute_uniform_blocks);
        gl::get_integer_v(gl::Get_p_name::max_vertex_shader_storage_blocks,          &m_info.max_vertex_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_fragment_shader_storage_blocks,        &m_info.max_fragment_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_geometry_shader_storage_blocks,        &m_info.max_geometry_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_control_shader_storage_blocks,    &m_info.max_tess_control_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_shader_storage_blocks, &m_info.max_tess_evaluation_shader_storage_blocks);
    }

    int uniform_buffer_offset_alignment{0};
    gl::get_integer_v(gl::Get_p_name::uniform_buffer_offset_alignment, &uniform_buffer_offset_alignment);
    m_info.uniform_buffer_offset_alignment        = static_cast<unsigned int>(uniform_buffer_offset_alignment);
    log_startup->info(
        "uniform block ("
        "max size = {}, "
        "offset alignment = {}. "
        "max bindings = {}, "
        "max compute blocks = {}, "
        "max vertex blocks = {}, "
        "max fragment blocks = {}"
        ")",
        m_info.max_uniform_block_size,
        m_info.uniform_buffer_offset_alignment,
        m_info.max_uniform_buffer_bindings,
        m_info.max_compute_uniform_blocks,
        m_info.max_vertex_uniform_blocks,
        m_info.max_fragment_uniform_blocks
    );
    log_startup->info(
        "shader storage block ("
        "offset alignment = {}"
        ", max bindings = {}"
        ", max compute blocks = {}"
        ", max vertex blocks = {}"
        ", max fragment blocks = {}"
        ")",
        m_info.shader_storage_buffer_offset_alignment,
        m_info.max_shader_storage_buffer_bindings,
        m_info.max_compute_shader_storage_blocks,
        m_info.max_vertex_shader_storage_blocks,
        m_info.max_fragment_shader_storage_blocks
    );

    if (force_no_persistent_buffers) {
        if (m_info.use_persistent_buffers) {
            m_info.use_persistent_buffers = false;
            log_startup->warn("Force disabled persistently mapped buffers due to config setting");
        }
    }

    // The vertex-input tracker binds a persistent empty VAO for pipelines that
    // declare no vertex input (core-profile GL rejects glDraw* with VAO 0).
    // The VAO is created eagerly per context by create_per_context_resources()
    // (called from Device::Device's body, once m_impl is wired). The tracker
    // also needs the device for the Scoped_vertex_input_state adoption at
    // pipeline bind.
    for (int slot = 0; slot < gl_context_slot_count; ++slot) {
        m_gl_state_trackers[slot].set_device(&m_device);
    }

    if (surface_create_info.context_window != nullptr) {
        if (initial_clear) {
            gl::clear_color(0.2f, 0.2f, 0.2f, 0.2f);
            for (int i = 0; i < 3; ++i) {
                gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
                surface_create_info.context_window->swap_buffers();
            }
        }
    }

    // TODO more formats
    gl::Internal_format formats[] = {
        gl::Internal_format::r8,
        gl::Internal_format::rg8,
        gl::Internal_format::rgba8,
        gl::Internal_format::srgb8_alpha8,
        gl::Internal_format::r11f_g11f_b10f,
        gl::Internal_format::r16_snorm,
        gl::Internal_format::r16f,
        gl::Internal_format::rg16f,
        gl::Internal_format::rgba16f,
        gl::Internal_format::r32f,
        gl::Internal_format::rg32f,
        gl::Internal_format::rgba32f,
        gl::Internal_format::depth32f_stencil8,
        gl::Internal_format::depth24_stencil8,
        gl::Internal_format::depth_stencil,
        gl::Internal_format::stencil_index8,
        gl::Internal_format::depth_component16,
        gl::Internal_format::depth_component24,
        gl::Internal_format::depth_component32,
        gl::Internal_format::depth_component32f,
        gl::Internal_format::depth_component,
        gl::Internal_format::depth_component16
    };

    log_startup->info("Format properties:");

    {
        // Use glGetInternalformativ (GL_ARB_internalformat_query2), GL 4.3 core.
        for (const gl::Internal_format format : formats) {
            Format_properties properties{};

            std::stringstream ss;
            GLint supported{};
            gl::get_internalformat_iv(gl::Texture_target::texture_2d, format, gl::Internal_format_p_name::internalformat_supported, 1, &supported);
            properties.supported = (supported == GL_TRUE);
            if (!properties.supported) {
                continue;
            }

            ss << "    " << gl::c_str(format) << ": ";

            auto get_int = [format](gl::Internal_format_p_name p_name, gl::Texture_target target = gl::Texture_target::texture_2d) -> int
            {
                GLint value{0};
                gl::get_internalformat_iv(target, format, p_name, 1, &value);
                return value;
            };
            auto get_bool = [format](gl::Internal_format_p_name p_name, gl::Texture_target target = gl::Texture_target::texture_2d) -> bool
            {
                GLint value{0};
                gl::get_internalformat_iv(target, format, p_name, 1, &value);
                return (value == GL_TRUE);
            };

            properties.red_size           = get_int(gl::Internal_format_p_name::internalformat_red_size);
            properties.green_size         = get_int(gl::Internal_format_p_name::internalformat_green_size);
            properties.blue_size          = get_int(gl::Internal_format_p_name::internalformat_blue_size);
            properties.alpha_size         = get_int(gl::Internal_format_p_name::internalformat_alpha_size);
            properties.depth_size         = get_int(gl::Internal_format_p_name::internalformat_depth_size);
            properties.stencil_size       = get_int(gl::Internal_format_p_name::internalformat_stencil_size);
            properties.image_texel_size   = get_int(gl::Internal_format_p_name::image_texel_size);
            properties.color_renderable   = get_bool(gl::Internal_format_p_name::color_renderable);
            properties.depth_renderable   = get_bool(gl::Internal_format_p_name::depth_renderable);
            properties.stencil_renderable = get_bool(gl::Internal_format_p_name::stencil_renderable);
            properties.filter             = get_bool(gl::Internal_format_p_name::filter);
            properties.framebuffer_blend  = get_bool(gl::Internal_format_p_name::framebuffer_blend);

            int num_virtual_page_sizes = 0;
            if (m_info.gl_version >= 460) {
                num_virtual_page_sizes = get_int(gl::Internal_format_p_name::num_virtual_page_sizes_arb);
            }
            if (num_virtual_page_sizes > 0) {
                properties.sparse_tile_x_sizes.resize(num_virtual_page_sizes);
                properties.sparse_tile_y_sizes.resize(num_virtual_page_sizes);
                properties.sparse_tile_z_sizes.resize(num_virtual_page_sizes);
                gl::get_internalformat_i_64v(
                    gl::Texture_target::texture_2d,
                    format,
                    gl::Internal_format_p_name::virtual_page_size_x_arb,
                    static_cast<GLsizei>(num_virtual_page_sizes),
                    properties.sparse_tile_x_sizes.data()
                );
                gl::get_internalformat_i_64v(
                    gl::Texture_target::texture_2d,
                    format,
                    gl::Internal_format_p_name::virtual_page_size_y_arb,
                    static_cast<GLsizei>(num_virtual_page_sizes),
                    properties.sparse_tile_y_sizes.data()
                );
                gl::get_internalformat_i_64v(
                    gl::Texture_target::texture_2d,
                    format,
                    gl::Internal_format_p_name::virtual_page_size_z_arb,
                    static_cast<GLsizei>(num_virtual_page_sizes),
                    properties.sparse_tile_z_sizes.data()
                );
                if (num_virtual_page_sizes > 0) {
                    ss << "page sizes:";
                    for (GLint i = 0; i < num_virtual_page_sizes; ++i) {
                        ss << fmt::format(
                            " {} x {} x {}",
                            properties.sparse_tile_x_sizes[i],
                            properties.sparse_tile_y_sizes[i],
                            properties.sparse_tile_z_sizes[i]
                        );
                    }
                }
            }

            {
                int num_sample_counts = get_int(gl::Internal_format_p_name::num_sample_counts, gl::Texture_target::texture_2d_multisample);
                if (num_sample_counts > 0) {
                    if (num_virtual_page_sizes > 0) {
                        ss << ", ";
                    }
                    ss << fmt::format("sample counts:", c_str(format));
                    properties.texture_2d_sample_counts.resize(num_sample_counts);
                    gl::get_internalformat_iv(
                        gl::Texture_target::texture_2d_multisample,
                        format,
                        gl::Internal_format_p_name::samples,
                        num_sample_counts,
                        properties.texture_2d_sample_counts.data()
                    );
                    std::sort(properties.texture_2d_sample_counts.begin(), properties.texture_2d_sample_counts.end());
                    for (int count : properties.texture_2d_sample_counts) {
                        ss << fmt::format(" {}", count);
                    }
                }
            }
            log_startup->info(ss.str());

            properties.texture_2d_array_max_width  = get_int(gl::Internal_format_p_name::max_width, gl::Texture_target::texture_2d_array);
            properties.texture_2d_array_max_height = get_int(gl::Internal_format_p_name::max_height, gl::Texture_target::texture_2d_array);
            properties.texture_2d_array_max_layers = get_int(gl::Internal_format_p_name::max_layers, gl::Texture_target::texture_2d_array);
            format_properties.insert({format, properties});
        }
    }

    {
        ERHE_PROFILE_SCOPE("Start shader monitor");
        m_shader_monitor.begin(graphics_config.shader_monitor_enabled);
    }

    if (m_info.gl_version >= 430) {
        gl::enable(gl::Enable_cap::primitive_restart_fixed_index);
    } else {
        gl::enable(gl::Enable_cap::primitive_restart);
        gl::primitive_restart_index(0xFFFFFFFFu);
    }
    gl::enable(gl::Enable_cap::scissor_test);
    if (m_info.use_clip_control) {
        gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    }

    // Populate coordinate conventions for OpenGL
    m_info.coordinate_conventions.framebuffer_origin = erhe::math::Framebuffer_origin::bottom_left;
    m_info.coordinate_conventions.clip_space_y_flip  = erhe::math::Clip_space_y_flip::disabled;
    m_info.coordinate_conventions.texture_origin     = erhe::math::Texture_origin::bottom_left;
    m_info.coordinate_conventions.native_depth_range = m_info.use_clip_control
        ? erhe::math::Depth_range::zero_to_one
        : erhe::math::Depth_range::negative_one_to_one;

    // Hardware-capability note: reverse-Z requires native_depth_range =
    // zero_to_one, which on OpenGL needs glClipControl (GL 4.5 core or
    // GL_ARB_clip_control). macOS exposes GL 4.1 with neither, so the depth
    // range is locked at negative_one_to_one and reverse-Z cannot be used.
    // Device::get_reverse_depth() already ANDs the user preference with this
    // capability, so no config mutation is needed here -- every downstream
    // reader derives the same effective value from that single query. We only
    // log when the user did not force-disable reverse-Z yet the hardware
    // cannot provide it, to explain why forward-Z is in effect.
    if ((m_info.coordinate_conventions.native_depth_range != erhe::math::Depth_range::zero_to_one) && !m_graphics_config.force_disable_reverse_depth) {
        log_startup->warn(
            "reverse-Z preferred but hardware does not support glClipControl "
            "(GL {}.{}, ARB_clip_control={}); using forward-Z",
            m_info.gl_version / 100, (m_info.gl_version / 10) % 10,
            gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clip_control)
        );
    }

    // OpenGL only supports the equivalent of "sample 0" for depth/stencil
    // multisample resolves: glBlitFramebuffer requires GL_NEAREST when the
    // mask includes depth or stencil, and there is no filter selection.
    m_info.supported_depth_resolve_modes          = Resolve_mode_flag_bit_mask::sample_zero;
    m_info.supported_stencil_resolve_modes        = Resolve_mode_flag_bit_mask::sample_zero;
    m_info.independent_depth_stencil_resolve      = true;
    m_info.independent_depth_stencil_resolve_none = true;

    if (
        (surface_create_info.context_window != nullptr) &&
        (surface_create_info.context_window->get_window_configuration().color_bit_depth <= 8)
    ) {
        gl::enable(gl::Enable_cap::framebuffer_srgb);
    }

    if (graphics_config.force_disable_vsync && (m_context_window != nullptr)) {
        m_context_window->set_swap_interval(0);
        log_startup->info("Disabled vsync (force_disable_vsync)");
    }

    std::fill(
        m_frame_syncs.begin(),
        m_frame_syncs.end(),
        Frame_sync{
            .frame_number = 0,
            .fence_sync   = nullptr,
            .result       = gl::Sync_status::timeout_expired
        }
    );

    m_last_ok_frame_timestamp = std::chrono::steady_clock::now();

    m_staging_buffer = std::make_unique<Ring_buffer_client>(
        device,
        erhe::graphics::Buffer_target::transfer_src,
        "Device::m_staging_buffer"
    );
}

Device_impl::~Device_impl() noexcept
{
    gl_helpers::set_error_callback({});
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::wait_for_displayed_frame(const std::int64_t frame_id, const uint64_t timeout_ns) -> Present_wait_result
{
    static_cast<void>(frame_id);
    static_cast<void>(timeout_ns);
    return Present_wait_result::unsupported;
}

auto Device_impl::get_frame_pacing_tier() const -> Frame_pacing_tier
{
    // Vsynced SwapBuffers has backpressure, so tier S is possible here in
    // principle; not wired yet - focus is the Vulkan backend (P4.2).
    return Frame_pacing_tier::off;
}

void Device_impl::set_present_target_time(const std::int64_t frame_id, const double target_time_seconds, const double hold_until_seconds)
{
    static_cast<void>(frame_id);
    static_cast<void>(target_time_seconds);
    static_cast<void>(hold_until_seconds);
}

auto Device_impl::get_native_handles() const -> Native_device_handles
{
    Native_device_handles handles{};
    if (m_context_window != nullptr) {
#if defined(ERHE_OS_WINDOWS)
        const HWND hwnd = m_context_window->get_hwnd();
        handles.gl_hdc   = reinterpret_cast<void*>(GetDC(hwnd));
        handles.gl_hglrc = reinterpret_cast<void*>(m_context_window->get_hglrc());
#endif
#if defined(ERHE_OS_LINUX)
        handles.gl_wl_display = reinterpret_cast<void*>(m_context_window->get_wl_display());
#endif
    }
    return handles;
}

void Device_impl::resize_swapchain_to_window()
{
    // NOP for GL backend
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    if (m_info.texture_heap_path == Texture_heap_path::opengl_bindless_textures) {
        return gl::get_texture_sampler_handle_arb(texture.get_impl().gl_name(), sampler.get_impl().gl_name());
    } else {
        const uint64_t texture_name  = static_cast<uint64_t>(texture.get_impl().gl_name());
        const uint64_t sampler_name  = static_cast<uint64_t>(sampler.get_impl().gl_name());
        const uint64_t handle        = texture_name | (sampler_name << 32);
        return handle;
    }
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    std::vector<erhe::dataformat::Format> result;
    erhe::dataformat::Format formats[] = {
        erhe::dataformat::Format::format_d16_unorm,
        erhe::dataformat::Format::format_x8_d24_unorm_pack32,
        erhe::dataformat::Format::format_d32_sfloat,
        erhe::dataformat::Format::format_s8_uint,
        erhe::dataformat::Format::format_d24_unorm_s8_uint,
        erhe::dataformat::Format::format_d32_sfloat_s8_uint
    };
    for (const erhe::dataformat::Format format : formats) {
        Format_properties properties = get_format_properties(format);
        if (!properties.supported) {
            continue;
        }
        result.push_back(format);
    }
    return result;
}

void Device_impl::sort_depth_stencil_formats(std::vector<erhe::dataformat::Format>& formats, const unsigned int sort_flags, const int requested_sample_count) const
{
    using namespace erhe::utility;
    const bool require_depth     = test_bit_set(sort_flags, format_flag_require_depth    );
    const bool require_stencil   = test_bit_set(sort_flags, format_flag_require_stencil  );
    const bool prefer_accuracy   = test_bit_set(sort_flags, format_flag_prefer_accuracy  );
    const bool prefer_filterable = test_bit_set(sort_flags, format_flag_prefer_filterable);

    auto format_score = [&](const erhe::dataformat::Format format) {
        const Format_properties properties = get_format_properties(format);
        if (!properties.supported) {
            return -1.0f;
        }
        if (require_depth) {
            if ((properties.depth_size == 0) || !properties.depth_renderable) {
                return -1.0f;
            }
        }
        if (require_stencil) {
            if ((properties.stencil_size == 0) || !properties.stencil_renderable) {
                return -1.0f;
            }
        }
        if (requested_sample_count != 0) {
            auto i = std::find(properties.texture_2d_sample_counts.begin(), properties.texture_2d_sample_counts.end(), requested_sample_count);
            if (i == properties.texture_2d_sample_counts.end()) {
                return -1.0f;
            }
        }
        float score = 0.0f;
        if (prefer_filterable && properties.filter) {
            score += 1.0f;
        }
        if (prefer_accuracy) {
            score += properties.depth_size;
        } else {
            score += 1.0f / properties.image_texel_size;
        }
        return score;
    };
    formats.erase(
        std::remove_if(
            formats.begin(), formats.end(),
            [&](const erhe::dataformat::Format& format) -> bool {
                return format_score(format) < 0.0f;
            }
        ),
        formats.end()
    );
    std::stable_sort(
        formats.begin(),
        formats.end(),
        [&](const erhe::dataformat::Format& lhs, const erhe::dataformat::Format& rhs)
        {
            const float lhs_score = format_score(lhs);
            const float rhs_score = format_score(rhs);
            return lhs_score < rhs_score;
        }
    );
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    for (const erhe::dataformat::Format format : formats) {
        Format_properties properties = get_format_properties(format);
        if (!properties.supported) {
            continue;
        }
        return format;
    }
    return erhe::dataformat::Format::format_undefined;
}

auto Device_impl::choose_depth_stencil_format(const unsigned int sort_flags, const int requested_sample_count) const -> erhe::dataformat::Format
{
    std::vector<erhe::dataformat::Format> formats = get_supported_depth_stencil_formats();
    sort_depth_stencil_formats(formats, sort_flags, requested_sample_count);
    const erhe::dataformat::Format result = choose_depth_stencil_format(formats);
    if (result == erhe::dataformat::Format::format_undefined) {
        ERHE_FATAL(
            "No supported depth/stencil format matches sort_flags=0x%x with sample_count=%d",
            sort_flags,
            requested_sample_count
        );
    }
    return result;
}

auto Device_impl::create_dummy_texture(Command_buffer& init_command_buffer, const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    const Texture_create_info create_info{
        .device       = m_device,
        .usage_mask   = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type         = Texture_type::texture_2d,
        .pixelformat  = format,
        .use_mipmaps  = false,
        .sample_count = 0,
        .width        = 2,
        .height       = 2,
        .debug_label  = erhe::utility::Debug_label{ fmt::format("dummy {} texture", c_str(format)) }
    };

    auto texture = std::make_shared<Texture>(m_device, create_info);
    if (
        (format == erhe::dataformat::Format::format_8_vec4_unorm) ||
        (format == erhe::dataformat::Format::format_8_vec4_srgb)
    ) {
        const std::array<uint8_t, 16> dummy_pixel_u8{
            0xee, 0x11, 0xdd, 0xff,  0xcc, 0x11, 0xbb, 0xff,
            0xcc, 0x11, 0xbb, 0xff,  0xee, 0x11, 0xdd, 0xff,
        };
        const std::span<const std::uint8_t> image_data{&dummy_pixel_u8[0], dummy_pixel_u8.size()};

        std::span<const std::uint8_t> src_span{dummy_pixel_u8.data(), dummy_pixel_u8.size()};
        std::size_t                   byte_count   = src_span.size_bytes();
        Ring_buffer_client            texture_upload_buffer{m_device, erhe::graphics::Buffer_target::transfer_src, "dummy texture upload"};
        Ring_buffer_range             buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
        std::span<std::byte>          dst_span     = buffer_range.get_span();
        memcpy(dst_span.data(), src_span.data(), byte_count);
        buffer_range.bytes_written(byte_count);
        buffer_range.close();

        const int src_bytes_per_row   = 2 * 4;
        const int src_bytes_per_image = 2 * src_bytes_per_row;
        Blit_command_encoder encoder{m_device, init_command_buffer};
        encoder.copy_from_buffer(
            buffer_range.get_buffer()->get_buffer(),         // source_buffer
            buffer_range.get_byte_start_offset_in_buffer(),  // source_offset
            src_bytes_per_row,                               // source_bytes_per_row
            src_bytes_per_image,                             // source_bytes_per_image
            glm::ivec3{2, 2, 1},                             // source_size
            texture.get(),                                   // destination_texture
            0,                                               // destination_slice
            0,                                               // destination_level
            glm::ivec3{0, 0, 0}                              // destination_origin
        );

        buffer_range.release();
    } else {
        texture->clear();
    }

    return texture;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

auto Device_impl::get_graphics_config() const -> const Graphics_config&
{
    return m_graphics_config;
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }

        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }

        case Buffer_target::draw_indirect: {
            // TODO Consider Draw_primitives_indirect_command
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64; // TODO
        }
    }
}

/// Ring buffer

static constexpr gl::Buffer_storage_mask storage_mask_persistent{
    gl::Buffer_storage_mask::map_coherent_bit   |
    gl::Buffer_storage_mask::map_persistent_bit |
    gl::Buffer_storage_mask::map_write_bit
};
static constexpr gl::Buffer_storage_mask storage_mask_not_persistent{
    gl::Buffer_storage_mask::map_write_bit
};
inline auto storage_mask(Device& device) -> gl::Buffer_storage_mask
{
    return device.get_info().use_persistent_buffers
        ? storage_mask_persistent
        : storage_mask_not_persistent;
}

static constexpr gl::Map_buffer_access_mask access_mask_persistent{
    gl::Map_buffer_access_mask::map_coherent_bit   |
    gl::Map_buffer_access_mask::map_persistent_bit |
    gl::Map_buffer_access_mask::map_write_bit
};
static constexpr gl::Map_buffer_access_mask access_mask_not_persistent{
    gl::Map_buffer_access_mask::map_write_bit
};

inline auto access_mask(Device& device) -> gl::Map_buffer_access_mask
{
    return device.get_info().use_persistent_buffers
        ? access_mask_persistent
        : access_mask_not_persistent;
}

void Device_impl::upload_to_buffer(const Buffer& buffer, size_t offset, const void* data, size_t length)
{
    // Consumer half of the publication contract: this is the main thread's
    // first touch of a worker-created buffer (glCopyNamedBufferSubData into
    // its name below). Server-side wait, once per object, no-op for
    // main-thread-created buffers.
    buffer.get_impl().wait_publication();

    Ring_buffer_range    staging_buffer_range = m_staging_buffer->acquire(Ring_buffer_usage::CPU_write, length);
    std::span<std::byte> staging_buffer_span  = staging_buffer_range.get_span();
    memcpy(
        staging_buffer_span.data(),  // void *dst
        data,                        // const void *src
        length                       // size_t len
    );
    staging_buffer_range.bytes_written(length);
    staging_buffer_range.close();

    gl::copy_named_buffer_sub_data(
        staging_buffer_range.get_buffer()->get_buffer()->get_impl().gl_name(),  // GLuint   readBuffer
        buffer.get_impl().gl_name(),                                            // GLuint   writeBuffer
        staging_buffer_range.get_byte_start_offset_in_buffer(),                 // GLintptr readOffset
        offset,                                                                 // GLintptr writeOffset
        length                                                                  // GLsizeiptr size
    );

    staging_buffer_range.release();
}

void Device_impl::upload_to_texture(
    const Texture&               texture,
    const int                    level,
    const int                    x,
    const int                    y,
    const int                    width,
    const int                    height,
    const erhe::dataformat::Format pixelformat,
    const void*                  data,
    const int                    row_stride
)
{
    // Block-compressed uploads go through Blit_command_encoder::copy_from_buffer
    ERHE_VERIFY(!erhe::dataformat::is_block_compressed(pixelformat));

    gl::Pixel_format gl_format{};
    gl::Pixel_type   gl_type{};
    const bool format_ok = get_format_and_type(pixelformat, gl_format, gl_type);
    ERHE_VERIFY(format_ok);

    const std::size_t bytes_per_pixel = get_gl_pixel_byte_count(pixelformat);
    const int         row_length      = (row_stride != 0) ? static_cast<int>(row_stride / bytes_per_pixel) : 0;

    // Ensure no PBO is bound so GL reads from the CPU pointer
    gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_row_length, row_length);
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_image_height, 0);

    gl::texture_sub_image_2d(
        texture.get_impl().gl_name(),
        level, x, y, width, height,
        gl_format, gl_type, data
    );
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, std::move(callback));
    // The handler is released by frame_completed() for this frame, which
    // only runs when the frame got a fence sync in end_frame(). Ring buffer
    // acquires request one as a side effect; a frame whose only pending
    // work is a completion handler (e.g. Mesh_memory retired-range frees)
    // must request it explicitly or the handler waits until wait_idle().
    m_need_sync = true;
}

void Device_impl::on_thread_enter()
{
    set_gl_context_index(0);
}

void Device_impl::install_gl_debug_callback()
{
    if (!m_info.use_debug_output) {
        return;
    }
    ERHE_PROFILE_SCOPE("Debug Callback");
    gl::debug_message_callback(erhe_opengl_callback, &m_device);
    gl::debug_message_control(
        gl::Debug_source  ::dont_care,
        gl::Debug_type    ::dont_care,
        gl::Debug_severity::dont_care,
        0,
        nullptr,
        GL_TRUE
    );
    gl::enable(gl::Enable_cap::debug_output);
    gl::enable(gl::Enable_cap::debug_output_synchronous);
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    // A signaled fence for completed_frame proves every command submitted
    // before it is done too, so the watermark takes the maximum rather than
    // requiring each frame to report in turn - frames that never got a fence
    // would otherwise hold it back forever.
    if (completed_frame + 1 > m_latest_completed_frame) {
        m_latest_completed_frame = completed_frame + 1;
    }
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(completed_frame);
    }
    for (const Completion_handler& entry : m_completion_handlers) {
        if (entry.frame_number == completed_frame) {
            entry.callback(*this);
        }
    }
    auto i = std::remove_if(
        m_completion_handlers.begin(),
        m_completion_handlers.end(),
        [completed_frame](Completion_handler& entry) { return entry.frame_number == completed_frame; }
    );
    if (i != m_completion_handlers.end()) {
        m_completion_handlers.erase(i, m_completion_handlers.end());
    }
}

auto Device_impl::wait_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::idle);
    // The main context's drain point for per-context container-object
    // deletion and shared-object binding scrubs: it never becomes current
    // again, so "drain on next make-current" would never fire for it.
    drain_container_object_deletes_for_current_context();
    drain_shared_object_scrubs_for_current_context();
    // Drop the previous frame's Command_buffer wrappers. GL serializes
    // through the driver context and submit_command_buffers is a
    // (mostly) no-op, so by the time we get back here the cbs from the
    // last frame are no longer referenced by anything.
    m_command_buffers.clear();
    m_state = Device_frame_state::waited;
    return true;
}

auto Device_impl::begin_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::waited);
    m_had_swapchain_frame = false;
    m_state = Device_frame_state::recording;
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Legacy compat: swapchain part moved to Command_buffer::begin_swapchain.
    static_cast<void>(frame_begin_info);
    return begin_frame();
}

auto Device_impl::end_frame() -> bool
{
    // CONTRACT: end_frame advances the frame index. That is its ONLY
    // job. It does not submit, it does not present (presentation is
    // implicit in Device::submit_command_buffers when a cb engaged a
    // swapchain via begin_swapchain). On GL the only extra work here
    // is the frame-sync bookkeeping that drives ring-buffer
    // completion notifications -- that's not a submit, just GL-fence
    // polling that pairs with m_need_sync from earlier in the frame.
    ERHE_VERIFY(
        (m_state == Device_frame_state::in_swapchain_frame) ||
        (m_state == Device_frame_state::recording) ||
        (m_state == Device_frame_state::waited)
    );

    // Poll any GPU timer query results that became available during this
    // frame, and advance the timer ring-buffer index for the next one.
    Gpu_timer_impl::end_frame();

    m_had_swapchain_frame = false;

    // Check previous frame fences for completion
    m_completed_frames.clear();
    for (Frame_sync& frame_sync : m_frame_syncs) {
        if (frame_sync.fence_sync != nullptr) {
            if (frame_sync.result != gl::Sync_status::condition_satisfied) {
                frame_sync.result = gl::client_wait_sync(
                    (GLsync)(frame_sync.fence_sync),
                    gl::Sync_object_mask{0}, //gl::Sync_object_mask::sync_flush_commands_bit,
                    0
                );
            }

            if (
                (frame_sync.result == gl::Sync_status::already_signaled) ||
                (frame_sync.result == gl::Sync_status::condition_satisfied)
            ) {
                gl::delete_sync((GLsync)(frame_sync.fence_sync));

                // Keep record of completed frames
                m_completed_frames.push_back(frame_sync.frame_number);
                // Remove from pending frames
                const auto i = std::remove(m_pending_frames.begin(), m_pending_frames.end(), frame_sync.frame_number);
                ERHE_VERIFY(i != m_pending_frames.end());
                m_pending_frames.erase(i, m_pending_frames.end());

                frame_sync.fence_sync = nullptr;
            }
        }
    }

    // Process completed frames
    if (!m_completed_frames.empty()) {
        std::sort(m_pending_frames.begin(), m_pending_frames.end(), [](uint64_t lhs, uint64_t rhs) { return lhs < rhs; });
        std::sort(m_completed_frames.begin(), m_completed_frames.end(), [](uint64_t lhs, uint64_t rhs) { return lhs < rhs; });
        for (uint64_t frame : m_completed_frames) {
            frame_completed(frame);
        }
    }

    // Find available frame sync slot and make new pending frame sync record
    if (m_need_sync) {
        for (Frame_sync& frame_sync : m_frame_syncs) {
            if (frame_sync.fence_sync == nullptr) {
                frame_sync.frame_number = m_frame_index,
                frame_sync.fence_sync   = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0),
                frame_sync.result       = gl::Sync_status::timeout_expired;
                m_pending_frames.push_back(m_frame_index);
                m_need_sync = false;
                break;
            }
        }
        if (m_need_sync) {
            log_context->warn("Out of frame sync slots");
            const std::chrono::steady_clock::duration duration = std::chrono::steady_clock::now() - m_last_ok_frame_timestamp;
            if (duration > std::chrono::seconds{5}) {
                log_context->critical("No frame sync slots available for over 5 seconds.");
                abort();
            }
        } else {
            m_last_ok_frame_timestamp = std::chrono::steady_clock::now();
        }
    }

    ++m_frame_index;

    m_state = Device_frame_state::idle;
    return true;
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    // Legacy compat overload; Frame_end_info is no longer used.
    static_cast<void>(frame_end_info);
    return end_frame();
}


auto Device_impl::recreate_surface_for_new_window() -> bool
{
    // Not applicable to OpenGL: the window-system context owns the
    // drawable, no surface object to recreate. Vulkan-only path.
    return false;
}

void Device_impl::clear_render_pipeline_cache()
{
    // OpenGL has no precompiled pipeline objects -- shader binding is
    // resolved per draw, so there is no stale-pipeline hazard like
    // Vulkan's m_pipeline_map.
}

void Device_impl::wait_idle()
{
    // Block CPU until all submitted GL commands have completed on the GPU.
    gl::finish();

    // Treat every outstanding pending frame as completed: fire the ring
    // buffer frame_completed callbacks and drain matching completion
    // handlers. Any fence syncs left in m_frame_syncs are now guaranteed
    // to be signaled; delete them and clear the slot.
    std::vector<uint64_t> completed_frames;
    completed_frames.swap(m_pending_frames);
    std::sort(completed_frames.begin(), completed_frames.end());
    for (Frame_sync& frame_sync : m_frame_syncs) {
        if (frame_sync.fence_sync != nullptr) {
            gl::delete_sync(static_cast<GLsync>(frame_sync.fence_sync));
            frame_sync.fence_sync = nullptr;
        }
    }
    for (uint64_t frame : completed_frames) {
        frame_completed(frame);
    }
    // gl::finish() above proves every command submitted so far is done, frames
    // that never got a fence included, so everything before the frame still
    // being recorded is retired. Without this the watermark would only ever
    // advance for fenced frames, and a consumer that asks
    // Device::is_frame_completed() would be told "no" indefinitely in a
    // workload that never requests a sync.
    if (m_frame_index > m_latest_completed_frame) {
        m_latest_completed_frame = m_frame_index;
    }

    // Anything left in m_completion_handlers is for a frame that was
    // never paired with a fence sync; drain it too.
    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }
    m_completion_handlers.clear();
}

auto Device_impl::is_in_swapchain_frame() const -> bool
{
    return m_state == Device_frame_state::in_swapchain_frame;
}

auto Device_impl::get_frame_index() const -> uint64_t
{
    return m_frame_index;
}

// GL runs no fixed frame-in-flight ring: depth is bounded by the fences
// end_frame() plants. This is the sizing hint for consumers that hold one
// copy per unretired frame; over-reporting only costs memory, and a consumer
// that runs out of copies grows (see Multi_copy_buffer).
auto Device_impl::get_number_of_frames_in_flight() const -> std::size_t
{
    return s_number_of_frames_in_flight;
}

auto Device_impl::is_frame_completed(const uint64_t frame) const -> bool
{
    return frame < m_latest_completed_frame;
}

auto Device_impl::allocate_ring_buffer_entry(
    const Buffer_target     buffer_target,
    const Ring_buffer_usage ring_buffer_usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    m_need_sync = true;
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // No existing usable buffer found, create new buffer. First buffer of a
    // usage class gets 4x headroom; spill buffers are sized to the request
    // (see the Vulkan backend for rationale).
    bool has_existing = false;
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (ring_buffer->match(ring_buffer_usage)) {
            has_existing = true;
            break;
        }
    }
    const Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, has_existing ? byte_count : 4 * byte_count),
        .ring_buffer_usage = ring_buffer_usage,
        .debug_label       = "Ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, ring_buffer_usage, byte_count);
}

void Device_impl::memory_barrier(const Memory_barrier_mask barriers)
{
    gl::memory_barrier(static_cast<gl::Memory_barrier_mask>(barriers)); // TODO Proper conversion
}

auto Device_impl::get_format_properties(const erhe::dataformat::Format format) const -> Format_properties
{
    const std::optional<gl::Internal_format> gl_format_opt = gl_helpers::convert_to_gl(format);
    ERHE_VERIFY(gl_format_opt.has_value());
    const gl::Internal_format gl_format = gl_format_opt.value();
    auto i = format_properties.find(gl_format);
    if (i == format_properties.end()) {
        return {};
    }

    // format_x8_d24_unorm_pack32 maps to depth24_stencil8, which advertises a
    // stencil aspect the abstraction's format does not have. Keep the reported
    // stencil aspect (size and renderable flag) consistent with the abstraction.
    Format_properties result = i->second;
    if (erhe::dataformat::get_stencil_size_bits(format) == 0) {
        result.stencil_size       = 0;
        result.stencil_renderable = false;
    }
    return result;
}

auto Device_impl::probe_image_format_support(const erhe::dataformat::Format format, const uint64_t usage_mask) const -> bool
{
    // OpenGL has no direct equivalent of vkGetPhysicalDeviceImageFormatProperties2.
    // The probe concept is Vulkan-specific; on GL we conservatively report "supported"
    // and let downstream texture creation fail if the combination is not renderable.
    // The undefined format is never a usable image format, so reject it explicitly.
    static_cast<void>(usage_mask);
    return format != erhe::dataformat::Format::format_undefined;
}

void Device_impl::clear_texture(const Texture& texture, const std::array<double, 4> value)
{
    const erhe::dataformat::Format      pixelformat       = texture.get_pixelformat();
    const erhe::dataformat::Format_kind format_kind       = erhe::dataformat::get_format_kind      (pixelformat);
    const std::size_t                   depth_size_bits   = erhe::dataformat::get_depth_size_bits  (pixelformat);
    const std::size_t                   stencil_size_bits = erhe::dataformat::get_stencil_size_bits(pixelformat);
    if (m_info.use_clear_texture) {
        switch (format_kind) {
            case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                const unsigned int clear_value[4] = {
                    static_cast<unsigned int>(value[0]),
                    static_cast<unsigned int>(value[1]),
                    static_cast<unsigned int>(value[2]),
                    static_cast<unsigned int>(value[3])
                };
                gl::clear_tex_image(texture.get_impl().gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::unsigned_int, &clear_value[0]);
                return;
            }
            case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                const int clear_value[4] = {
                    static_cast<int>(value[0]),
                    static_cast<int>(value[1]),
                    static_cast<int>(value[2]),
                    static_cast<int>(value[3])
                };
                gl::clear_tex_image(texture.get_impl().gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::int_, &clear_value[0]);
                return;
            }
            case erhe::dataformat::Format_kind::format_kind_float: {
                const float clear_value[4] = {
                    static_cast<float>(value[0]),
                    static_cast<float>(value[1]),
                    static_cast<float>(value[2]),
                    static_cast<float>(value[3])
                };
                gl::clear_tex_image(texture.get_impl().gl_name(), 0, gl::Pixel_format::rgba, gl::Pixel_type::float_, &clear_value[0]);
                return;
            }
            case erhe::dataformat::Format_kind::format_kind_depth_stencil: {
                if ((depth_size_bits > 0) && (stencil_size_bits == 0)) {
                    const float clear_value[4] = {
                        static_cast<float>(value[0]),
                        static_cast<float>(value[1]),
                        static_cast<float>(value[2]),
                        static_cast<float>(value[3])
                    };
                    gl::clear_tex_image(texture.get_impl().gl_name(), 0, gl::Pixel_format::depth_component, gl::Pixel_type::float_, &clear_value[0]);
                    return;
                }
                // TODO - Currently falls through to renderpass path
                break;
            }
            default: {
                break;
            }
        }
    }

    Render_pass_descriptor render_pass_descriptor{};
    render_pass_descriptor.debug_label = erhe::utility::Debug_label{"Device_impl::clear_texture"};
    if (format_kind != erhe::dataformat::Format_kind::format_kind_depth_stencil) {
        render_pass_descriptor.color_attachments[0].texture        = &texture;
        render_pass_descriptor.color_attachments[0].load_action    = Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value[0] = value[0];
        render_pass_descriptor.color_attachments[0].clear_value[1] = value[1];
        render_pass_descriptor.color_attachments[0].clear_value[2] = value[2];
        render_pass_descriptor.color_attachments[0].clear_value[3] = value[3];
        render_pass_descriptor.color_attachments[0].usage_before   = Image_usage_flag_bit_mask::color_attachment;
        render_pass_descriptor.color_attachments[0].layout_before  = Image_layout::color_attachment_optimal;
        render_pass_descriptor.color_attachments[0].usage_after    = Image_usage_flag_bit_mask::color_attachment;
        render_pass_descriptor.color_attachments[0].layout_after   = Image_layout::color_attachment_optimal;
    } else {
        if (depth_size_bits > 0) {
            render_pass_descriptor.depth_attachment.texture        = &texture;
            render_pass_descriptor.depth_attachment.load_action    = Load_action::Clear;
            render_pass_descriptor.depth_attachment.clear_value[0] = value[0];
            render_pass_descriptor.depth_attachment.usage_before   = Image_usage_flag_bit_mask::depth_stencil_attachment;
            render_pass_descriptor.depth_attachment.layout_before  = Image_layout::depth_stencil_attachment_optimal;
            render_pass_descriptor.depth_attachment.usage_after    = Image_usage_flag_bit_mask::depth_stencil_attachment;
            render_pass_descriptor.depth_attachment.layout_after   = Image_layout::depth_stencil_attachment_optimal;
        }
        if (stencil_size_bits > 0) {
            render_pass_descriptor.stencil_attachment.texture        = &texture;
            render_pass_descriptor.stencil_attachment.load_action    = Load_action::Clear;
            render_pass_descriptor.stencil_attachment.clear_value[0] = value[1];
            render_pass_descriptor.stencil_attachment.usage_before   = Image_usage_flag_bit_mask::depth_stencil_attachment;
            render_pass_descriptor.stencil_attachment.layout_before  = Image_layout::depth_stencil_attachment_optimal;
            render_pass_descriptor.stencil_attachment.usage_after    = Image_usage_flag_bit_mask::depth_stencil_attachment;
            render_pass_descriptor.stencil_attachment.layout_after   = Image_layout::depth_stencil_attachment_optimal;
        }
    }
    render_pass_descriptor.render_target_width  = texture.get_width();
    render_pass_descriptor.render_target_height = texture.get_height();
    Render_pass render_pass{m_device, render_pass_descriptor};

    // clear_texture has no Command_buffer parameter, but the encoder /
    // Scoped_render_pass APIs require one. Allocate a transient cb out
    // of the current slot's pool; on GL the cb is just a typed handle
    // (no native command buffer), so this is essentially free.
    Command_buffer& transient_cb = get_command_buffer(0);
    Render_command_encoder clear_render_encoder = make_render_command_encoder(transient_cb);
    Scoped_render_pass scoped_render_pass{render_pass, transient_cb};
}

void Device_impl::start_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = (m_context_window != nullptr) ? m_context_window->get_device_pointer() : nullptr;
    RENDERDOC_WindowHandle  window = (m_context_window != nullptr) ? m_context_window->get_window_handle()  : nullptr;
    api->SetActiveWindow(device, window);
    api->StartFrameCapture(device, window);
    log_context->info("RenderDoc: StartFrameCapture()");
}

void Device_impl::end_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = (m_context_window != nullptr) ? m_context_window->get_device_pointer() : nullptr;
    RENDERDOC_WindowHandle  window = (m_context_window != nullptr) ? m_context_window->get_window_handle()  : nullptr;
    uint32_t result = api->EndFrameCapture(device, window);
    if (result == 0) {
        log_context->warn("RenderDoc: EndFrameCapture() failed");
        return;
    }
    if (api->IsTargetControlConnected()) {
        api->ShowReplayUI();
    } else {
        api->LaunchReplayUI(1, nullptr);
    }
}

void Device_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    // No-op for OpenGL -- image layouts are implicit
    static_cast<void>(texture);
    static_cast<void>(new_layout);
}

void Device_impl::cmd_texture_barrier(uint64_t usage_before, uint64_t usage_after)
{
    // No-op for OpenGL -- synchronization is implicit
    static_cast<void>(usage_before);
    static_cast<void>(usage_after);
}

auto Device_impl::get_command_buffer(unsigned int thread_slot) -> Command_buffer&
{
    // GL has no native command buffer object; a Command_buffer here
    // is a thin recording handle. Allocate a fresh wrapper, keep it
    // alive in m_command_buffers until the next wait_frame(), and
    // return a reference. Begin/end on the cb are no-ops on GL.
    auto label = erhe::utility::Debug_label{
        fmt::format("GL cb (thread_slot={}, allocation_index={})", thread_slot, m_command_buffers.size())
    };
    auto cb = std::make_unique<Command_buffer>(m_device, label);
    Command_buffer& ref = *cb;
    m_command_buffers.push_back(std::move(cb));
    return ref;
}

void Device_impl::submit_command_buffers(std::span<Command_buffer* const> command_buffers)
{
    // GL records straight into the driver context, so there's no
    // command-buffer submit work here. The only thing that matters is
    // the implicit-present hook: any cb that engaged a swapchain via
    // begin_swapchain has its Swapchain_impl pointer cached, and we
    // drive swap_buffers on it now.
    for (Command_buffer* command_buffer : command_buffers) {
        ERHE_VERIFY(command_buffer != nullptr);
        Swapchain_impl* swapchain = command_buffer->get_impl().take_swapchain_used();
        if (swapchain != nullptr) {
            const bool present_ok = swapchain->present();
            static_cast<void>(present_ok);
        }
    }
}

void Device_impl::submit_command_buffer_and_wait(Command_buffer& command_buffer)
{
    // GL has no fence-per-cb machinery here; submit then drain the driver.
    Command_buffer* command_buffers[] = { &command_buffer };
    submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    gl::finish();
}

auto Device_impl::make_blit_command_encoder(Command_buffer& command_buffer) -> Blit_command_encoder
{
    return Blit_command_encoder{m_device, command_buffer};
}

auto Device_impl::make_compute_command_encoder(Command_buffer& command_buffer) -> Compute_command_encoder
{
    return Compute_command_encoder{m_device, command_buffer};
}
auto Device_impl::make_render_command_encoder(Command_buffer& command_buffer) -> Render_command_encoder
{
    return Render_command_encoder{m_device, command_buffer};
}

void Device_impl::reset_shader_stages_state_tracker()
{
    get_state_tracker().shader_stages.reset();
}

auto Device_impl::push_program(const unsigned int program) -> Program_binding_guard
{
    return get_state_tracker().shader_stages.push_program(program);
}

auto Device_impl::get_draw_id_uniform_location() const -> GLint
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_gl_state_trackers[context_index].shader_stages.get_draw_id_uniform_location();
}

auto Device_impl::get_binding_state() -> Gl_binding_state&
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_gl_binding_states[context_index];
}

auto Device_impl::get_state_tracker() -> OpenGL_state_tracker&
{
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    return m_gl_state_trackers[context_index];
}

auto Device_impl::get_default_vertex_input_state() -> const Vertex_input_state*
{
    ERHE_VERIFY(m_default_vertex_input_state);
    return m_default_vertex_input_state.get();
}

void Device_impl::create_per_context_resources()
{
    // The main context's default vertex input state: a single device-owned
    // object that lives for the process and that every context needs, so it
    // is created eagerly rather than lazily on first draw. Created here -
    // from Device::Device's body - rather than in Device_impl's constructor,
    // because its destructor reaches Device::get_impl(), which requires
    // Device::m_impl to be wired for the object's whole lifetime. Each pool
    // context below populates its own slot as the last step of creating
    // that context, while it is current.
    ERHE_VERIFY(!m_default_vertex_input_state);
    m_default_vertex_input_state = std::make_unique<Vertex_input_state>(m_device);

    // The worker share-context pool. Created eagerly on the main thread:
    // SDL's share-context path make-currents the main context mid-creation
    // (sdl_window.cpp share ctor -> open()), creates a window, and
    // registers an event watch - all main-thread-only operations, so lazy
    // creation from a worker is not implementable. No window to share from
    // (headless / null window) means no pool, and
    // supports_worker_contexts() stays false - GPU-touching worker call
    // sites take their main-thread fallback.
    if (m_context_window == nullptr) {
        return;
    }
    for (int slot = 1; slot <= gl_worker_context_pool_size; ++slot) {
        // Leaves the new share context current on this (main) thread; a
        // creation failure aborts via the Context_window constructor's
        // verify, like the main context's own creation.
        auto worker_context_window = std::make_unique<erhe::window::Context_window>(m_context_window);
        worker_context_window->make_current(); // explicit, not relying on SDL leaving it current
        set_gl_context_index(slot);
        // glDebugMessageCallback is per-context: without this every GL
        // error a worker raises on this context is silently discarded.
        install_gl_debug_callback();
        // The context's own default-VAO instance, created while the
        // context is current - the const per-draw substitution path only
        // ever reads an already-populated own-context slot.
        {
            const Scoped_vertex_input_state scoped_default_vertex_input_state{m_device, *m_default_vertex_input_state.get()};
            ERHE_VERIFY(scoped_default_vertex_input_state.gl_name() != 0);
        }
        m_context_slot_live[slot].store(true, std::memory_order_release);
        worker_context_window->clear_current();
        m_worker_context_windows.push_back(std::move(worker_context_window));
        m_free_worker_context_slots.push_back(slot);
    }
    // Share-context creation stole the main context; restore it.
    m_context_window->make_current();
    set_gl_context_index(0);
    log_startup->info("Created {} GL worker share contexts", m_worker_context_windows.size());
}

auto Device_impl::supports_worker_contexts() const -> bool
{
    return !m_worker_context_windows.empty();
}

auto Device_impl::acquire_worker_context_slot(const std::source_location& location) -> int
{
    // Only a thread with no context current may acquire: the main thread
    // never comes here (Scoped_worker_context no-ops for it) and nested
    // worker scopes refcount instead of re-acquiring.
    ERHE_VERIFY(!gl_thread_has_context());
    ERHE_VERIFY(!m_worker_context_windows.empty());
    int slot = -1;
    {
        std::unique_lock<std::mutex> lock{m_worker_context_pool_mutex};
        // Acquire watchdog - proposal E of
        // doc/gl_worker_context_enforcement.md. This wait observes the real
        // deadlock condition the compile-time guards only approximate, and
        // a wedged pool looks BUSY from outside (parents parked in
        // Subflow::join spin in _corun_until), so without this report
        // nothing points at the context pool at all. Keep waiting rather
        // than abort: a long wait can also be legitimate contention.
        constexpr std::chrono::seconds watchdog_interval{10};
        while (
            !m_worker_context_pool_condition.wait_for(
                lock,
                watchdog_interval,
                [this]() { return !m_free_worker_context_slots.empty(); }
            )
        ) {
            std::string holders;
            for (std::size_t i = 0, end = m_worker_context_slot_holders.size(); i < end; ++i) {
                const Worker_context_slot_holder& holder = m_worker_context_slot_holders[i];
                std::ostringstream thread_id_stream;
                thread_id_stream << holder.thread_id;
                holders += fmt::format(
                    "  slot {}: thread {} acquired at {}:{}\n",
                    i + 1,
                    thread_id_stream.str(),
                    holder.acquire_site.file_name(),
                    holder.acquire_site.line()
                );
            }
            log_threads->error(
                "GL worker context pool: acquire from {}:{} has waited more than {} seconds with every slot held:\n{}",
                location.file_name(),
                location.line(),
                watchdog_interval.count(),
                holders
            );
        }
        slot = m_free_worker_context_slots.back();
        m_free_worker_context_slots.pop_back();
        m_worker_context_slot_holders[slot - 1] = Worker_context_slot_holder{
            .thread_id    = std::this_thread::get_id(),
            .acquire_site = location
        };
    }
    m_worker_context_windows[slot - 1]->make_current();
    set_gl_context_index(slot);
    // This context's drain point: names queued by destructors on other
    // contexts, and shared-object scrubs, since the last time this context
    // was current.
    drain_container_object_deletes_for_current_context();
    drain_shared_object_scrubs_for_current_context();
    return slot;
}

void Device_impl::release_worker_context_slot(const int slot)
{
    ERHE_VERIFY(gl_thread_is_worker_context());
    ERHE_VERIFY(get_gl_context_index() == slot);
    ERHE_VERIFY(slot >= 1);
    ERHE_VERIFY(slot <= static_cast<int>(m_worker_context_windows.size()));
    m_worker_context_windows[slot - 1]->clear_current();
    set_gl_context_index(-1);
    {
        const std::lock_guard<std::mutex> lock{m_worker_context_pool_mutex};
        m_worker_context_slot_holders[slot - 1] = Worker_context_slot_holder{};
        m_free_worker_context_slots.push_back(slot);
    }
    m_worker_context_pool_condition.notify_one();
}

void Device_impl::queue_vertex_array_delete_on_context(const int context_index, const unsigned int name)
{
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    ERHE_VERIFY(name != 0);
    Deferred_container_deletes& queue = m_deferred_container_deletes[context_index];
    const std::lock_guard<std::mutex> lock{queue.mutex};
    queue.vertex_arrays.push_back(name);
}

void Device_impl::queue_framebuffer_delete_on_context(const int context_index, const unsigned int name)
{
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    ERHE_VERIFY(name != 0);
    Deferred_container_deletes& queue = m_deferred_container_deletes[context_index];
    const std::lock_guard<std::mutex> lock{queue.mutex};
    queue.framebuffers.push_back(name);
}

void Device_impl::drain_container_object_deletes_for_current_context()
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    Deferred_container_deletes& queue = m_deferred_container_deletes[context_index];
    const std::lock_guard<std::mutex> lock{queue.mutex};
    for (unsigned int name : queue.vertex_arrays) {
        m_gl_binding_states[context_index].on_vertex_array_deleted(name);
        gl::delete_vertex_arrays(1, &name);
    }
    queue.vertex_arrays.clear();
    for (unsigned int name : queue.framebuffers) {
        m_gl_binding_states[context_index].on_framebuffer_deleted(name);
        gl::delete_framebuffers(1, &name);
    }
    queue.framebuffers.clear();
}

auto Device_impl::get_pending_container_delete_count(const int context_index) -> std::size_t
{
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    Deferred_container_deletes& queue = m_deferred_container_deletes[context_index];
    const std::lock_guard<std::mutex> lock{queue.mutex};
    return queue.vertex_arrays.size() + queue.framebuffers.size();
}

void Device_impl::on_shared_object_deleted(const Gl_shared_object_kind kind, const unsigned int name)
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    ERHE_VERIFY(name != 0);
    const int current_index = get_gl_context_index();
    ERHE_VERIFY(current_index >= 0);
    ERHE_VERIFY(current_index < gl_context_slot_count);

    // The deleting context: GL auto-unbinds the object here, so mirror the
    // auto-unbind in this context's cache only - no GL calls.
    Gl_binding_state& current_binding_state = m_gl_binding_states[current_index];
    switch (kind) {
        case Gl_shared_object_kind::buffer:       current_binding_state.on_buffer_deleted      (name); break;
        case Gl_shared_object_kind::texture:      current_binding_state.on_texture_deleted     (name); break;
        case Gl_shared_object_kind::sampler:      current_binding_state.on_sampler_deleted     (name); break;
        case Gl_shared_object_kind::renderbuffer: current_binding_state.on_renderbuffer_deleted(name); break;
        case Gl_shared_object_kind::program:      current_binding_state.on_program_deleted     (name); break;
        default: ERHE_FATAL("bad Gl_shared_object_kind %d", static_cast<int>(kind)); break;
    }

    // Every OTHER live context still has the orphan in its real binding
    // points; queue a scrub with that context's bind-epoch snapshot.
    for (int slot = 0; slot < gl_context_slot_count; ++slot) {
        if (slot == current_index) {
            continue;
        }
        if (!m_context_slot_live[slot].load(std::memory_order_acquire)) {
            continue;
        }
        const uint64_t enqueue_epoch = m_gl_binding_states[slot].get_bind_epoch();
        Deferred_shared_object_scrubs& queue = m_deferred_shared_object_scrubs[slot];
        const std::lock_guard<std::mutex> lock{queue.mutex};
        queue.entries.push_back(Deferred_shared_object_scrubs::Entry{kind, name, enqueue_epoch});
        // Disables bind elision on the target context until its drain: a
        // cached name may now refer to a deleted object whose name GL
        // recycles, so an equal name no longer proves the object is bound.
        m_gl_binding_states[slot].note_scrub_enqueued();
    }
}

void Device_impl::drain_shared_object_scrubs_for_current_context()
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    const int context_index = get_gl_context_index();
    ERHE_VERIFY(context_index >= 0);
    ERHE_VERIFY(context_index < gl_context_slot_count);
    Gl_binding_state& binding_state = m_gl_binding_states[context_index];
    Deferred_shared_object_scrubs& queue = m_deferred_shared_object_scrubs[context_index];
    const std::lock_guard<std::mutex> lock{queue.mutex};
    for (const Deferred_shared_object_scrubs::Entry& entry : queue.entries) {
        switch (entry.kind) {
            case Gl_shared_object_kind::buffer:       binding_state.scrub_deleted_buffer      (entry.name, entry.enqueue_epoch); break;
            case Gl_shared_object_kind::texture:      binding_state.scrub_deleted_texture     (entry.name, entry.enqueue_epoch); break;
            case Gl_shared_object_kind::sampler:      binding_state.scrub_deleted_sampler     (entry.name, entry.enqueue_epoch); break;
            case Gl_shared_object_kind::renderbuffer: binding_state.scrub_deleted_renderbuffer(entry.name, entry.enqueue_epoch); break;
            case Gl_shared_object_kind::program:      binding_state.scrub_deleted_program     (entry.name, entry.enqueue_epoch); break;
            default: ERHE_FATAL("bad Gl_shared_object_kind %d", static_cast<int>(entry.kind)); break;
        }
    }
    binding_state.note_scrubs_drained(queue.entries.size());
    queue.entries.clear();
}

// GL object creation
//
// gl::create_* creates the object immediately (no bind needed).

// Shared-object creators take ERHE_VERIFY_GL_THREAD_HAS_CONTEXT(): legal on
// any thread with a context current, main or worker. Container objects
// (vertex arrays, framebuffers) are per-context; their creators also take
// HAS_CONTEXT because per-object accessors guarantee the object being
// created belongs to the calling thread's own context. create_query is the
// exception: Gpu_timer is main-thread-only, so it takes MAIN_CONTEXT.

auto Device_impl::create_texture(gl::Texture_target target) -> Gl_texture
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name{0};
    gl::create_textures(target, 1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_texture{name, /*owned=*/true, this};
}

auto Device_impl::create_texture_view(gl::Texture_target target) -> Gl_texture
{
    // Texture views use gen_textures (name only, no object created yet).
    // The object is created later by glTextureView.
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    static_cast<void>(target);
    GLuint name{0};
    gl::gen_textures(1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_texture{name, /*owned=*/true, this};
}

auto Device_impl::create_buffer() -> Gl_buffer
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name{0};
    gl::create_buffers(1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_buffer{name, this};
}

auto Device_impl::create_renderbuffer() -> Gl_renderbuffer
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name{0};
    gl::create_renderbuffers(1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_renderbuffer{name, this};
}

auto Device_impl::create_sampler() -> Gl_sampler
{
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name{0};
    gl::create_samplers(1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_sampler{name, this};
}

auto Device_impl::create_query(gl::Query_target target) -> Gl_query
{
    // Only Gpu_timer_impl::create() calls this, and Gpu_timer is
    // main-thread-only (its per-context state is a ring of four queries).
    ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT();
    GLuint name{0};
    gl::create_queries(target, 1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_query{name};
}

auto Device_impl::create_program() -> Gl_program
{
    // glCreateProgram is not DSA — available since GL 2.0.
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name = gl::create_program();
    ERHE_VERIFY(name != 0);
    return Gl_program{name, this};
}

auto Device_impl::create_shader(gl::Shader_type type) -> Gl_shader
{
    // glCreateShader is not DSA — available since GL 2.0.
    ERHE_VERIFY_GL_THREAD_HAS_CONTEXT();
    GLuint name = gl::create_shader(type);
    ERHE_VERIFY(name != 0);
    return Gl_shader{name};
}

} // namespace erhe::graphics
