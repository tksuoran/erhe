// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/instance.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/draw_indirect.hpp"
#include "erhe_gl/enum_bit_mask_operators.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/align.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window.hpp"

#if !defined(WIN32)
#   include <csignal>
#endif

#include <sstream>
#include <vector>

typedef unsigned char GLubyte;

namespace erhe::graphics {

namespace {

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

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

Instance::Instance(erhe::window::Context_window& context_window)
    : shader_monitor  {*this}
    , context_provider{*this, opengl_state_tracker}
    , m_context_window{context_window}
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
        info.vendor = Vendor::Nvidia;
    } else if (gl_vendor_lc.find("amd") != std::string::npos) {
        info.vendor = Vendor::Amd;
    } else if (gl_vendor_lc.find("intel") != std::string::npos) {
        info.vendor = Vendor::Intel;
    } else {
        info.vendor = Vendor::Unknown;
    }

    auto versions = split(gl_version_str, '.');

    int major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
    int minor = versions.size() > 1 ? to_int(digits_only(versions[1])) : 0;

    info.gl_version = (major * 100) + (minor * 10);

    gl::get_integer_v(gl::Get_p_name::max_vertex_attribs, &limits.max_vertex_attribs);
    log_startup->info("max vertex attribs: {}", limits.max_vertex_attribs);

    log_startup->info("GL Extensions:");
    {
        ERHE_PROFILE_SCOPE("Extensions");

        int num_extensions{0};

        gl::get_integer_v(gl::Get_p_name::num_extensions, &num_extensions);
        if (num_extensions > 0) {
            extensions.reserve(num_extensions);
            for (unsigned int i = 0; i < static_cast<unsigned int>(num_extensions); ++i) {
                const auto* extension_str = gl::get_string_i(gl::String_name::extensions, i);
                auto e = std::string(reinterpret_cast<const char*>(extension_str));
                log_startup->info("    {}", e);
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
        info.glsl_version = (major * 100) + minor;
    }

    log_startup->info("glVersion:   {}", info.gl_version);
    log_startup->info("glslVersion: {}", info.glsl_version);

    {
        ERHE_PROFILE_SCOPE("Query GL");

        {
            ERHE_PROFILE_SCOPE("gl::command_info_init");
            gl::command_info_init(info.glsl_version, extensions);
        }

        gl::get_float_v  (gl::Get_p_name::max_texture_max_anisotropy, &limits.max_texture_max_anisotropy);
        gl::get_integer_v(gl::Get_p_name::max_samples,                &limits.max_samples);
        gl::get_integer_v(gl::Get_p_name::max_color_texture_samples,  &limits.max_color_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_depth_texture_samples,  &limits.max_depth_texture_samples);
        gl::get_integer_v(gl::Get_p_name::max_framebuffer_samples,    &limits.max_framebuffer_samples);
        gl::get_integer_v(gl::Get_p_name::max_integer_samples,        &limits.max_integer_samples);

        log_startup->info(
            "max samples = {}, max color texture samples = {}, max depth texture samples = {}, "
            "max framebuffer samples = {}, max integer samples = {}",
            limits.max_samples,
            limits.max_color_texture_samples,
            limits.max_depth_texture_samples,
            limits.max_framebuffer_samples,
            limits.max_integer_samples
        );

        gl::get_integer_v(gl::Get_p_name::max_texture_size,          &limits.max_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_3d_texture_size,       &limits.max_3d_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_cube_map_texture_size, &limits.max_cube_map_texture_size);
        gl::get_integer_v(gl::Get_p_name::max_array_texture_layers,  &limits.max_array_texture_layers);

        log_startup->info("max texture size:          {}", limits.max_texture_size);
        log_startup->info("max 3d texture size:       {}", limits.max_3d_texture_size);
        log_startup->info("max cube map texture size: {}", limits.max_cube_map_texture_size);
        log_startup->info("max array texture layers:  {}", limits.max_array_texture_layers);

        gl::get_integer_v(gl::Get_p_name::max_texture_image_units,          &limits.max_texture_image_units);
        gl::get_integer_v(gl::Get_p_name::max_combined_texture_image_units, &limits.max_combined_texture_image_units);

        // GL 3.0 introduced context flags
        int context_flags = 0;
        gl::get_integer_v(gl::Get_p_name::context_flags, &context_flags);
        if ((static_cast<unsigned int>(context_flags) & static_cast<unsigned int>(GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT)) != 0) {
            info.forward_compatible = true;
            log_startup->info("forward compatible");
        }

        // GL 3.3 introduced context profile mask
        int context_profile_mask = 0;
        gl::get_integer_v(gl::Get_p_name::context_profile_mask, &context_profile_mask);
        if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_CORE_PROFILE_BIT)) != 0) {
            info.core_profile = true;
            log_startup->info("core profile");
        }
        if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)) != 0) {
            info.compatibility_profile = true;
            log_startup->info("compatibility profile");
        }

        gl::get_integer_v(gl::Get_p_name::max_texture_buffer_size, &limits.max_texture_buffer_size);
    }

    int shader_storage_buffer_offset_alignment{0};
    int uniform_buffer_offset_alignment       {0};

    if (info.gl_version >= 430) {
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
            gl::get_integer_iv(gl::Get_p_name::max_compute_work_group_count, i, &limits.max_compute_workgroup_count[i]);
            gl::get_integer_iv(gl::Get_p_name::max_compute_work_group_size,  i, &limits.max_compute_workgroup_size[i]);
        }
        gl::get_integer_v(gl::Get_p_name::max_compute_work_group_invocations, &limits.max_compute_work_group_invocations);
        gl::get_integer_v(gl::Get_p_name::max_compute_shared_memory_size,     &limits.max_compute_shared_memory_size);
        log_startup->info(
            "Max compute workgroup count = {} x {} x {}",
            limits.max_compute_workgroup_count[0],
            limits.max_compute_workgroup_count[1],
            limits.max_compute_workgroup_count[2]
        );
        log_startup->info(
            "Max compute workgroup size = {} x {} x {}",
            limits.max_compute_workgroup_size[0],
            limits.max_compute_workgroup_size[1],
            limits.max_compute_workgroup_size[2]
        );
        log_startup->info(
            "Max compute workgroup invocations = {}",
            limits.max_compute_work_group_invocations
        );
        log_startup->info(
            "Max compute shared memory size = {}",
            limits.max_compute_shared_memory_size
        );

        gl::get_integer_v(gl::Get_p_name::shader_storage_buffer_offset_alignment,    &shader_storage_buffer_offset_alignment);
        gl::get_integer_v(gl::Get_p_name::max_shader_storage_buffer_bindings,        &limits.max_shader_storage_buffer_bindings);
        gl::get_integer_v(gl::Get_p_name::max_compute_shader_storage_blocks,         &limits.max_compute_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_compute_uniform_blocks,                &limits.max_compute_uniform_blocks);
        gl::get_integer_v(gl::Get_p_name::max_vertex_shader_storage_blocks,          &limits.max_vertex_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_fragment_shader_storage_blocks,        &limits.max_fragment_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_geometry_shader_storage_blocks,        &limits.max_geometry_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_control_shader_storage_blocks,    &limits.max_tess_control_shader_storage_blocks);
        gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_shader_storage_blocks, &limits.max_tess_evaluation_shader_storage_blocks);
        info.use_compute_shader = true;
    } else {
        info.use_compute_shader = false;
        for (GLuint i = 0; i < 3; ++i) {
            limits.max_compute_workgroup_count[i] = 0;
            limits.max_compute_workgroup_size [i] = 0;
        }
        limits.max_compute_work_group_invocations        = 0;
        limits.max_compute_shared_memory_size            = 0;
        limits.max_shader_storage_buffer_bindings        = 0;
        limits.max_compute_shader_storage_blocks         = 0;
        limits.max_compute_uniform_blocks                = 0;
        limits.max_vertex_shader_storage_blocks          = 0;
        limits.max_fragment_shader_storage_blocks        = 0;
        limits.max_geometry_shader_storage_blocks        = 0;
        limits.max_tess_control_shader_storage_blocks    = 0;
        limits.max_tess_evaluation_shader_storage_blocks = 0;
    }

    gl::get_integer_v(gl::Get_p_name::uniform_buffer_offset_alignment,           &uniform_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::max_uniform_block_size,                    &limits.max_uniform_block_size);
    gl::get_integer_v(gl::Get_p_name::max_uniform_buffer_bindings,               &limits.max_uniform_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_blocks,                 &limits.max_vertex_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_vectors,                &limits.max_vertex_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_blocks,               &limits.max_fragment_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_vectors,              &limits.max_fragment_uniform_vectors);
    gl::get_integer_v(gl::Get_p_name::max_geometry_uniform_blocks,               &limits.max_geometry_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_control_uniform_blocks,           &limits.max_tess_control_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_uniform_blocks,        &limits.max_tess_evaluation_uniform_blocks);

    implementation_defined.shader_storage_buffer_offset_alignment = static_cast<unsigned int>(shader_storage_buffer_offset_alignment);
    implementation_defined.uniform_buffer_offset_alignment        = static_cast<unsigned int>(uniform_buffer_offset_alignment);
    log_startup->info(
        "uniform block ("
        "max size = {}, "
        "offset alignment = {}. "
        "max bindings = {}, "
        "max compute blocks = {}, "
        "max vertex blocks = {}, "
        "max fragment blocks = {}"
        ")",
        limits.max_uniform_block_size,
        implementation_defined.uniform_buffer_offset_alignment,
        limits.max_uniform_buffer_bindings,
        limits.max_compute_uniform_blocks,
        limits.max_vertex_uniform_blocks,
        limits.max_fragment_uniform_blocks
    );
    log_startup->info(
        "shader storage block ("
        "offset alignment = {}"
        ", max bindings = {}"
        ", max compute blocks = {}"
        ", max vertex blocks = {}"
        ", max fragment blocks = {}"
        ")",
        implementation_defined.shader_storage_buffer_offset_alignment,
        limits.max_shader_storage_buffer_bindings,
        limits.max_compute_shader_storage_blocks,
        limits.max_vertex_shader_storage_blocks,
        limits.max_fragment_shader_storage_blocks
    );

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_bindless_texture)) {
        info.use_bindless_texture = true;
    }
    if (info.vendor == Vendor::Intel) {
        info.use_bindless_texture = false;
    }
    log_startup->info("GL_ARB_bindless_texture supported : {}", info.use_bindless_texture);

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_sparse_texture)) {
        ERHE_PROFILE_SCOPE("Sparse texture");

        info.use_sparse_texture = true;
        gl::get_integer_v(gl::Get_p_name::max_sparse_texture_size_arb, &limits.max_sparse_texture_size);
        log_startup->info("max sparse texture size : {}", limits.max_sparse_texture_size);
    }
    log_startup->info("GL_ARB_sparse_texture supported : {}", info.use_sparse_texture);

    info.use_persistent_buffers = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
    info.use_direct_state_access = (info.gl_version >= 450) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_direct_state_access);
    info.use_multi_draw_indirect = (info.gl_version >= 430) || gl::is_extension_supported(gl::Extension::Extension_GL_ARB_multi_draw_indirect);
    log_startup->info("Persistent Buffers supported:  {}", info.use_sparse_texture);
    log_startup->info("Direct State Access supported: {}", info.use_direct_state_access);
    log_startup->info("Multi Draw Indirect supported: {}", info.use_multi_draw_indirect);

    bool force_no_bindless           {false};
    bool force_no_persistent_buffers {false};
    bool force_no_direct_state_access{false};
    bool force_no_multi_draw_indirect{false};
    int  force_gl_version            {false};
    int  force_glsl_version          {false};
    bool capture_support             {false};
    bool initial_clear               {false};
    {
        const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "graphics");
        ini.get("reverse_depth",                configuration.reverse_depth  );
        ini.get("post_processing",              configuration.post_processing);
        ini.get("use_time_query",               configuration.use_time_query );
        ini.get("force_no_bindless",            force_no_bindless);
        ini.get("force_no_persistent_buffers",  force_no_persistent_buffers);
        ini.get("force_no_direct_state_access", force_no_direct_state_access);
        ini.get("force_no_multi_draw_indirect", force_no_multi_draw_indirect);
        ini.get("force_gl_version",             force_gl_version);
        ini.get("force_glsl_version",           force_glsl_version);
        ini.get("initial_clear",                initial_clear);
    }
    if (force_gl_version > 0) {
        info.gl_version = force_gl_version;
        log_startup->warn("Forced GL version to be {} due to erhe.ini setting", force_gl_version);
    }
    if (force_glsl_version > 0) {
        info.glsl_version = force_glsl_version;
        log_startup->warn("Forced GLSL version to be {} due to erhe.ini setting", force_glsl_version);
    }

    if (info.use_bindless_texture) {
#if defined(ERHE_SPIRV)
        // 'GL_ARB_bindless_texture' : not allowed when using generating SPIR-V codes
        info.use_bindless_texture = false;
        log_startup->warn("Force disabled GL_ARB_bindless_texture due to ERHE_SPIRV cmake setting");
#else
        if (force_no_bindless || capture_support) {
            info.use_bindless_texture = false;
            log_startup->warn("Force disabled GL_ARB_bindless_texture due to erhe.ini setting");
        }
#endif
    }

    if (!info.use_direct_state_access) { 
        ERHE_FATAL("OpenGL driver does not support irect state access, required by erhe. This is a fatal error.");
    }
    // if (info.use_direct_state_access) { 
    //     if (force_no_direct_state_access) {
    //         info.use_direct_state_access = false;
    //         log_startup->warn("Force disabled direct state access due to erhe.ini setting");
    //     }
    // }

    if (info.use_multi_draw_indirect) { 
        if (force_no_multi_draw_indirect) {
            info.use_multi_draw_indirect = false;
            log_startup->warn("Force disabled multi draw indirect due to erhe.ini setting");
        }
    }

    if (force_no_persistent_buffers) {
        if (info.use_persistent_buffers) {
            info.use_persistent_buffers = false;
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
        const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderdoc");
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
        gl::Internal_format::depth_component32f,
        gl::Internal_format::depth_component,
        gl::Internal_format::depth_component16
    };

    log_startup->info("Format properties:");
    for (const gl::Internal_format format : formats) {
        Format_properties properties{};

        GLint supported{};
        gl::get_internalformat_iv(gl::Texture_target::texture_2d, format, gl::Internal_format_p_name::internalformat_supported, 1, &supported);
        properties.supported = (supported == GL_TRUE);
        if (!properties.supported) {
            continue;
        }

        log_startup->info("    {}", gl::c_str(format));

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

        int num_virtual_page_sizes = get_int(gl::Internal_format_p_name::num_virtual_page_sizes_arb);
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
            std::stringstream ss;
            for (GLint i = 0; i < num_virtual_page_sizes; ++i) {
                ss << fmt::format(
                    " {} x {} x {}",
                    properties.sparse_tile_x_sizes[i],
                    properties.sparse_tile_y_sizes[i],
                    properties.sparse_tile_z_sizes[i]
                );
            }
            log_startup->info("    {} : num page sizes {} :{}", gl::c_str(format), num_virtual_page_sizes, ss.str());
        }

        {
            int num_sample_counts = get_int(gl::Internal_format_p_name::num_sample_counts, gl::Texture_target::texture_2d_multisample);
            std::stringstream ss;
            ss << fmt::format("    {} sample counts:", c_str(format));
            if (num_sample_counts > 0) {
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
            log_startup->info(ss.str());
        }

        properties.texture_2d_array_max_width  = get_int(gl::Internal_format_p_name::max_width, gl::Texture_target::texture_2d_array);
        properties.texture_2d_array_max_height = get_int(gl::Internal_format_p_name::max_height, gl::Texture_target::texture_2d_array);
        properties.texture_2d_array_max_layers = get_int(gl::Internal_format_p_name::max_layers, gl::Texture_target::texture_2d_array);
        format_properties.insert({format, properties});
    }

    {
        ERHE_PROFILE_SCOPE("Start shader monitor");
        shader_monitor.begin();
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
}

auto Instance::depth_clear_value_pointer() const -> const float*
{
    return configuration.reverse_depth ? &float_zero_value : &float_one_value;
}

auto Instance::depth_function(const gl::Depth_function depth_function) const -> gl::Depth_function
{
    return configuration.reverse_depth ? reverse(depth_function) : depth_function;
}

auto Instance::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    if (info.use_bindless_texture) {
        return gl::get_texture_sampler_handle_arb(texture.gl_name(), sampler.gl_name());
    } else {
        const uint64_t texture_name  = static_cast<uint64_t>(texture.gl_name());
        const uint64_t sampler_name  = static_cast<uint64_t>(sampler.gl_name());
        const uint64_t handle        = texture_name | (sampler_name << 32);
        return handle;
    }
}

auto Instance::choose_depth_stencil_format(const unsigned int flags, int sample_count) const -> gl::Internal_format
{
    using namespace erhe::bit;
    const bool require_depth     = test_all_rhs_bits_set(flags, format_flag_require_depth    );
    const bool require_stencil   = test_all_rhs_bits_set(flags, format_flag_require_stencil  );
    const bool prefer_accuracy   = test_all_rhs_bits_set(flags, format_flag_prefer_accuracy  );
    const bool prefer_filterable = test_all_rhs_bits_set(flags, format_flag_prefer_filterable);
    gl::Internal_format formats[] = {
        gl::Internal_format::depth32f_stencil8,
        gl::Internal_format::depth24_stencil8,
        gl::Internal_format::depth_stencil,
        gl::Internal_format::stencil_index8,
        gl::Internal_format::depth_component32f,
        gl::Internal_format::depth_component,
        gl::Internal_format::depth_component16
    };

    gl::Internal_format best_format = gl::Internal_format{0};
    float best_score = 0.0f;
    for (const auto format : formats) {
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

auto Instance::create_dummy_texture() -> std::shared_ptr<Texture>
{
    const erhe::graphics::Texture::Create_info create_info{
        .instance    = *this,
        .width       = 2,
        .height      = 2,
        .debug_label = "dummy"
    };

    auto texture = std::make_shared<Texture>(*this, create_info);
    texture->set_debug_label("dummy");
    const std::array<uint8_t, 16> dummy_pixel{
        0xee, 0x11, 0xdd, 0xff,
        0xcc, 0x11, 0xbb, 0xff,
        0xee, 0x11, 0xdd, 0xff,
        0xcc, 0x11, 0xbb, 0xff
    };
    const std::span<const std::uint8_t> image_data{&dummy_pixel[0], dummy_pixel.size()};

    texture->upload(create_info.internal_format, image_data, create_info.width, create_info.height);

    return texture;
}


void Instance::texture_unit_cache_reset(const unsigned int base_texture_unit)
{
    SPDLOG_LOGGER_TRACE(log_texture_frame, "texture_unit_cache_reset(base_texture_unit = {})", base_texture_unit);

    m_base_texture_unit = base_texture_unit;
    m_texture_units.clear();
}

auto Instance::texture_unit_cache_allocate(const uint64_t handle) -> std::optional<std::size_t>
{
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    const GLuint texture_name = erhe::graphics::get_texture_from_handle(handle);
    const GLuint sampler_name = erhe::graphics::get_sampler_from_handle(handle);
#endif

    for (std::size_t texture_unit = 0, end = m_texture_units.size(); texture_unit < end; ++texture_unit) {
        if (m_texture_units[texture_unit] == handle) {
            SPDLOG_LOGGER_TRACE(log_texture_frame, "cache hit texture unit {} for texture {}, sampler {}", texture_unit, texture_name, sampler_name);
            return texture_unit;
        }
    }

    const std::size_t result = m_texture_units.size();
    m_texture_units.push_back(handle);
    SPDLOG_LOGGER_TRACE(log_texture_frame, "allocted texture unit {} for texture {}, sampler {}", result, texture_name, sampler_name);
    return result;
}

auto Instance::texture_unit_cache_bind(const uint64_t fallback_handle) -> std::size_t
{
    const GLuint fallback_texture_name = erhe::graphics::get_texture_from_handle(fallback_handle);
    const GLuint fallback_sampler_name = erhe::graphics::get_sampler_from_handle(fallback_handle);

    GLuint i{};
    GLuint end = std::min(
        static_cast<GLuint>(m_texture_units.size()),
        static_cast<GLuint>(limits.max_texture_image_units - m_base_texture_unit)
    );

    for (i = 0; i < end; ++i) {
        const uint64_t handle       = m_texture_units[i];
        const GLuint   texture_name = erhe::graphics::get_texture_from_handle(handle);
        const GLuint   sampler_name = erhe::graphics::get_sampler_from_handle(handle);

        if (handle != 0) {
#if !defined(NDEBUG)
            if (gl::is_texture(texture_name) == GL_TRUE) {
                gl::bind_texture_unit(m_base_texture_unit + i, texture_name);
                SPDLOG_LOGGER_TRACE(log_texture_frame, "texture unit {} + {} = {}: bound texture {}", m_base_texture_unit, i, m_base_texture_unit + i, texture_name);
            } else {
                log_texture_frame->warn("texture unit {} + {} = {}: {} is not a texture", m_base_texture_unit, i, m_base_texture_unit + i, texture_name);
                gl::bind_texture_unit(m_base_texture_unit + i, erhe::graphics::get_texture_from_handle(fallback_handle));
            }

            if ((sampler_name == 0) || (gl::is_sampler(sampler_name) == GL_TRUE)) {
                gl::bind_sampler(m_base_texture_unit + i, sampler_name);
                SPDLOG_LOGGER_TRACE(log_texture_frame, "texture unit {} + {} = {}: bound sampler {}", m_base_texture_unit, i, m_base_texture_unit + i, sampler_name);
            } else {
                gl::bind_sampler(m_base_texture_unit + i, erhe::graphics::get_sampler_from_handle(fallback_handle));
                log_texture_frame->warn("texture unit {} + {} = {}: {} is not a sampler", m_base_texture_unit, i, m_base_texture_unit + i, sampler_name);
            }
#else
            gl::bind_texture_unit(m_base_texture_unit + i, texture_name);
            gl::bind_sampler     (m_base_texture_unit + i, sampler_name);
#endif
        } else {
            gl::bind_texture_unit(m_base_texture_unit + i, fallback_texture_name);
            gl::bind_sampler     (m_base_texture_unit + i, fallback_sampler_name);
        }
    }
    return m_texture_units.size();
}

auto Instance::get_buffer_alignment(gl::Buffer_target target) -> std::size_t
{
    switch (target) {
        //using enum gl::Buffer_target;
        case gl::Buffer_target::array_buffer: 
        case gl::Buffer_target::element_array_buffer: {
            return 64; // TODO Good alignment?
        }

        case gl::Buffer_target::shader_storage_buffer: {
            return implementation_defined.shader_storage_buffer_offset_alignment;
        }

        case gl::Buffer_target::uniform_buffer: {
            return implementation_defined.uniform_buffer_offset_alignment;
        }

        case gl::Buffer_target::draw_indirect_buffer: {
            return sizeof(gl::Draw_elements_indirect_command);
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
inline auto storage_mask(erhe::graphics::Instance& instance) -> gl::Buffer_storage_mask
{
    return instance.info.use_persistent_buffers
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

inline auto access_mask(erhe::graphics::Instance& instance) -> gl::Map_buffer_access_mask
{
    return instance.info.use_persistent_buffers
        ? access_mask_persistent
        : access_mask_not_persistent;
}



// Ring buffer

Buffer_range::Buffer_range()
    : m_ring_buffer                     {nullptr}
    , m_span                            {}
    , m_wrap_count                      {0}
    , m_byte_span_start_offset_in_buffer{0}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {Ring_buffer_usage::None}
{
    SPDLOG_LOGGER_TRACE(log_gpu_ring_buffer, "Buffer_range::Buffer_range()");
}

Buffer_range::Buffer_range(
    GPU_ring_buffer&     ring_buffer,
    Ring_buffer_usage    usage,
    std::span<std::byte> span,
    std::size_t          wrap_count,
    size_t               byte_start_offset_in_buffer
)
    : m_ring_buffer                     {&ring_buffer}
    , m_span                            {span}
    , m_wrap_count                      {wrap_count}
    , m_byte_span_start_offset_in_buffer{byte_start_offset_in_buffer}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {usage}
{
    ERHE_VERIFY(usage != Ring_buffer_usage::None);
    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "Buffer_range::Buffer_range() buffer = {} start offset in buffer = {} span byte count = {}",
        ring_buffer.get_buffer().debug_label,
        byte_start_offset,
        span.size_bytes()
    );
}

Buffer_range::Buffer_range(Buffer_range&& old) noexcept
    : m_ring_buffer                     {std::exchange(old.m_ring_buffer,                      nullptr)}
    , m_span                            {std::exchange(old.m_span,                             {}     )}
    , m_wrap_count                      {std::exchange(old.m_wrap_count,                       0      )}
    , m_byte_span_start_offset_in_buffer{std::exchange(old.m_byte_span_start_offset_in_buffer, 0      )}
    , m_byte_write_position_in_span     {std::exchange(old.m_byte_write_position_in_span,      0      )}
    , m_byte_flush_position_in_span     {std::exchange(old.m_byte_flush_position_in_span,      0      )}
    , m_usage                           {std::exchange(old.m_usage,                            Ring_buffer_usage::None)}
    , m_is_closed                       {std::exchange(old.m_is_closed,                        true   )}
    , m_is_released                     {std::exchange(old.m_is_released ,                     false  )}
    , m_is_cancelled                    {std::exchange(old.m_is_cancelled,                     true   )}
{
}

Buffer_range& Buffer_range::operator=(Buffer_range&& old) noexcept
{
    m_ring_buffer                      = std::exchange(old.m_ring_buffer,                      nullptr);
    m_span                             = std::exchange(old.m_span,                             {}     );
    m_wrap_count                       = std::exchange(old.m_wrap_count,                       0      );
    m_byte_span_start_offset_in_buffer = std::exchange(old.m_byte_span_start_offset_in_buffer, 0      );
    m_byte_write_position_in_span      = std::exchange(old.m_byte_write_position_in_span,      0      );
    m_byte_flush_position_in_span      = std::exchange(old.m_byte_flush_position_in_span,      0      );
    m_usage                            = std::exchange(old.m_usage,                            Ring_buffer_usage::None);
    m_is_closed                        = std::exchange(old.m_is_closed,                        true   );
    m_is_released                      = std::exchange(old.m_is_released,                      false  );
    m_is_cancelled                     = std::exchange(old.m_is_cancelled,                     true   );
    return *this;
}

Buffer_range::~Buffer_range()
{
    ERHE_VERIFY((m_ring_buffer == nullptr) || m_is_released || m_is_cancelled);
}

void Buffer_range::close()
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_ring_buffer->close(m_byte_span_start_offset_in_buffer, m_byte_write_position_in_span);
    m_is_closed = true;
}

void Buffer_range::bytes_written(std::size_t byte_count)
{
    //ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_byte_write_position_in_span + byte_count <= m_span.size_bytes());
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_byte_write_position_in_span += byte_count;
}

void Buffer_range::flush(std::size_t byte_write_position_in_span)
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_write_position_in_span);
    m_byte_write_position_in_span = byte_write_position_in_span;
    ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    const size_t flush_byte_count = byte_write_position_in_span - m_byte_flush_position_in_span;
    if (flush_byte_count > 0) {
        m_ring_buffer->flush(m_byte_flush_position_in_span, flush_byte_count);
    }
    m_byte_flush_position_in_span = byte_write_position_in_span;
}

void Buffer_range::release()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_is_released);
    m_is_released = true;

    if (m_ring_buffer == nullptr) {
        ERHE_VERIFY(m_byte_write_position_in_span == 0);
        return;
    }
    ERHE_VERIFY(is_closed());
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    m_ring_buffer->make_sync_entry(
        m_wrap_count,
        m_byte_span_start_offset_in_buffer,
        m_byte_write_position_in_span
    );
}

void Buffer_range::cancel()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(!m_is_cancelled);
    m_is_cancelled = true;
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    ERHE_VERIFY(m_ring_buffer != nullptr);
}

auto Buffer_range::get_span() const -> std::span<std::byte>
{
    return m_span;
}

auto Buffer_range::get_byte_start_offset_in_buffer() const -> std::size_t
{
    return m_byte_span_start_offset_in_buffer;
}

auto Buffer_range::get_writable_byte_count() const -> std::size_t
{
    ERHE_VERIFY(m_span.size_bytes() >= m_byte_write_position_in_span);
    return m_span.size_bytes() - m_byte_write_position_in_span;
}

auto Buffer_range::get_written_byte_count() const -> std::size_t
{
    return m_byte_write_position_in_span;
}

auto Buffer_range::is_closed() const -> bool
{
    return m_is_closed;
}

auto Buffer_range::get_buffer() const -> GPU_ring_buffer*
{
    return m_ring_buffer;
}

//
//

GPU_ring_buffer::GPU_ring_buffer(
    erhe::graphics::Instance&          graphics_instance,
    const GPU_ring_buffer_create_info& create_info
)
    : m_instance     {graphics_instance}
    , m_buffer{
        std::make_unique<Buffer>(
            m_instance,
            Buffer_create_info{
                .target              = gl::Buffer_target::copy_write_buffer, // generic buffer target
                .capacity_byte_count = create_info.size,
                .storage_mask        = storage_mask(m_instance),
                .access_mask         = access_mask(m_instance),
                .debug_label         = create_info.debug_label
            }
        )
    }
    , m_read_wrap_count{0}
    , m_read_offset    {m_buffer->capacity_byte_count()}
{
}

auto GPU_ring_buffer::get_buffer() -> Buffer*
{
    return m_buffer.get();
}

auto GPU_ring_buffer::get_name() const -> const std::string&
{
    return m_name;
}

void GPU_ring_buffer::sanity_check()
{
    ERHE_VERIFY(m_write_position <= m_buffer->capacity_byte_count());

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
    } else {
        ERHE_FATAL("buffer issue");
    }
}

void GPU_ring_buffer::get_size_available_for_write(
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
    //// gl::Buffer_target target = m_buffer.target();
    //// std::size_t required_alignment = m_instance.get_buffer_alignment(target);
    const std::size_t aligned_write_position = align_offset(m_write_position, required_alignment);
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
        out_available_byte_count_without_wrap = m_buffer->capacity_byte_count() - m_write_position;
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

auto GPU_ring_buffer::acquire(std::size_t required_alignment, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
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

    if (!m_instance.info.use_persistent_buffers) {
        if (usage == Ring_buffer_usage::CPU_write) {
            m_buffer->begin_write(m_write_position, byte_count); // maps requested range
        }
        Buffer_range result{*this, usage, m_buffer->map(), m_write_wrap_count, m_write_position};
        m_write_position += byte_count;
        sanity_check();
        return result;
    } else {
        size_t buffer_mapped_span_byte_count = m_buffer->map().size_bytes();
        ERHE_VERIFY(m_write_position + byte_count <= buffer_mapped_span_byte_count);
        Buffer_range result{*this, usage, m_buffer->map().subspan(m_write_position, byte_count), m_write_wrap_count, m_write_position};
        m_write_position += byte_count;
        sanity_check();
        return result;
    }
}

void GPU_ring_buffer::flush(std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (!m_instance.info.use_persistent_buffers) {
        m_buffer->flush_bytes(byte_offset, byte_count);
    }
}

void GPU_ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (!m_instance.info.use_persistent_buffers) {
        m_buffer->end_write(byte_offset, byte_write_count); // flush and unmap
    }
    //m_map = {};
}

GPU_ring_buffer::~GPU_ring_buffer()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();
}

void GPU_ring_buffer::frame_completed(uint64_t completed_frame)
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

void Instance::frame_completed(uint64_t frame)
{
    for (const std::unique_ptr<GPU_ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(frame);
    }
}

void Instance::end_of_frame()
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
    for (Frame_sync& frame_sync : m_frame_syncs) {
        if (frame_sync.fence_sync == nullptr) {
            frame_sync.frame_number = m_frame_number,
            frame_sync.fence_sync   = gl::fence_sync(gl::Sync_condition::sync_gpu_commands_complete, 0),
            frame_sync.result       = gl::Sync_status::timeout_expired;
            m_pending_frames.push_back(m_frame_number);
            ++m_frame_number;
            return;
        }
    }
    ERHE_FATAL("Not enough frame sync slots");
}

void GPU_ring_buffer::wrap_write()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    ++m_write_wrap_count;
    m_write_position = 0;

    sanity_check();

}

void GPU_ring_buffer::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    // Merge to existing sync
    for (Ring_buffer_sync_entry& entry : m_sync_entries) {
        if (
            (entry.waiting_for_frame == m_instance.get_frame_number()) &&
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
            .waiting_for_frame = m_instance.get_frame_number(),
            .wrap_count        = wrap_count,
            .byte_offset       = byte_offset,
            .byte_count        = byte_count
        }
    );
}

GPU_ring_buffer_client::GPU_ring_buffer_client(Instance& graphics_instance, std::string_view debug_label, gl::Buffer_target buffer_target, std::optional<unsigned int> binding_point)
    : m_graphics_instance{graphics_instance}
    , m_buffer_target    {buffer_target}
    , m_debug_label      {debug_label}
    , m_binding_point    {binding_point}
{
}

auto GPU_ring_buffer_client::acquire(Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    ERHE_VERIFY(byte_count > 0);
    return m_graphics_instance.allocate_ring_buffer_entry(m_buffer_target, usage, byte_count);
}

auto GPU_ring_buffer_client::bind(const Buffer_range& range) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // sanity_check();

    const size_t offset     = range.get_byte_start_offset_in_buffer();
    const size_t byte_count = range.get_written_byte_count();
    if (byte_count == 0) {
        return false;
    }

    GPU_ring_buffer* ring_buffer = range.get_buffer();
    ERHE_VERIFY(ring_buffer != nullptr);
    erhe::graphics::Buffer* buffer = ring_buffer->get_buffer();
    ERHE_VERIFY(buffer != nullptr);

    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "binding {} {} {} buffer offset = {} byte count = {}",
        m_name,
        gl::c_str(m_buffer_target),
        m_binding_point.has_value() ? "uses binding point" : "non-indexed binding",
        m_binding_point.has_value() ? m_binding_point.value() : 0,
        range.first_byte_offset,
        range.byte_count
    );

    ERHE_VERIFY(
        (m_buffer_target != gl::Buffer_target::uniform_buffer) ||
        (byte_count <= static_cast<std::size_t>(m_graphics_instance.limits.max_uniform_block_size))
    );
    ERHE_VERIFY(offset + byte_count <= buffer->capacity_byte_count());

    if (m_binding_point.has_value()) {
        ERHE_VERIFY(gl_helpers::is_indexed(m_buffer_target));
        gl::bind_buffer_range(
            m_buffer_target,
            m_binding_point.value(),
            static_cast<GLuint>    (buffer->gl_name()),
            static_cast<GLintptr>  (offset),
            static_cast<GLsizeiptr>(byte_count)
        );
    } else {
        ERHE_VERIFY(!gl_helpers::is_indexed(m_buffer_target));
        gl::bind_buffer(m_buffer_target, static_cast<GLuint>(buffer->gl_name()));
    }
    return true;
}

auto Instance::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Instance::allocate_ring_buffer_entry(gl::Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Buffer_range
{
    std::size_t required_alignment = get_buffer_alignment(buffer_target);
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<GPU_ring_buffer>& ring_buffer : m_ring_buffers) {
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
        .debug_label = "GPU_ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<GPU_ring_buffer>(*this, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, usage, byte_count);
}

void Instance::named_renderbuffer_storage_multisample(GLuint renderbuffer, GLsizei samples, gl::Internal_format internalformat, GLsizei width, GLsizei height)
{
    if (info.use_direct_state_access) {
        gl::named_renderbuffer_storage_multisample(renderbuffer, samples, internalformat, width, height);
    } else {
        int current_renderbuffer = 0;
        gl::get_integer_v(gl::Get_p_name::renderbuffer_binding, &current_renderbuffer);
        gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, renderbuffer);
        gl::renderbuffer_storage_multisample(gl::Renderbuffer_target::renderbuffer, samples, internalformat, width, height);
        gl::bind_renderbuffer(gl::Renderbuffer_target::renderbuffer, current_renderbuffer);
    }
}

void Instance::multi_draw_elements_indirect(
    gl::Primitive_type     mode,
    gl::Draw_elements_type type,
    const void*            indirect,
    GLsizei                drawcount,
    GLsizei                stride
)
{
    gl::multi_draw_elements_indirect(mode, type, indirect, drawcount, stride);
}

auto Instance::get_format_properties(gl::Internal_format format) const -> Format_properties
{
    auto i = format_properties.find(format);
    if (i == format_properties.end()) {
        return {};
    }
    return i->second;
}

} // namespace erhe::graphics
