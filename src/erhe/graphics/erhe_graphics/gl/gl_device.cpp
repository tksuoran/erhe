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
#include "erhe_graphics/graphics_log.hpp"
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
    , m_gl_context_provider{device, m_gl_state_tracker}
{
    ERHE_PROFILE_FUNCTION();

    // Single source of truth for the bound VAO. The per-draw tracker and the
    // VAO-setup push/pop guards in Vertex_input_state_impl must share state,
    // or the per-draw cache can skip a needed bind after a guard restores 0.
    m_gl_state_tracker.set_binding_state(&m_gl_binding_state);

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

    const bool force_bindless_textures_off       = graphics_config.opengl.force_bindless_textures_off;
    const bool force_no_persistent_buffers       = graphics_config.opengl.force_no_persistent_buffers;
    const bool force_no_direct_state_access      = graphics_config.opengl.force_no_direct_state_access;
    const bool force_no_clip_control             = graphics_config.opengl.force_no_clip_control;
    const bool force_no_compute_shader           = graphics_config.opengl.force_no_compute_shader;
    const bool force_emulate_multi_draw_indirect = graphics_config.opengl.force_emulate_multi_draw_indirect;
    const int  force_gl_version                  = graphics_config.opengl.force_gl_version;
    const int  force_glsl_version                = graphics_config.opengl.force_glsl_version;
    const bool initial_clear                     = graphics_config.initial_clear;

    m_context_window = surface_create_info.context_window;

    if (force_gl_version > 0) {
        m_info.gl_version = force_gl_version;
        log_startup->warn("Forced GL version to be {} due to config setting", force_gl_version);
    }
    if (force_glsl_version > 0) {
        m_info.glsl_version = force_glsl_version;
        log_startup->warn("Forced GLSL version to be {} due to config setting", force_glsl_version);
    }

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
        if (m_info.gl_version >= 430) {
            gl::get_integer_v(gl::Get_p_name::max_framebuffer_samples, &m_info.max_framebuffer_samples);
        }
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
    // TODO: Add ARB_debug_output support for macOS GL 4.1
    m_info.use_debug_output = (m_info.gl_version >= 430);
    m_info.use_debug_groups = (m_info.gl_version >= 430);
    Scoped_debug_group_impl::s_enabled = m_info.use_debug_groups;
    log_startup->info("Debug output supported: {} (groups: {})", m_info.use_debug_output, m_info.use_debug_groups);

    if (m_info.use_debug_output) {
        ERHE_PROFILE_SCOPE("Debug Callback");
        gl::debug_message_callback(erhe_opengl_callback, nullptr);
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

    m_info.use_direct_state_access =
        (m_info.gl_version >= 450) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_direct_state_access);
    log_startup->info("Direct State Access supported: {}", m_info.use_direct_state_access);

    m_info.use_clip_control =
        (m_info.gl_version >= 450) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clip_control);
    log_startup->info("Clip Control supported: {}", m_info.use_clip_control);

    m_info.use_shader_storage_buffers =
        (m_info.gl_version >= 430) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object);
    if (force_no_compute_shader && m_info.use_shader_storage_buffers) {
        m_info.use_shader_storage_buffers = false;
        m_info.shader_storage_buffer_offset_alignment = 0;
        log_startup->warn("Force disabled shader storage buffers due to config setting force_no_compute_shader");
    }
    log_startup->info("SSBO supported: {}", m_info.use_shader_storage_buffers);
    if (m_info.use_shader_storage_buffers) {
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

    m_info.use_persistent_buffers = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
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

    m_info.use_compute_shader = m_info.gl_version >= 430;
    if (force_no_compute_shader) {
        if (m_info.use_compute_shader) {
            m_info.use_compute_shader = false;
            log_startup->warn("Force disabled compute shaders due to config setting");
        }
    }
    if (m_info.use_compute_shader) {
        log_startup->info("Compute shaders supported: true");
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
    } else {
        log_startup->info("Compute shaders supported: false");
        for (GLuint i = 0; i < 3; ++i) {
            m_info.max_compute_workgroup_count[i] = 0;
            m_info.max_compute_workgroup_size [i] = 0;
        }
        m_info.max_compute_work_group_invocations        = 0;
        m_info.max_compute_shared_memory_size            = 0;
        m_info.max_shader_storage_buffer_bindings        = 0;
        m_info.max_compute_shader_storage_blocks         = 0;
        m_info.max_compute_uniform_blocks                = 0;
        m_info.max_vertex_shader_storage_blocks          = 0;
        m_info.max_fragment_shader_storage_blocks        = 0;
        m_info.max_geometry_shader_storage_blocks        = 0;
        m_info.max_tess_control_shader_storage_blocks    = 0;
        m_info.max_tess_evaluation_shader_storage_blocks = 0;
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

    if (force_no_direct_state_access) {
        if (m_info.use_direct_state_access) {
            m_info.use_direct_state_access = false;
            log_startup->warn("Force disabled direct state access due to erhe.json setting");
        }
        // Persistent buffers require glBufferStorage (GL 4.4), which is not available
        // when DSA is disabled (pre-DSA path uses glBufferData instead)
        if (m_info.use_persistent_buffers) {
            m_info.use_persistent_buffers = false;
            log_startup->warn("Force disabled persistent buffers because direct state access is disabled (glBufferStorage not available)");
        }
    }

    if (force_no_clip_control) {
        if (m_info.use_clip_control) {
            m_info.use_clip_control = false;
            log_startup->warn("Force disabled clip control due to config setting");
        }
    }

    m_gl_state_tracker.vertex_input.set_use_dsa(m_info.use_direct_state_access);

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

    if (m_info.gl_version >= 430) {
        // GL 4.3+ path: use glGetInternalformativ (GL_ARB_internalformat_query2)
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
    } else {
        // GL < 4.3 fallback: probe formats by creating textures/renderbuffers
        // and checking for GL errors and framebuffer completeness.

        // Suspend wrapper-level error checking: the probe pattern intentionally
        // invokes failing GL calls and reads the result via gl::get_error().
        // With ERHE_GL_CHECK_ERRORS active (macOS debug), the wrapper would
        // consume the error before the probe can observe it, and the device
        // error callback would fire on every probed-but-unsupported format.
        gl_helpers::set_error_checking(false);

        // Helper: drain any pending GL errors
        auto drain_gl_errors = []() {
            while (gl::get_error() != gl::Error_code::no_error) {}
        };

        // Query global texture array limits (GL 3.0) - applied to all probed formats
        const int global_max_texture_size       = m_info.max_texture_size;
        const int global_max_array_texture_layers = m_info.max_array_texture_layers;

        // Helper: probe supported MSAA sample counts for a given internal format
        // by calling renderbuffer_storage_multisample with candidate counts and
        // checking for GL errors. GL 4.1 lacks glGetInternalformativ (4.2+), so
        // we must probe explicitly. Returns sorted ascending counts that include 1.
        auto probe_sample_counts = [&drain_gl_errors](gl::Internal_format format, int max_samples) -> std::vector<int>
        {
            std::vector<int> result;
            result.push_back(1);
            if (max_samples <= 1) {
                return result;
            }
            const int candidates[] = { 2, 4, 8, 16, 32 };
            for (const int count : candidates) {
                if (count > max_samples) {
                    break;
                }
                drain_gl_errors();
                GLuint rb = 0;
                gl::gen_renderbuffers(1, &rb);
                gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, rb);
                gl::renderbuffer_storage_multisample(gl::Renderbuffer_target::renderbuffer, count, format, 4, 4);
                const bool ok = (gl::get_error() == gl::Error_code::no_error);
                gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, 0);
                gl::delete_renderbuffers(1, &rb);
                drain_gl_errors();
                if (ok) {
                    result.push_back(count);
                }
            }
            return result;
        };

        // Determine pixel format and type for tex_image_2d probing of color formats
        struct Color_format_info
        {
            gl::Internal_format internal_format;
            gl::Pixel_format    pixel_format;
            gl::Pixel_type      pixel_type;
        };
        Color_format_info color_format_table[] = {
            { gl::Internal_format::r8,              gl::Pixel_format::red,  gl::Pixel_type::unsigned_byte },
            { gl::Internal_format::rg8,             gl::Pixel_format::rg,   gl::Pixel_type::unsigned_byte },
            { gl::Internal_format::rgba8,           gl::Pixel_format::rgba, gl::Pixel_type::unsigned_byte },
            { gl::Internal_format::srgb8_alpha8,    gl::Pixel_format::rgba, gl::Pixel_type::unsigned_byte },
            { gl::Internal_format::r11f_g11f_b10f,  gl::Pixel_format::rgb,  gl::Pixel_type::float_        },
            { gl::Internal_format::r16_snorm,       gl::Pixel_format::red,  gl::Pixel_type::short_        },
            { gl::Internal_format::r16f,            gl::Pixel_format::red,  gl::Pixel_type::float_        },
            { gl::Internal_format::rg16f,           gl::Pixel_format::rg,   gl::Pixel_type::float_        },
            { gl::Internal_format::rgba16f,         gl::Pixel_format::rgba, gl::Pixel_type::float_        },
            { gl::Internal_format::r32f,            gl::Pixel_format::red,  gl::Pixel_type::float_        },
            { gl::Internal_format::rg32f,           gl::Pixel_format::rg,   gl::Pixel_type::float_        },
            { gl::Internal_format::rgba32f,         gl::Pixel_format::rgba, gl::Pixel_type::float_        },
        };

        // Only sized depth/stencil formats; unsized formats (depth_component,
        // depth_stencil) are excluded because convert_from_gl cannot map them
        // to dataformat::Format for size queries.
        gl::Internal_format depth_stencil_formats[] = {
            gl::Internal_format::depth32f_stencil8,
            gl::Internal_format::depth24_stencil8,
            gl::Internal_format::stencil_index8,
            gl::Internal_format::depth_component16,
            gl::Internal_format::depth_component32f,
        };

        // Probe color formats using tex_image_2d
        for (const Color_format_info& info : color_format_table) {
            Format_properties properties{};

            drain_gl_errors();

            GLuint tex = 0;
            gl::gen_textures(1, &tex);
            gl::bind_texture(gl::Texture_target::texture_2d, tex);
            gl::tex_image_2d(
                gl::Texture_target::texture_2d,
                0,
                static_cast<GLint>(info.internal_format),
                4, 4, 0,
                info.pixel_format, info.pixel_type,
                nullptr
            );

            if (gl::get_error() != gl::Error_code::no_error) {
                drain_gl_errors();
                gl::bind_texture(gl::Texture_target::texture_2d, 0);
                gl::delete_textures(1, &tex);
                drain_gl_errors();
                log_startup->info("    {}: not supported", gl::c_str(info.internal_format));
                continue;
            }

            properties.supported = true;

            // Query actual component sizes from the created texture
            GLint red_size   = 0;
            GLint green_size = 0;
            GLint blue_size  = 0;
            GLint alpha_size = 0;
            gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, 0, gl::Get_texture_parameter::texture_red_size,   &red_size);
            gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, 0, gl::Get_texture_parameter::texture_green_size, &green_size);
            gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, 0, gl::Get_texture_parameter::texture_blue_size,  &blue_size);
            gl::get_tex_level_parameter_iv(gl::Texture_target::texture_2d, 0, gl::Get_texture_parameter::texture_alpha_size, &alpha_size);
            properties.red_size         = red_size;
            properties.green_size       = green_size;
            properties.blue_size        = blue_size;
            properties.alpha_size       = alpha_size;
            properties.depth_size       = 0;
            properties.stencil_size     = 0;
            properties.image_texel_size = (red_size + green_size + blue_size + alpha_size + 7) / 8;
            properties.filter           = true;  // All color formats support filtering
            properties.framebuffer_blend = true;

            // Probe color renderability by attaching to an FBO
            GLuint fbo = 0;
            gl::gen_framebuffers(1, &fbo);
            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, fbo);
            gl::framebuffer_texture_2d(
                gl::Framebuffer_target::framebuffer,
                gl::Framebuffer_attachment::color_attachment0,
                gl::Texture_target::texture_2d,
                tex, 0
            );
            gl::Framebuffer_status status = gl::check_framebuffer_status(gl::Framebuffer_target::framebuffer);
            properties.color_renderable = (status == gl::Framebuffer_status::framebuffer_complete);
            drain_gl_errors();

            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
            gl::delete_framebuffers(1, &fbo);
            gl::bind_texture(gl::Texture_target::texture_2d, 0);
            gl::delete_textures(1, &tex);
            drain_gl_errors();

            // Float formats do not support blending without GL_ARB_color_buffer_float
            if (info.pixel_type == gl::Pixel_type::float_) {
                properties.framebuffer_blend = properties.color_renderable;
            }

            // Probe supported MSAA sample counts. The current color_format_table
            // contains no integer formats, so GL_MAX_COLOR_TEXTURE_SAMPLES (and
            // GL_MAX_SAMPLES as the overall cap) is the appropriate bound.
            const int color_sample_cap = std::min(m_info.max_samples, m_info.max_color_texture_samples);
            properties.texture_2d_sample_counts = probe_sample_counts(info.internal_format, color_sample_cap);

            std::stringstream ss;
            ss << "    " << gl::c_str(info.internal_format)
               << ": R" << properties.red_size
               << " G" << properties.green_size
               << " B" << properties.blue_size
               << " A" << properties.alpha_size;
            if (properties.color_renderable) {
                ss << " color-renderable";
            }
            if (!properties.texture_2d_sample_counts.empty()) {
                ss << ", sample counts:";
                for (const int count : properties.texture_2d_sample_counts) {
                    ss << ' ' << count;
                }
            }
            log_startup->info(ss.str());

            properties.texture_2d_array_max_width  = global_max_texture_size;
            properties.texture_2d_array_max_height = global_max_texture_size;
            properties.texture_2d_array_max_layers = global_max_array_texture_layers;
            format_properties.insert({info.internal_format, properties});
        }

        // Probe depth/stencil formats using renderbuffer_storage
        for (const gl::Internal_format format : depth_stencil_formats) {
            Format_properties properties{};

            drain_gl_errors();

            GLuint rb = 0;
            gl::gen_renderbuffers(1, &rb);
            gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, rb);
            gl::renderbuffer_storage(gl::Renderbuffer_target::renderbuffer, format, 4, 4);

            if (gl::get_error() != gl::Error_code::no_error) {
                drain_gl_errors();
                gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, 0);
                gl::delete_renderbuffers(1, &rb);
                drain_gl_errors();
                log_startup->info("    {}: not supported", gl::c_str(format));
                continue;
            }

            properties.supported = true;

            // Determine depth/stencil sizes from format
            const erhe::dataformat::Format df = gl_helpers::convert_from_gl(format);
            properties.depth_size   = static_cast<int>(erhe::dataformat::get_depth_size_bits(df));
            properties.stencil_size = static_cast<int>(erhe::dataformat::get_stencil_size_bits(df));
            properties.image_texel_size = (properties.depth_size + properties.stencil_size + 7) / 8;
            properties.filter = (properties.depth_size > 0) && (properties.stencil_size == 0);

            // Probe renderability by attaching to an FBO
            GLuint fbo = 0;
            gl::gen_framebuffers(1, &fbo);
            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, fbo);

            // We need a color attachment for the FBO to be complete
            GLuint color_tex = 0;
            gl::gen_textures(1, &color_tex);
            gl::bind_texture(gl::Texture_target::texture_2d, color_tex);
            gl::tex_image_2d(
                gl::Texture_target::texture_2d,
                0,
                static_cast<GLint>(gl::Internal_format::rgba8),
                4, 4, 0,
                gl::Pixel_format::rgba, gl::Pixel_type::unsigned_byte,
                nullptr
            );
            gl::framebuffer_texture_2d(
                gl::Framebuffer_target::framebuffer,
                gl::Framebuffer_attachment::color_attachment0,
                gl::Texture_target::texture_2d,
                color_tex, 0
            );

            if ((properties.depth_size > 0) && (properties.stencil_size > 0)) {
                gl::framebuffer_renderbuffer(
                    gl::Framebuffer_target::framebuffer,
                    gl::Framebuffer_attachment::depth_stencil_attachment,
                    gl::Renderbuffer_target::renderbuffer,
                    rb
                );
            } else if (properties.depth_size > 0) {
                gl::framebuffer_renderbuffer(
                    gl::Framebuffer_target::framebuffer,
                    gl::Framebuffer_attachment::depth_attachment,
                    gl::Renderbuffer_target::renderbuffer,
                    rb
                );
            } else {
                gl::framebuffer_renderbuffer(
                    gl::Framebuffer_target::framebuffer,
                    gl::Framebuffer_attachment::stencil_attachment,
                    gl::Renderbuffer_target::renderbuffer,
                    rb
                );
            }

            gl::Framebuffer_status status = gl::check_framebuffer_status(gl::Framebuffer_target::framebuffer);
            bool renderable = (status == gl::Framebuffer_status::framebuffer_complete);
            properties.depth_renderable   = renderable && (properties.depth_size > 0);
            properties.stencil_renderable = renderable && (properties.stencil_size > 0);
            drain_gl_errors();

            gl::bind_framebuffer(gl::Framebuffer_target::framebuffer, 0);
            gl::delete_framebuffers(1, &fbo);
            gl::bind_texture(gl::Texture_target::texture_2d, 0);
            gl::delete_textures(1, &color_tex);
            gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, 0);
            gl::delete_renderbuffers(1, &rb);
            drain_gl_errors();

            // Probe supported MSAA sample counts. Depth/stencil formats are
            // capped by GL_MAX_DEPTH_TEXTURE_SAMPLES and GL_MAX_SAMPLES.
            const int depth_sample_cap = std::min(m_info.max_samples, m_info.max_depth_texture_samples);
            properties.texture_2d_sample_counts = probe_sample_counts(format, depth_sample_cap);

            std::stringstream ss;
            ss << "    " << gl::c_str(format)
               << ": D" << properties.depth_size
               << " S" << properties.stencil_size;
            if (properties.depth_renderable) {
                ss << " depth-renderable";
            }
            if (properties.stencil_renderable) {
                ss << " stencil-renderable";
            }
            if (!properties.texture_2d_sample_counts.empty()) {
                ss << ", sample counts:";
                for (const int count : properties.texture_2d_sample_counts) {
                    ss << ' ' << count;
                }
            }
            log_startup->info(ss.str());

            properties.texture_2d_array_max_width  = global_max_texture_size;
            properties.texture_2d_array_max_height = global_max_texture_size;
            properties.texture_2d_array_max_layers = global_max_array_texture_layers;
            format_properties.insert({format, properties});
        }

        gl_helpers::set_error_checking(true);
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

    // Hardware-capability clamp: reverse-Z requires native_depth_range =
    // zero_to_one, which on OpenGL needs glClipControl (GL 4.5 core or
    // GL_ARB_clip_control). macOS exposes GL 4.1 with neither, so the depth
    // range is locked at negative_one_to_one and the user's reverse_depth
    // preference cannot be honored. Force m_graphics_config.reverse_depth
    // to false here so every downstream reader (Light_interface's immutable
    // shadow comparison sampler, depth_compare_op selection, projection
    // matrices) sees the same effective value. Without this clamp the
    // shadow sampler is baked with greater_or_equal while the shadow map
    // is rendered/cleared for less_or_equal -- shadows render inverted.
    if ((m_info.coordinate_conventions.native_depth_range != erhe::math::Depth_range::zero_to_one) && m_graphics_config.reverse_depth) {
        log_startup->warn(
            "reverse_depth requested but hardware does not support glClipControl "
            "(GL {}.{}, ARB_clip_control={}); forcing reverse_depth=false",
            m_info.gl_version / 100, (m_info.gl_version / 10) % 10,
            gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clip_control)
        );
        m_graphics_config.reverse_depth = false;
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
    Ring_buffer_range    staging_buffer_range = m_staging_buffer->acquire(Ring_buffer_usage::CPU_write, length);
    std::span<std::byte> staging_buffer_span  = staging_buffer_range.get_span();
    memcpy(
        staging_buffer_span.data(),  // void *dst
        data,                        // const void *src
        length                       // size_t len
    );
    staging_buffer_range.bytes_written(length);
    staging_buffer_range.close();

    if (m_info.use_direct_state_access) {
        gl::copy_named_buffer_sub_data(
            staging_buffer_range.get_buffer()->get_buffer()->get_impl().gl_name(),  // GLuint   readBuffer
            buffer.get_impl().gl_name(),                                            // GLuint   writeBuffer
            staging_buffer_range.get_byte_start_offset_in_buffer(),                 // GLintptr readOffset
            offset,                                                                 // GLintptr writeOffset
            length                                                                  // GLsizeiptr size
        );
    } else {
        auto guard_read  = m_gl_binding_state.push_buffer(gl::Buffer_target::copy_read_buffer,  staging_buffer_range.get_buffer()->get_buffer()->get_impl().gl_name());
        auto guard_write = m_gl_binding_state.push_buffer(gl::Buffer_target::copy_write_buffer, buffer.get_impl().gl_name());
        gl::copy_buffer_sub_data(
            gl::Copy_buffer_sub_data_target::copy_read_buffer,   // GLenum   readTarget
            gl::Copy_buffer_sub_data_target::copy_write_buffer,   // GLenum   writeTarget
            staging_buffer_range.get_byte_start_offset_in_buffer(),  // GLintptr readOffset
            offset,                                                  // GLintptr writeOffset
            length                                                   // GLsizeiptr size
        );
    }

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

    const gl::Texture_target gl_target = convert_to_gl_texture_target(
        texture.get_texture_type(), texture.get_sample_count() != 0, texture.get_array_layer_count() != 0
    );

    if (m_info.use_direct_state_access) {
        gl::texture_sub_image_2d(
            texture.get_impl().gl_name(),
            level, x, y, width, height,
            gl_format, gl_type, data
        );
    } else {
        constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
        auto guard = m_gl_binding_state.push_texture(
            scratch_unit, gl_target, texture.get_impl().gl_name()
        );
        gl::tex_sub_image_2d(
            gl_target,
            level, x, y, width, height,
            gl_format, gl_type, data
        );
    }
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, std::move(callback));
}

void Device_impl::on_thread_enter()
{
    m_gl_state_tracker.on_thread_enter();
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
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

    // No existing usable buffer found, create new buffer
    const Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, 4 * byte_count),
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

    // format_x8_d24_unorm_pack32 maps to depth24_stencil8
    Format_properties result = i->second;
    if (erhe::dataformat::get_stencil_size_bits(format) == 0) {
        result.stencil_size = 0;
    }
    return result;
}

auto Device_impl::probe_image_format_support(const erhe::dataformat::Format format, const uint64_t usage_mask) const -> bool
{
    // OpenGL has no direct equivalent of vkGetPhysicalDeviceImageFormatProperties2.
    // The probe concept is Vulkan-specific; on GL we conservatively report "supported"
    // and let downstream texture creation fail if the combination is not renderable.
    static_cast<void>(format);
    static_cast<void>(usage_mask);
    return true;
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
    m_gl_state_tracker.shader_stages.reset();
}

auto Device_impl::push_program(const unsigned int program) -> Program_binding_guard
{
    return m_gl_state_tracker.shader_stages.push_program(program);
}

auto Device_impl::get_draw_id_uniform_location() const -> GLint
{
    return m_gl_state_tracker.shader_stages.get_draw_id_uniform_location();
}

auto Device_impl::get_binding_state() -> Gl_binding_state&
{
    return m_gl_binding_state;
}

// GL object creation
// GL object creation
//
// DSA path: gl::create_* creates the object immediately (no bind needed).
// Pre-DSA path (GL 4.1): gl::gen_* + push_* to temporarily bind (creating
// the object). RAII guards restore previous bindings on scope exit.

auto Device_impl::create_texture(gl::Texture_target target) -> Gl_texture
{
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_textures(target, 1, &name);
    } else {
        gl::gen_textures(1, &name);
        ERHE_VERIFY(name != 0);
        // Bind to a scratch texture unit to create the texture object.
        constexpr GLuint scratch_unit = Gl_binding_state::s_max_texture_units - 1;
        auto guard = m_gl_binding_state.push_texture(scratch_unit, target, name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_texture{name, /*owned=*/true, &m_gl_binding_state};
}

auto Device_impl::create_texture_view(gl::Texture_target target) -> Gl_texture
{
    // Texture views use gen_textures (name only, no object created yet).
    // The object is created later by glTextureView.
    static_cast<void>(target);
    GLuint name{0};
    gl::gen_textures(1, &name);
    ERHE_VERIFY(name != 0);
    return Gl_texture{name, /*owned=*/true, &m_gl_binding_state};
}

auto Device_impl::create_buffer() -> Gl_buffer
{
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_buffers(1, &name);
    } else {
        gl::gen_buffers(1, &name);
        ERHE_VERIFY(name != 0);
        auto guard = m_gl_binding_state.push_buffer(gl::Buffer_target::copy_write_buffer, name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_buffer{name, &m_gl_binding_state};
}

auto Device_impl::create_framebuffer() -> Gl_framebuffer
{
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_framebuffers(1, &name);
    } else {
        gl::gen_framebuffers(1, &name);
        ERHE_VERIFY(name != 0);
        auto guard = m_gl_binding_state.push_framebuffer(gl::Framebuffer_target::draw_framebuffer, name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_framebuffer{name, &m_gl_binding_state};
}

auto Device_impl::create_renderbuffer() -> Gl_renderbuffer
{
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_renderbuffers(1, &name);
    } else {
        gl::gen_renderbuffers(1, &name);
        ERHE_VERIFY(name != 0);
        auto guard = m_gl_binding_state.push_renderbuffer(name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_renderbuffer{name, &m_gl_binding_state};
}

auto Device_impl::create_sampler() -> Gl_sampler
{
    // glGenSamplers creates the sampler object immediately (no bind needed).
    // Same call works for both DSA and pre-DSA.
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_samplers(1, &name);
    } else {
        gl::gen_samplers(1, &name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_sampler{name, &m_gl_binding_state};
}

auto Device_impl::create_vertex_array() -> Gl_vertex_array
{
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_vertex_arrays(1, &name);
    } else {
        gl::gen_vertex_arrays(1, &name);
        ERHE_VERIFY(name != 0);
        auto guard = m_gl_binding_state.push_vertex_array(name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_vertex_array{name, &m_gl_binding_state};
}

auto Device_impl::create_query(gl::Query_target target) -> Gl_query
{
    // Queries are created on first use (glBeginQuery). glGenQueries is
    // sufficient for both DSA and pre-DSA.
    GLuint name{0};
    if (m_info.use_direct_state_access) {
        gl::create_queries(target, 1, &name);
    } else {
        static_cast<void>(target);
        gl::gen_queries(1, &name);
    }
    ERHE_VERIFY(name != 0);
    return Gl_query{name};
}

auto Device_impl::create_program() -> Gl_program
{
    // glCreateProgram is not DSA — available since GL 2.0.
    GLuint name = gl::create_program();
    ERHE_VERIFY(name != 0);
    return Gl_program{name, &m_gl_binding_state};
}

auto Device_impl::create_shader(gl::Shader_type type) -> Gl_shader
{
    // glCreateShader is not DSA — available since GL 2.0.
    GLuint name = gl::create_shader(type);
    ERHE_VERIFY(name != 0);
    return Gl_shader{name};
}

} // namespace erhe::graphics
