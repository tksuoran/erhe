#include "erhe/graphics/instance.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/graphics_log.hpp"

#if !defined(WIN32)
# include <csignal>
#endif

#include <sstream>
#include <vector>

#define LOG_CATEGORY log_configuration

typedef unsigned char GLubyte;

namespace erhe::graphics
{

Instance::Info                                     Instance::info;
Instance::Limits                                   Instance::limits;
Instance::Implementation_defined                   Instance::implementation_defined;
std::unordered_map<gl::Internal_format, Tile_size> Instance::sparse_tile_sizes;

//std::vector<gl::Extension>       Instance::extensions;

namespace
{

auto to_int(const std::string& text) -> int
{
    return stoi(text);
}

} // namespace

auto split(
    const std::string& text,
    const char         separator
) -> std::vector<std::string>
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

void Instance::initialize()
{
    std::vector<std::string> extensions;
    const auto gl_vendor      = (get_string)(gl::String_name::vendor);
    const auto gl_renderer    = (get_string)(gl::String_name::renderer);
    const auto gl_version_str = (get_string)(gl::String_name::version);

    log_configuration->info("GL Vendor:     {}", gl_vendor);
    log_configuration->info("GL Renderer:   {}", gl_renderer);
    log_configuration->info("GL Version:    {}", gl_version_str.c_str());

    auto versions = split(gl_version_str, '.');

    int major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
    int minor = versions.size() > 1 ? to_int(digits_only(versions[1])) : 0;

    info.gl_version = (major * 100) + (minor * 10);

    gl::get_integer_v(gl::Get_p_name::max_texture_size, &limits.max_texture_size);
    log_configuration->info("max texture size: {}", limits.max_texture_size);

    gl::get_integer_v(gl::Get_p_name::max_vertex_attribs, &limits.max_vertex_attribs);
    log_configuration->info("max vertex attribs: {}", limits.max_vertex_attribs);

    log_configuration->trace("GL Extensions:");
    {
        int num_extensions{0};

        gl::get_integer_v(gl::Get_p_name::num_extensions, &num_extensions);
        if (num_extensions > 0) {
            extensions.reserve(num_extensions);
            for (unsigned int i = 0; i < static_cast<unsigned int>(num_extensions); ++i) {
                const auto* extension_str = gl::get_string_i(gl::String_name::extensions, i);
                auto e = std::string(reinterpret_cast<const char*>(extension_str));
                extensions.push_back(e);
            }
        }
    }

    {
        auto shading_language_version = (get_string)(gl::String_name::shading_language_version);
        log_configuration->info("GLSL Version:  {}", shading_language_version);
        versions = split(shading_language_version, '.');

        major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
        minor = (versions.size() > 1) ? to_int(digits_only(versions[1])) : 0;
        info.glsl_version = (major * 100) + minor;
    }

    log_configuration->info("glVersion:   {}", info.gl_version);
    log_configuration->info("glslVersion: {}", info.glsl_version);

    gl::command_info_init(info.glsl_version, extensions);

    gl::get_float_v  (gl::Get_p_name::max_texture_max_anisotropy, &limits.max_texture_max_anisotropy);
    gl::get_integer_v(gl::Get_p_name::max_samples,                &limits.max_samples);
    gl::get_integer_v(gl::Get_p_name::max_color_texture_samples,  &limits.max_color_texture_samples);
    gl::get_integer_v(gl::Get_p_name::max_depth_texture_samples,  &limits.max_depth_texture_samples);
    gl::get_integer_v(gl::Get_p_name::max_framebuffer_samples,    &limits.max_framebuffer_samples);
    gl::get_integer_v(gl::Get_p_name::max_integer_samples,        &limits.max_integer_samples);

    log_configuration->info(
        "max samples = {}, max color texture samples = {}, max depth texture samples = {}, "
        "max framebuffer samples = {}, max integer samples = {}",
        limits.max_samples,
        limits.max_color_texture_samples,
        limits.max_depth_texture_samples,
        limits.max_framebuffer_samples,
        limits.max_integer_samples
    );

    gl::get_integer_v(gl::Get_p_name::max_3d_texture_size,              &limits.max_3d_texture_size);
    gl::get_integer_v(gl::Get_p_name::max_cube_map_texture_size,        &limits.max_cube_map_texture_size);
    gl::get_integer_v(gl::Get_p_name::max_texture_image_units,          &limits.max_texture_image_units);
    gl::get_integer_v(gl::Get_p_name::max_combined_texture_image_units, &limits.max_combined_texture_image_units);

    // GL 3.0 introduced context flags
    int context_flags = 0;
    gl::get_integer_v(gl::Get_p_name::context_flags, &context_flags);
    if ((static_cast<unsigned int>(context_flags) & static_cast<unsigned int>(GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT)) != 0) {
        info.forward_compatible = true;
        log_configuration->info("forward compatible");
    }

    // GL 3.3 introduced context profile mask
    int context_profile_mask = 0;
    gl::get_integer_v(gl::Get_p_name::context_profile_mask, &context_profile_mask);
    if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_CORE_PROFILE_BIT)) != 0) {
        info.core_profile = true;
        log_configuration->info("core profile");
    }
    if ((static_cast<unsigned int>(context_profile_mask) & static_cast<unsigned int>(GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)) != 0) {
        info.compatibility_profile = true;
        log_configuration->info("compatibility profile");
    }


    gl::get_integer_v(gl::Get_p_name::max_texture_buffer_size, &limits.max_texture_buffer_size);

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

    int shader_storage_buffer_offset_alignment{0};
    int uniform_buffer_offset_alignment       {0};
    gl::get_integer_v(gl::Get_p_name::shader_storage_buffer_offset_alignment,    &shader_storage_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::uniform_buffer_offset_alignment,           &uniform_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::max_uniform_block_size,                    &limits.max_uniform_block_size);
    gl::get_integer_v(gl::Get_p_name::max_shader_storage_buffer_bindings,        &limits.max_shader_storage_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_uniform_buffer_bindings,               &limits.max_uniform_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_vertex_shader_storage_blocks,          &limits.max_vertex_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_vertex_uniform_blocks,                 &limits.max_vertex_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_fragment_shader_storage_blocks,        &limits.max_fragment_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_fragment_uniform_blocks,               &limits.max_fragment_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_geometry_shader_storage_blocks,        &limits.max_geometry_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_geometry_uniform_blocks,               &limits.max_geometry_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_control_shader_storage_blocks,    &limits.max_tess_control_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_control_uniform_blocks,           &limits.max_tess_control_uniform_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_shader_storage_blocks, &limits.max_tess_evaluation_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_tess_evaluation_uniform_blocks,        &limits.max_tess_evaluation_uniform_blocks);

    implementation_defined.shader_storage_buffer_offset_alignment = static_cast<unsigned int>(shader_storage_buffer_offset_alignment);
    implementation_defined.uniform_buffer_offset_alignment        = static_cast<unsigned int>(uniform_buffer_offset_alignment);
    log_configuration->info(
        "uniform block ("
        "max size = {}, "
        "offset alignment = {}. "
        "max bindings = {}, "
        "max vertex blocks = {}, "
        "max fragment blocks = {}"
        ")",
        limits.max_uniform_block_size,
        implementation_defined.uniform_buffer_offset_alignment,
        limits.max_uniform_buffer_bindings,
        limits.max_vertex_uniform_blocks,
        limits.max_fragment_uniform_blocks
    );
    log_configuration->info(
        "shader storage block ("
        "offset alignment = {}"
        ", max bindings = {}"
        ", max vertex blocks = {}"
        ", max fragment blocks = {}"
        ")",
        implementation_defined.shader_storage_buffer_offset_alignment,
        limits.max_shader_storage_buffer_bindings,
        limits.max_vertex_shader_storage_blocks,
        limits.max_fragment_shader_storage_blocks
    );

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_bindless_texture)) {
        info.use_bindless_texture = true;
    }
    log_configuration->info("GL_ARB_bindless_texture supported : {}", info.use_bindless_texture);

    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_sparse_texture)) {
        info.use_sparse_texture = true;
        GLint max_sparse_texture_size{};
        gl::get_integer_v(gl::Get_p_name::max_sparse_texture_size_arb, &max_sparse_texture_size);
        log_configuration->info("max sparse texture size : {}", max_sparse_texture_size);

        gl::Internal_format formats[] = {
            gl::Internal_format::r8,
            gl::Internal_format::rgba8,
            gl::Internal_format::rgba16f,
            gl::Internal_format::rg32f,
            gl::Internal_format::rgba32f,
            gl::Internal_format::depth_component,
            gl::Internal_format::depth_component32f,
            gl::Internal_format::depth24_stencil8,
            gl::Internal_format::stencil_index8
        };

        for (const auto format : formats) {
            GLint num_virtual_page_sizes{};
            gl::get_internalformat_iv(
                gl::Texture_target::texture_2d,
                format,
                gl::Internal_format_p_name::num_virtual_page_sizes_arb,
                1,
                &num_virtual_page_sizes
            );

            if (num_virtual_page_sizes == 0) {
                continue;
            }

            std::vector<GLint64> x_sizes;
            std::vector<GLint64> y_sizes;
            std::vector<GLint64> z_sizes;
            x_sizes.resize(num_virtual_page_sizes);
            y_sizes.resize(num_virtual_page_sizes);
            z_sizes.resize(num_virtual_page_sizes);
            gl::get_internalformat_i_64v(
                gl::Texture_target::texture_2d,
                format,
                gl::Internal_format_p_name::virtual_page_size_x_arb,
                static_cast<GLsizei>(num_virtual_page_sizes),
                x_sizes.data()
            );
            gl::get_internalformat_i_64v(
                gl::Texture_target::texture_2d,
                format,
                gl::Internal_format_p_name::virtual_page_size_y_arb,
                static_cast<GLsizei>(num_virtual_page_sizes),
                y_sizes.data()
            );
            gl::get_internalformat_i_64v(
                gl::Texture_target::texture_2d,
                format,
                gl::Internal_format_p_name::virtual_page_size_z_arb,
                static_cast<GLsizei>(num_virtual_page_sizes),
                z_sizes.data()
            );
            std::stringstream ss;
            for (GLint i = 0; i < num_virtual_page_sizes; ++i) {
                ss << fmt::format(" {} x {} x {}", x_sizes[i], y_sizes[i], z_sizes[i]);
            }
            sparse_tile_sizes[format] = Tile_size{
                .x = static_cast<int>(x_sizes[0]),
                .y = static_cast<int>(y_sizes[0]),
                .z = static_cast<int>(z_sizes[0]),
            };

            log_configuration->info(
                "    {} : num page sizes {} :{}",
                gl::c_str(format),
                num_virtual_page_sizes,
                ss.str()
            );
        }
    }
    log_configuration->info("GL_ARB_sparse_texture supported : {}", info.use_sparse_texture);

    info.use_persistent_buffers = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
    log_configuration->info("GL_ARB_buffer_storage supported : {}", info.use_sparse_texture);

    if (
        !gl::is_extension_supported(gl::Extension::Extension_GL_ARB_direct_state_access) &&
        (info.gl_version < 450)
    ) {
        log_configuration->info("Direct state access is not supported by OpenGL driver. This is a fatal error.");
    }
}

} // namespace erhe::graphics
