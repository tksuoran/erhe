// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gl/gl_blit_command_encoder.hpp"
#include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_debug.hpp"
#include "erhe_graphics/gl/gl_render_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/align.hpp"
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

auto digits_only(std::string s) -> std::string
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

auto get_string(gl::String_name string_name) -> std::string
{
    const GLubyte* gl_str = gl::get_string(string_name);
    const char*    c_str  = reinterpret_cast<const char*>(gl_str);
    return (c_str != nullptr) ? std::string{c_str} : std::string{};
}

//

Device_impl::Device_impl(Device& device, erhe::window::Context_window& context_window)
    : m_device             {device}
    , m_shader_monitor     {device}
    , m_gl_context_provider{device, m_gl_state_tracker}
    , m_context_window     {context_window}
{
    ERHE_PROFILE_FUNCTION();

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

    {
        ERHE_PROFILE_SCOPE("Query GL");

        {
            ERHE_PROFILE_SCOPE("gl::command_info_init");
            gl::command_info_init(m_info.glsl_version, extensions);
        }

        gl::get_float_v  (gl::Get_p_name::max_texture_max_anisotropy, &m_info.max_texture_max_anisotropy);
        gl::get_integer_v(gl::Get_p_name::max_samples,                &m_info.max_samples);
        gl::get_integer_v(gl::Get_p_name::max_color_texture_samples,  &m_info.max_color_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_depth_texture_samples,  &m_info.max_depth_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_framebuffer_samples,    &m_info.max_framebuffer_samples);
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

        gl::get_integer_v(gl::Get_p_name::max_texture_image_units,          &m_info.max_texture_image_units);
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

    int shader_storage_buffer_offset_alignment{0};
    int uniform_buffer_offset_alignment       {0};

    if (m_info.gl_version >= 430) {
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

        gl::get_integer_v(gl::Get_p_name::shader_storage_buffer_offset_alignment,    &shader_storage_buffer_offset_alignment);
        gl::get_integer_v(gl::Get_p_name::max_shader_storage_buffer_bindings,        &m_info.max_shader_storage_buffer_bindings);
        gl::get_integer_v(gl::Get_p_name::max_compute_shader_storage_blocks,         &m_info.max_compute_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_compute_uniform_blocks,                &m_info.max_compute_uniform_blocks);
        gl::get_integer_v(gl::Get_p_name::max_vertex_shader_storage_blocks,          &m_info.max_vertex_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_fragment_shader_storage_blocks,        &m_info.max_fragment_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_geometry_shader_storage_blocks,        &m_info.max_geometry_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_control_shader_storage_blocks,    &m_info.max_tess_control_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_shader_storage_blocks, &m_info.max_tess_evaluation_shader_storage_blocks);
        m_info.use_compute_shader = true;
    } else {
        m_info.use_compute_shader = false;
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

    gl::get_integer_v(gl::Get_p_name::uniform_buffer_offset_alignment,           &uniform_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::max_uniform_block_size,                    &m_info.max_uniform_block_size);
    gl::get_integer_v(gl::Get_p_name::max_uniform_buffer_bindings,               &m_info.max_uniform_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_blocks,                 &m_info.max_vertex_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_vectors,                &m_info.max_vertex_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_blocks,               &m_info.max_fragment_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_vectors,              &m_info.max_fragment_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_geometry_uniform_blocks,               &m_info.max_geometry_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_control_uniform_blocks,           &m_info.max_tess_control_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_uniform_blocks,        &m_info.max_tess_evaluation_uniform_blocks);

    m_info.shader_storage_buffer_offset_alignment = static_cast<unsigned int>(shader_storage_buffer_offset_alignment);
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

    bool force_bindless_textures_off {false};
    bool force_no_persistent_buffers {false};
    bool force_no_direct_state_access{false};
    bool force_no_multi_draw_indirect{false};
    int  force_gl_version            {false};
    int  force_glsl_version          {false};
    bool capture_support             {false};
    bool initial_clear               {false};
    {
        const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "graphics");
        //// ini.get("post_processing",              configuration.post_processing);
        //// ini.get("use_time_query",               configuration.use_time_query );
        ini.get("force_bindless_textures_off",  force_bindless_textures_off);
        ini.get("force_no_persistent_buffers",  force_no_persistent_buffers);
        ini.get("force_no_direct_state_access", force_no_direct_state_access);
        ini.get("force_no_multi_draw_indirect", force_no_multi_draw_indirect);
        ini.get("force_gl_version",             force_gl_version);
        ini.get("force_glsl_version",           force_glsl_version);
        ini.get("initial_clear",                initial_clear);
    }

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_bindless_texture)) {
        m_info.use_bindless_texture = true;
    }
    if (m_info.vendor == Vendor::Intel) {
        m_info.use_bindless_texture = false;
    }
    log_startup->info("GL_ARB_bindless_texture supported : {}", m_info.use_bindless_texture);
    if (m_info.use_bindless_texture) {
#if defined(ERHE_SPIRV)
        // 'GL_ARB_bindless_texture' : not allowed when using generating SPIR-V codes
        m_info.use_bindless_texture = false;
        log_startup->warn("Force disabled GL_ARB_bindless_texture due to ERHE_SPIRV cmake setting");
#else
        if (force_bindless_textures_off) {
            m_info.use_bindless_texture = false;
            log_startup->warn("Force disabled GL_ARB_bindless_texture due to erhe.ini setting force_bindless_textures_off");
        }
        else
        if (capture_support) {
            m_info.use_bindless_texture = false;
            log_startup->warn("Force disabled GL_ARB_bindless_texture due to erhe.ini enabling RenderDoc capture");
        }
#endif
    }
    m_info.use_clear_texture = (m_info.gl_version >= 440) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_clear_texture);
    log_startup->info("GL_ARB_clear_texture supported : {}", m_info.use_clear_texture);

    const bool use_direct_state_access =
        (m_info.gl_version >= 450) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_direct_state_access);
    if (!use_direct_state_access) {
        log_startup->error(
            "Your graphics driver does not support OpenGL direct state access. "
            "OpenGL version 4.5 or GL_ARB_direct_state_access is required. "
            "This is a fatal error."
        );
    }

    const bool use_shader_storage_buffer_object =
        (m_info.gl_version >= 430) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object);
    if (!use_shader_storage_buffer_object) {
        log_startup->error(
            "Your graphics driver does not support OpenGL shader storage buffer object. "
            "OpenGL version 4.3 or GL_ARB_shader_storage_buffer_object is required. "
            "This is a fatal error."
        );
    }

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_sparse_texture)) {
        ERHE_PROFILE_SCOPE("Sparse texture");

        m_info.use_sparse_texture = true;
        gl::get_integer_v(gl::Get_p_name::max_sparse_texture_size_arb, &m_info.max_sparse_texture_size);
        log_startup->info("max sparse texture size : {}", m_info.max_sparse_texture_size);
    }
    log_startup->info("GL_ARB_sparse_texture supported : {}", m_info.use_sparse_texture);

    m_info.use_persistent_buffers = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
    m_info.use_multi_draw_indirect = (m_info.gl_version >= 430) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_multi_draw_indirect);
    log_startup->info("Persistent Buffers supported:  {}", m_info.use_sparse_texture);
    log_startup->info("Multi Draw Indirect supported: {}", m_info.use_multi_draw_indirect);

    if (force_gl_version > 0) {
        m_info.gl_version = force_gl_version;
        log_startup->warn("Forced GL version to be {} due to erhe.ini setting", force_gl_version);
    }
    if (force_glsl_version > 0) {
        m_info.glsl_version = force_glsl_version;
        log_startup->warn("Forced GLSL version to be {} due to erhe.ini setting", force_glsl_version);
    }

    if (m_info.use_multi_draw_indirect) { 
        if (force_no_multi_draw_indirect) {
            m_info.use_multi_draw_indirect = false;
            log_startup->warn("Force disabled multi draw indirect due to erhe.ini setting");
        }
    }

    if (force_no_persistent_buffers) {
        if (m_info.use_persistent_buffers) {
            m_info.use_persistent_buffers = false;
            log_startup->warn("Force disabled persistently mapped buffers due to erhe.ini setting");
        }
    }

    if (initial_clear) {
        gl::clear_color(0.2f, 0.2f, 0.2f, 0.2f);
        for (int i = 0; i < 3; ++i) {
            gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
            context_window.swap_buffers();
        }
    }

    {
        const auto& ini = erhe::configuration::get_ini_file_section(c_erhe_config_file_path, "renderdoc");
        ini.get("capture_support", capture_support);
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

    {
        ERHE_PROFILE_SCOPE("Start shader monitor");
        m_shader_monitor.begin();
    }

    gl::enable(gl::Enable_cap::scissor_test);

    std::fill(
        m_frame_syncs.begin(),
        m_frame_syncs.end(),
        Frame_sync{
            .frame_number = 0,
            .fence_sync   = nullptr,
            .result       = gl::Sync_status::timeout_expired
        }
    );
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    if (m_info.use_bindless_texture) {
        return gl::get_texture_sampler_handle_arb(texture.get_impl().gl_name(), sampler.get_impl().gl_name());
    } else {
        const uint64_t texture_name  = static_cast<uint64_t>(texture.get_impl().gl_name());
        const uint64_t sampler_name  = static_cast<uint64_t>(sampler.get_impl().gl_name());
        const uint64_t handle        = texture_name | (sampler_name << 32);
        return handle;
    }
}

auto Device_impl::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> erhe::dataformat::Format
{
    using namespace erhe::utility;
    const bool require_depth     = test_bit_set(flags, format_flag_require_depth    );
    const bool require_stencil   = test_bit_set(flags, format_flag_require_stencil  );
    const bool prefer_accuracy   = test_bit_set(flags, format_flag_prefer_accuracy  );
    const bool prefer_filterable = test_bit_set(flags, format_flag_prefer_filterable);
    erhe::dataformat::Format formats[] = {
        erhe::dataformat::Format::format_d16_unorm,
        erhe::dataformat::Format::format_x8_d24_unorm_pack32,
        erhe::dataformat::Format::format_d32_sfloat,
        erhe::dataformat::Format::format_s8_uint,
        erhe::dataformat::Format::format_d24_unorm_s8_uint,
        erhe::dataformat::Format::format_d32_sfloat_s8_uint
    };

    erhe::dataformat::Format best_format = erhe::dataformat::Format::format_undefined;
    float best_score = 0.0f;
    for (const erhe::dataformat::Format format : formats) {
        Format_properties properties = get_format_properties(format);
        if (!properties.supported) {
            continue;
        }
        if (require_depth) {
            if ((properties.depth_size == 0) || !properties.depth_renderable) {
                continue;
            }
        }
        if (require_stencil) {
            if ((properties.stencil_size == 0) || !properties.stencil_renderable) {
                continue;
            }
        }
        if (sample_count != 0) {
            auto i = std::find(properties.texture_2d_sample_counts.begin(), properties.texture_2d_sample_counts.end(), sample_count);
            if (i == properties.texture_2d_sample_counts.end()) {
                continue;
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
        if (score > best_score) {
            best_format = format;
            best_score = score;
        }
    }
    return best_format;
}

auto Device_impl::create_dummy_texture() -> std::shared_ptr<Texture>
{
    const Texture::Create_info create_info{
        .device      = m_device,
        .width       = 2,
        .height      = 2,
        .debug_label = "dummy"
    };

    auto texture = std::make_shared<Texture>(m_device, create_info);
    const std::array<uint8_t, 16> dummy_pixel{
        0xee, 0x11, 0xdd, 0xff,  0xcc, 0x11, 0xbb, 0xff,
        0xcc, 0x11, 0xbb, 0xff,  0xee, 0x11, 0xdd, 0xff,
    };
    const std::span<const std::uint8_t> image_data{&dummy_pixel[0], dummy_pixel.size()};

    std::span<const std::uint8_t> src_span{dummy_pixel.data(), dummy_pixel.size()};
    std::size_t                   byte_count   = src_span.size_bytes();
    GPU_ring_buffer_client        texture_upload_buffer{m_device, erhe::graphics::Buffer_target::pixel, "dummy texture upload"};
    Buffer_range                  buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    std::span<std::byte>          dst_span     = buffer_range.get_span();
    memcpy(dst_span.data(), src_span.data(), byte_count);
    buffer_range.bytes_written(byte_count);
    buffer_range.close();

    const int src_bytes_per_row   = 2 * 4;
    const int src_bytes_per_image = 2 * src_bytes_per_row;
    Blit_command_encoder encoder{m_device};
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

Texture_heap_impl::Texture_heap_impl(
    Device&        device,
    const Texture& fallback_texture,
    const Sampler& fallback_sampler,
    std::size_t    reserved_slot_count
)
    : m_device             {device}
    , m_fallback_texture   {fallback_texture}
    , m_fallback_sampler   {fallback_sampler}
    , m_reserved_slot_count{reserved_slot_count}
    , m_used_slot_count    {0}
{
    // log_texture_frame->trace("Texture_heap_impl::Texture_heap_impl()");

    if (m_device.get_info().use_bindless_texture) {
        const uint64_t fallback_texture_handle = m_device.get_handle(m_fallback_texture, m_fallback_sampler);
        m_textures.resize(m_reserved_slot_count);
        m_samplers.resize(m_reserved_slot_count);
        m_assigned.resize(m_reserved_slot_count);
        m_gl_bindless_texture_handles.resize(m_reserved_slot_count);
        m_gl_bindless_texture_resident.resize(m_reserved_slot_count);
        std::fill(m_assigned.begin(), m_assigned.end(), false);
        std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
        std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
        std::fill(m_gl_bindless_texture_handles.begin(), m_gl_bindless_texture_handles.end(), fallback_texture_handle);
        std::fill(m_gl_bindless_texture_resident.begin(), m_gl_bindless_texture_resident.end(), false);
        m_used_slot_count = 0;
    } else {
        m_textures.resize(device.get_info().max_texture_image_units);
        m_samplers.resize(device.get_info().max_texture_image_units);
        m_gl_textures.resize(device.get_info().max_texture_image_units);
        m_gl_samplers.resize(device.get_info().max_texture_image_units);
        m_zero_vector.resize(device.get_info().max_texture_image_units);
        reset();
    }
}

Texture_heap_impl::~Texture_heap_impl()
{
    // log_texture_frame->trace("Texture_heap_impl::~Texture_heap_impl()");
}

void Texture_heap_impl::reset()
{
    // log_texture_frame->trace("Texture_heap_impl::reset()");

    if (!m_device.get_info().use_bindless_texture) {
        const GLuint fallback_texture_name = m_fallback_texture.get_impl().gl_name();
        const GLuint fallback_sampler_name = m_fallback_sampler.get_impl().gl_name();
        std::fill(m_textures.begin(), m_textures.end(), &m_fallback_texture);
        std::fill(m_samplers.begin(), m_samplers.end(), &m_fallback_sampler);
        std::fill(m_gl_textures.begin(), m_gl_textures.end(), fallback_texture_name);
        std::fill(m_gl_samplers.begin(), m_gl_samplers.end(), fallback_sampler_name);
        m_used_slot_count = 0;
    } else {
        ERHE_FATAL("This should not happen");
    }
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    ERHE_VERIFY(texture != nullptr);
    ERHE_VERIFY(sampler != nullptr);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            if (m_device.get_info().use_bindless_texture) {
                return m_gl_bindless_texture_handles[slot];
            } else {
                return static_cast<uint64_t>(slot - m_reserved_slot_count);
            }
        }
    }
    ERHE_FATAL("texture %u sampler %u not found in texture heap", texture->get_impl().gl_name(), sampler->get_impl().gl_name());
}

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if (texture == nullptr) {
        texture = &m_fallback_texture;
    }
    if (sampler == nullptr) {
        sampler = &m_fallback_sampler;
    }

    if (m_device.get_info().use_bindless_texture) {
        const uint64_t gl_bindless_texture_handle = m_device.get_handle(*texture, *sampler);
        m_assigned                    [slot] = true;
        m_gl_bindless_texture_handles [slot] = gl_bindless_texture_handle;
        m_gl_bindless_texture_resident[slot] = false;
        m_textures                    [slot] = texture;
        m_samplers                    [slot] = sampler;
        // log_texture_frame->trace("assigned texture heap slot {} for texture {}, sampler {} bindless handle {}", slot, texture->gl_name(), sampler->gl_name(), format_texture_handle(gl_bindless_texture_handle));
        return gl_bindless_texture_handle;

    } else {

        m_textures   [slot] = texture;
        m_samplers   [slot] = sampler;
        m_gl_textures[slot] = texture->get_impl().gl_name();
        m_gl_samplers[slot] = sampler->get_impl().gl_name();
        // log_texture_frame->trace("assigned texture heap slot {} for texture {}, sampler {}", slot, texture->gl_name(), sampler->gl_name());
        return slot;
    }
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    if ((texture == nullptr) || (sampler == nullptr)) {
        return invalid_texture_handle;
    }

    // const GLuint texture_name = texture->gl_name(); // get_texture_from_handle(handle);
    // const GLuint sampler_name = sampler->gl_name(); // get_sampler_from_handle(handle);

    for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
        if ((m_textures[slot] == texture) && (m_samplers[slot] == sampler)) {
            // log_texture_frame->trace("cache hit texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
            if (m_device.get_info().use_bindless_texture) {
                return m_gl_bindless_texture_handles[slot];
            } else {
                return static_cast<uint64_t>(slot);
            }
        }
    }

    if (m_device.get_info().use_bindless_texture) {
        // const std::size_t slot = m_reserved_slot_count + m_used_slot_count;
        const uint64_t gl_bindless_texture_handle = m_device.get_handle(*texture, *sampler);
        m_gl_bindless_texture_handles .push_back(gl_bindless_texture_handle);
        m_gl_bindless_texture_resident.push_back(false);
        m_textures                    .push_back(texture);
        m_samplers                    .push_back(sampler);
        ++m_used_slot_count;
        // log_texture_frame->trace(
        //     "allocated texture heap slot {} for texture {}, sampler {} bindless handle = {}",
        //     slot, texture_name, sampler_name, format_texture_handle(gl_bindless_texture_handle)
        // );
        return gl_bindless_texture_handle;

    } else {

        if (m_reserved_slot_count + m_used_slot_count < m_textures.size()) {
            const std::size_t slot = m_reserved_slot_count + m_used_slot_count;
            m_textures   [slot] = texture;
            m_samplers   [slot] = sampler;
            m_gl_textures[slot] = texture->get_impl().gl_name();
            m_gl_samplers[slot] = sampler->get_impl().gl_name();
            ++m_used_slot_count;
            // log_texture_frame->trace("allocated texture heap slot {} for texture {}, sampler {}", slot, texture_name, sampler_name);
            return static_cast<uint64_t>(slot - m_reserved_slot_count);
        }

        // log_texture_frame->trace("texture heap is full, unable to allocate slot for texture {}, sampler {}", texture_name, sampler_name);
        return {};
    }
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap_impl::unbind()
{
    // log_texture_frame->trace("Texture_heap_impl::unbind()");

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                // log_texture_frame->trace(
                //     "making texture handle {} non-resident / texture {}, sampler {}",
                //     format_texture_handle(gl_bindless_texture_handle),
                //     m_textures[slot]->gl_name(),
                //     m_samplers[slot]->gl_name()
                // );
                gl::make_texture_handle_non_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = false;
            }
        }
    } else {
        gl::bind_textures(0, m_device.get_info().max_texture_image_units, m_zero_vector.data());
        gl::bind_samplers(0, m_device.get_info().max_texture_image_units, m_zero_vector.data());
    }
}

// TODO Maybe this should use Render_command_encoder?
auto Texture_heap_impl::bind() -> std::size_t
{
    // log_texture_frame->trace("Texture_heap_impl::bind()");

    if (m_device.get_info().use_bindless_texture) {
        for (std::size_t slot = 0; slot < m_reserved_slot_count + m_used_slot_count; ++slot) {
            if ((slot < m_reserved_slot_count) && !m_assigned[slot]) {
                continue;
            }
            if (!m_gl_bindless_texture_resident[slot]) {
                const uint64_t gl_bindless_texture_handle = m_gl_bindless_texture_handles[slot];
                // log_texture_frame->trace(
                //     "making texture handle {} resident / texture {}, sampler {}",
                //     format_texture_handle(gl_bindless_texture_handle),
                //     m_textures[slot]->gl_name(),
                //     m_samplers[slot]->gl_name()
                // );
                gl::make_texture_handle_resident_arb(gl_bindless_texture_handle);
                m_gl_bindless_texture_resident[slot] = true;
            }
        }
        return m_used_slot_count;

    } else {

        gl::bind_textures(0, m_device.get_info().max_texture_image_units, m_gl_textures.data());
        gl::bind_samplers(0, m_device.get_info().max_texture_image_units, m_gl_samplers.data());
        return m_used_slot_count;
    }
}

auto Device_impl::get_buffer_alignment(Buffer_target target) -> std::size_t
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

Device_impl::~Device_impl() = default;

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

void Device_impl::upload_to_buffer(Buffer& buffer, size_t offset, const void* data, size_t length)
{
    // TODO Use persistent staging buffer, maybe GPU_ring_buffer_impl?
    Buffer_create_info create_info{
        .capacity_byte_count = length,
        .usage               = Buffer_usage     ::transfer,
        .direction           = Buffer_direction ::cpu_to_gpu,
        .cache_mode          = Buffer_cache_mode::default_,
        .mapping             = Buffer_mapping   ::not_mappable,
        .coherency           = Buffer_coherency ::on,
        .init_data           = data,
        .debug_label         = "Staging buffer"
    };
    Buffer staging_buffer{m_device, create_info};
    gl::copy_named_buffer_sub_data(staging_buffer.get_impl().gl_name(), buffer.get_impl().gl_name(), 0, offset, length);
}

void Device_impl::add_completion_handler(std::function<void()> callback)
{
    m_completion_handlers.emplace_back(m_frame_number, callback);
}

void Device_impl::on_thread_enter()
{
    m_gl_state_tracker.on_thread_enter();
}

// Ring buffer

GPU_ring_buffer_impl::GPU_ring_buffer_impl(
    Device&                            device,
    GPU_ring_buffer&                   ring_buffer,
    const GPU_ring_buffer_create_info& create_info
)
    : m_device     {device}
    , m_ring_buffer{ring_buffer}
    , m_direction  {create_info.direction}
    , m_buffer{
        std::make_unique<Buffer>(
            m_device,
            Buffer_create_info{
                .capacity_byte_count = create_info.size,
                .usage               = create_info.usage,
                .direction           = create_info.direction,
                .cache_mode          = Buffer_cache_mode::default_,
                .mapping             = Buffer_mapping::persistent,
                .coherency           = Buffer_coherency::on, // TODO explicitly flush range?
                .debug_label         = create_info.debug_label
            }
        )
    }
    , m_read_wrap_count{0}
    , m_read_offset    {m_buffer->get_capacity_byte_count()}
{
}

auto GPU_ring_buffer_impl::get_buffer() -> Buffer*
{
    return m_buffer.get();
}

auto GPU_ring_buffer_impl::get_name() const -> const std::string&
{
    return m_name;
}

void GPU_ring_buffer_impl::sanity_check()
{
    ERHE_VERIFY(m_write_position <= m_buffer->get_capacity_byte_count());

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
    } else {
        ERHE_FATAL("buffer issue");
    }
}

void GPU_ring_buffer_impl::get_size_available_for_write(
    std::size_t  required_alignment,
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    // Initial situation:
    //   +--------------------------+
    //   ^                          ^
    //   w1                         r0

    //  CPU progress:
    //   +------+---------------+---+
    //                          ^   ^
    //                          w1  r0

    //  GPU progress:
    //   +------+---------------+---+
    //          ^               ^    
    //          r1              w1   

    //  CPU progress:
    //   +----+---+---+--------------+-+
    //        ^   ^                     
    //        w2  r1                    
    const std::size_t aligned_write_position = erhe::utility::align_offset_power_of_two(m_write_position, required_alignment);
    ERHE_VERIFY(aligned_write_position >= m_write_position);
    out_alignment_byte_count_without_wrap    = aligned_write_position - m_write_position;

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
        out_available_byte_count_without_wrap = m_read_offset - m_write_position;
        out_available_byte_count_with_wrap = 0; // cannot wrap because GPU is still using the buffer from this point
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
        out_available_byte_count_without_wrap = m_buffer->get_capacity_byte_count() - m_write_position;
        out_available_byte_count_with_wrap = m_read_offset;
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else {
        ERHE_FATAL("buffer issue");
        out_available_byte_count_without_wrap = 0;
        out_available_byte_count_with_wrap = 0;
        return;
    }
}

auto GPU_ring_buffer_impl::acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap   {0};
    bool wrap{false};

    sanity_check();

    get_size_available_for_write(required_alignment, alignment_byte_count_without_wrap, available_byte_count_without_wrap, available_byte_count_with_wrap);

    if (byte_count == 0) {
        byte_count = std::max(available_byte_count_without_wrap, available_byte_count_with_wrap);
    }

    wrap = (byte_count > available_byte_count_without_wrap);
    if (wrap && (byte_count > available_byte_count_with_wrap)) {
        return {}; // Not enough space currently available
    }

    if (wrap) {
        wrap_write();
    } else {
        m_write_position += alignment_byte_count_without_wrap;
    }

    sanity_check();

    if (!m_device.get_info().use_persistent_buffers) {
        if (usage == Ring_buffer_usage::CPU_write) {
            m_buffer->begin_write(m_write_position, byte_count); // maps requested range
        }
        Buffer_range result{m_ring_buffer, usage, m_buffer->get_map(), m_write_wrap_count, m_write_position};
        m_write_position += byte_count;
        sanity_check();
        return result;
    } else {
        size_t buffer_mapped_span_byte_count = m_buffer->get_map().size_bytes();
        ERHE_VERIFY(m_write_position + byte_count <= buffer_mapped_span_byte_count);
        Buffer_range result{m_ring_buffer, usage, m_buffer->get_map().subspan(m_write_position, byte_count), m_write_wrap_count, m_write_position};
        m_write_position += byte_count;
        sanity_check();
        return result;
    }
}

auto GPU_ring_buffer_impl::get_direction() const -> Buffer_direction
{
    return m_direction;
}

void GPU_ring_buffer_impl::flush(std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (!m_device.get_info().use_persistent_buffers) {
        m_buffer->flush_bytes(byte_offset, byte_count);
    }
}

void GPU_ring_buffer_impl::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (!m_device.get_info().use_persistent_buffers) {
        m_buffer->end_write(byte_offset, byte_write_count); // flush and unmap
    }
    //m_map = {};
}

GPU_ring_buffer_impl::~GPU_ring_buffer_impl()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();
}

void GPU_ring_buffer_impl::frame_completed(uint64_t completed_frame)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    for (Ring_buffer_sync_entry& entry : m_sync_entries) {
        if (entry.waiting_for_frame == completed_frame) {
            if (
                (entry.wrap_count > m_read_wrap_count) ||
                (
                    (entry.wrap_count == m_read_wrap_count) &&
                    (entry.byte_offset + entry.byte_count > m_read_offset)
                )
            ) {
                size_t new_read_wrap_count = entry.wrap_count;
                size_t new_read_offset     = entry.byte_offset + entry.byte_count;
                if (m_write_wrap_count == new_read_wrap_count + 1) {
                    ERHE_VERIFY(new_read_offset >= m_write_position);
                } else if (new_read_wrap_count == m_write_wrap_count) {
                    ERHE_VERIFY(m_write_position >= new_read_offset);
                } else {
                    ERHE_FATAL("buffer issue");
                }

                m_read_wrap_count = new_read_wrap_count;
                m_read_offset = new_read_offset;
                sanity_check();
            }
        }
    }

    auto i = std::remove_if(
        m_sync_entries.begin(),
        m_sync_entries.end(),
        [completed_frame](Ring_buffer_sync_entry& entry) { return entry.waiting_for_frame == completed_frame; }
    );
    if (i != m_sync_entries.end()) {
        m_sync_entries.erase(i, m_sync_entries.end());
    }
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    for (const std::unique_ptr<GPU_ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(completed_frame);
    }
    for (const Completion_handler& entry : m_completion_handlers) {
        if (entry.frame_number == completed_frame) {
            entry.callback();
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

void Device_impl::start_of_frame()
{
    ++m_frame_number;
}

void Device_impl::end_of_frame()
{
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
                frame_sync.frame_number = m_frame_number,
                frame_sync.fence_sync   = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0),
                frame_sync.result       = gl::Sync_status::timeout_expired;
                m_pending_frames.push_back(m_frame_number);
                m_need_sync = false;
                return;
            }
        }
        log_context->warn("Out of frame sync slots");
    }
}

void GPU_ring_buffer_impl::wrap_write()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    ++m_write_wrap_count;
    m_write_position = 0;

    sanity_check();
}

void GPU_ring_buffer_impl::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    // Merge to existing sync
    for (Ring_buffer_sync_entry& entry : m_sync_entries) {
        if (
            (entry.waiting_for_frame == m_device.get_frame_number()) &&
            (entry.wrap_count == wrap_count)
        ) {
            if (byte_offset + byte_count > entry.byte_offset + entry.byte_count) {
                entry.byte_offset = byte_offset;
                entry.byte_count  = byte_count;
            }
            return;
        }
    }

    // Make new sync entry
    m_sync_entries.push_back(
        Ring_buffer_sync_entry{
            .waiting_for_frame = m_device.get_frame_number(),
            .wrap_count        = wrap_count,
            .byte_offset       = byte_offset,
            .byte_count        = byte_count
        }
    );
}

auto Device_impl::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Device_impl::allocate_ring_buffer_entry(Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    m_need_sync = true;
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    const Buffer_direction direction = [usage]() {
        switch (usage) {
            default:
            case Ring_buffer_usage::None      : ERHE_FATAL("Device_impl::allocate_ring_buffer_entry() - bad usage"); return Buffer_direction::cpu_to_gpu;
            case Ring_buffer_usage::CPU_write : return Buffer_direction::cpu_to_gpu;
            case Ring_buffer_usage::CPU_read  : return Buffer_direction::gpu_to_cpu;
            case Ring_buffer_usage::GPU_access: return Buffer_direction::gpu_only;
        }
    }();

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<GPU_ring_buffer>& ring_buffer : m_ring_buffers) {
        if (ring_buffer->get_direction() != direction) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<GPU_ring_buffer>& ring_buffer : m_ring_buffers) {
        if (ring_buffer->get_direction() != direction) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // No existing usable buffer found, create new buffer
    GPU_ring_buffer_create_info create_info{
        .size        = std::max(m_min_buffer_size, 4 * byte_count),
        .direction   = direction,
        .debug_label = "GPU_ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<GPU_ring_buffer>(m_device, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, usage, byte_count);
}

void Device_impl::memory_barrier(Memory_barrier_mask barriers)
{
    gl::memory_barrier(static_cast<gl::Memory_barrier_mask>(barriers)); // TODO Proper conversion
}

auto Device_impl::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    std::optional<gl::Internal_format> gl_format_opt = gl_helpers::convert_to_gl(format);
    ERHE_VERIFY(gl_format_opt.has_value());
    gl::Internal_format gl_format = gl_format_opt.value();
    auto i = format_properties.find(gl_format);
    if (i == format_properties.end()) {
        return {};
    }
    return i->second;
}

void Device_impl::clear_texture(Texture& texture, std::array<double, 4> value)
{
    const erhe::dataformat::Format      pixelformat  = texture.get_pixelformat();
    const erhe::dataformat::Format_kind format_kind  = erhe::dataformat::get_format_kind (pixelformat);
    const std::size_t                   depth_size   = erhe::dataformat::get_depth_size  (pixelformat);
    const std::size_t                   stencil_size = erhe::dataformat::get_stencil_size(pixelformat);
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
                if ((depth_size > 0) && (stencil_size == 0)) {
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
    if (format_kind != erhe::dataformat::Format_kind::format_kind_depth_stencil) {
        render_pass_descriptor.color_attachments[0].texture        = &texture;
        render_pass_descriptor.color_attachments[0].load_action    = Load_action::Clear;
        render_pass_descriptor.color_attachments[0].clear_value[0] = value[0];
        render_pass_descriptor.color_attachments[0].clear_value[1] = value[1];
        render_pass_descriptor.color_attachments[0].clear_value[2] = value[2];
        render_pass_descriptor.color_attachments[0].clear_value[3] = value[3];
    } else {
        if (depth_size > 0) {
            render_pass_descriptor.depth_attachment.texture        = &texture;
            render_pass_descriptor.depth_attachment.load_action    = Load_action::Clear;
            render_pass_descriptor.depth_attachment.clear_value[0] = value[0];
        }
        if (stencil_size > 0) {
            render_pass_descriptor.stencil_attachment.texture        = &texture;
            render_pass_descriptor.stencil_attachment.load_action    = Load_action::Clear;
            render_pass_descriptor.stencil_attachment.clear_value[0] = value[1];
        }
    }
    render_pass_descriptor.render_target_width  = texture.get_width();
    render_pass_descriptor.render_target_height = texture.get_height();
    Render_pass render_pass{m_device, render_pass_descriptor};

    Render_command_encoder clear_render_encoder = make_render_command_encoder(render_pass);
}

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder(m_device);
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder(m_device);
}

auto Device_impl::make_render_command_encoder(Render_pass& render_pass) -> Render_command_encoder
{
    return Render_command_encoder(m_device, render_pass);
}


} // namespace erhe::graphics
