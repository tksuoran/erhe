#include "erhe_graphics/instance.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_gl/command_info.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/texture.hpp"

#include "glslang/Public/ShaderLang.h"

#include "igl/IGL.h"

#if !defined(WIN32)
#   include <csignal>
#endif

#include <sstream>
#include <vector>

typedef unsigned char GLubyte;

namespace erhe::graphics
{


namespace
{

constexpr float float_zero_value{0.0f};
constexpr float float_one_value {1.0f};

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

//

Instance::Instance(erhe::window::Context_window& context_window)
    : shader_monitor  {*this}
    , context_provider{*this, opengl_state_tracker}
    , m_context_window{context_window}
{
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

    gl::get_integer_v(gl::Get_p_name::max_texture_size, &limits.max_texture_size);
    log_startup->info("max texture size: {}", limits.max_texture_size);

    gl::get_integer_v(gl::Get_p_name::max_vertex_attribs, &limits.max_vertex_attribs);
    log_startup->info("max vertex attribs: {}", limits.max_vertex_attribs);

    log_startup->trace("GL Extensions:");
    {
        int num_extensions{0};

        gl::get_integer_v(gl::Get_p_name::num_extensions, &num_extensions);
        if (num_extensions > 0) {
            extensions.reserve(num_extensions);
            for (unsigned int i = 0; i < static_cast<unsigned int>(num_extensions); ++i) {
                const auto* extension_str = gl::get_string_i(gl::String_name::extensions, i);
                auto e = std::string(reinterpret_cast<const char*>(extension_str));
                log_startup->trace("    {}", e);
                extensions.push_back(e);
            }
        }
    }

    {
        auto shading_language_version = (get_string)(gl::String_name::shading_language_version);
        log_startup->info("GLSL Version:  {}", shading_language_version);
        versions = split(shading_language_version, '.');

        major = !versions.empty() ? to_int(digits_only(versions[0])) : 0;
        minor = (versions.size() > 1) ? to_int(digits_only(versions[1])) : 0;
        info.glsl_version = (major * 100) + minor;
    }

    log_startup->info("glVersion:   {}", info.gl_version);
    log_startup->info("glslVersion: {}", info.glsl_version);

    gl::command_info_init(info.glsl_version, extensions);

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

    gl::get_integer_v(gl::Get_p_name::max_3d_texture_size,              &limits.max_3d_texture_size);
    gl::get_integer_v(gl::Get_p_name::max_cube_map_texture_size,        &limits.max_cube_map_texture_size);
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

    int shader_storage_buffer_offset_alignment{0};
    int uniform_buffer_offset_alignment       {0};
    gl::get_integer_v(gl::Get_p_name::shader_storage_buffer_offset_alignment,    &shader_storage_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::uniform_buffer_offset_alignment,           &uniform_buffer_offset_alignment);
    gl::get_integer_v(gl::Get_p_name::max_uniform_block_size,                    &limits.max_uniform_block_size);
    gl::get_integer_v(gl::Get_p_name::max_shader_storage_buffer_bindings,        &limits.max_shader_storage_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_uniform_buffer_bindings,               &limits.max_uniform_buffer_bindings);
    gl::get_integer_v(gl::Get_p_name::max_compute_shader_storage_blocks,         &limits.max_compute_shader_storage_blocks);
    gl::get_integer_v(gl::Get_p_name::max_compute_uniform_blocks,                &limits.max_compute_uniform_blocks);
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
        info.use_sparse_texture = true;
        GLint max_sparse_texture_size{};
        gl::get_integer_v(gl::Get_p_name::max_sparse_texture_size_arb, &max_sparse_texture_size);
        log_startup->info("max sparse texture size : {}", max_sparse_texture_size);

        gl::Internal_format formats[] = {
            gl::Internal_format::r8,
            gl::Internal_format::rgba8,
            gl::Internal_format::rgba16f,
            gl::Internal_format::rg32f,
            gl::Internal_format::rgba32f,
            gl::Internal_format::depth_component,
            gl::Internal_format::depth_component32f,
            gl::Internal_format::depth24_stencil8,
            gl::Internal_format::depth32f_stencil8,
            gl::Internal_format::depth_stencil,
            gl::Internal_format::stencil_index8
        };

        for (const auto format : formats) {
            GLint supported{};
            gl::get_internalformat_iv(
                gl::Texture_target::texture_2d,
                format,
                gl::Internal_format_p_name::internalformat_supported,
                1,
                &supported
            );
            if (supported == GL_FALSE) {
                continue;
            }

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

            log_startup->info(
                "    {} : num page sizes {} :{}",
                gl::c_str(format),
                num_virtual_page_sizes,
                ss.str()
            );
        }
    }
    log_startup->info("GL_ARB_sparse_texture supported : {}", info.use_sparse_texture);

    info.use_persistent_buffers = gl::is_extension_supported(gl::Extension::Extension_GL_ARB_buffer_storage);
    log_startup->info("GL_ARB_buffer_storage supported : {}", info.use_sparse_texture);

    if (
        !gl::is_extension_supported(gl::Extension::Extension_GL_ARB_direct_state_access) &&
        (info.gl_version < 450)
    ) {
        log_startup->info("Direct state access is not supported by OpenGL driver. This is a fatal error.");
    }

    bool force_no_bindless          {false};
    bool force_no_persistent_buffers{false};
    bool capture_support            {false};
    {
        auto ini = erhe::configuration::get_ini("erhe.ini", "graphics");
        ini->get("reverse_depth",   configuration.reverse_depth  );
        ini->get("post_processing", configuration.post_processing);
        ini->get("use_time_query",  configuration.use_time_query );
        ini->get("force_no_bindless",           force_no_bindless);
        ini->get("force_no_persistent_buffers", force_no_persistent_buffers);
    }
    {
        auto ini = erhe::configuration::get_ini("erhe.ini", "renderdoc");
        ini->get("capture_support", capture_support);
    }

    if (force_no_bindless || capture_support) {
        if (info.use_bindless_texture) {
            info.use_bindless_texture = false;
            log_startup->warn("Force disabled GL_ARB_bindless_texture due to erhe.ini setting");
        }
    }

    if (force_no_persistent_buffers) {
        if (info.use_persistent_buffers) {
            info.use_persistent_buffers = false;
            log_startup->warn("Force disabled persistently mapped buffers due to erhe.ini setting");
        }
    }

    shader_monitor.begin();

    glslang::InitializeProcess();
}

Instance::~Instance()
{
    glslang::FinalizeProcess();
}

auto Instance::depth_clear_value_pointer() const -> const float*
{
    return configuration.reverse_depth ? &float_zero_value : &float_one_value;
}

auto Instance::depth_function(
    const gl::Depth_function depth_function
) const -> gl::Depth_function
{
    return configuration.reverse_depth ? reverse(depth_function) : depth_function;
}

auto Instance::get_handle(
    const Texture& texture,
    const Sampler& sampler
) const -> uint64_t
{
    if (info.use_bindless_texture) {
        return gl::get_texture_sampler_handle_arb(
            texture.gl_name(),
            sampler.gl_name()
        );
    } else {
        const uint64_t texture_name  = static_cast<uint64_t>(texture.gl_name());
        const uint64_t sampler_name  = static_cast<uint64_t>(sampler.gl_name());
        const uint64_t handle        = texture_name | (sampler_name << 32);
        return handle;
    }
}

auto Instance::create_dummy_texture() -> std::shared_ptr<Texture>
{
    const erhe::graphics::Texture::Create_info create_info{
        .instance = *this,
        .width    = 2,
        .height   = 2
    };

    auto texture = std::make_shared<Texture>(create_info);
    texture->set_debug_label("dummy");
    const std::array<uint8_t, 16> dummy_pixel{
        0xee, 0x11, 0xdd, 0xff,
        0xcc, 0x11, 0xbb, 0xff,
        0xee, 0x11, 0xdd, 0xff,
        0xcc, 0x11, 0xbb, 0xff
    };
    const gsl::span<const std::byte> image_data{
        reinterpret_cast<const std::byte*>(&dummy_pixel[0]),
        dummy_pixel.size()
    };

    texture->upload(
        create_info.internal_format,
        image_data,
        create_info.width,
        create_info.height
    );

    return texture;
}


void Instance::texture_unit_cache_reset(const unsigned int base_texture_unit)
{
    m_base_texture_unit = base_texture_unit;
    m_texture_units.clear();
}

auto Instance::texture_unit_cache_allocate(
    const uint64_t handle
) -> std::optional<std::size_t>
{
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    const GLuint texture_name = erhe::graphics::get_texture_from_handle(handle);
    const GLuint sampler_name = erhe::graphics::get_sampler_from_handle(handle);
#endif

    for (std::size_t texture_unit = 0, end = m_texture_units.size(); texture_unit < end; ++texture_unit) {
        if (m_texture_units[texture_unit] == handle) {
            SPDLOG_LOGGER_TRACE(
                log_texture,
                "cache hit texture unit {} for texture {}, sampler {}",
                texture_unit,
                texture_name,
                sampler_name
            );
            return texture_unit;
        }
    }

    const std::size_t result = m_texture_units.size();
    m_texture_units.push_back(handle);
    SPDLOG_LOGGER_TRACE(
        log_texture,
        "allocted texture unit {} for texture {}, sampler {}",
        result,
        texture_name,
        sampler_name
    );
    return result;
}

auto Instance::texture_unit_cache_bind(const uint64_t fallback_handle) -> size_t
{
    const GLuint fallback_texture_name = erhe::graphics::get_texture_from_handle(fallback_handle);
    const GLuint fallback_sampler_name = erhe::graphics::get_sampler_from_handle(fallback_handle);

    GLuint i{};
    GLuint end = std::min(
        static_cast<GLuint>(m_texture_units.size()),
        static_cast<GLuint>(limits.max_texture_image_units)
    );

    for (i = 0; i < end; ++i) {
        const uint64_t handle       = m_texture_units[i];
        const GLuint   texture_name = erhe::graphics::get_texture_from_handle(handle);
        const GLuint   sampler_name = erhe::graphics::get_sampler_from_handle(handle);

        if (handle != 0) {
#if !defined(NDEBUG)
            if (gl::is_texture(texture_name) == GL_TRUE) {
                gl::bind_texture_unit(m_base_texture_unit + i, texture_name);
                SPDLOG_LOGGER_TRACE(
                    log_texture,
                    "texture unit {} + {} = {}: bound texture {}",
                    m_base_texture_unit,
                    i,
                    m_base_texture_unit + i,
                    texture_name
                );
            } else {
                log_texture->warn(
                    "texture unit {} + {} = {}: {} is not a texture",
                    m_base_texture_unit,
                    i,
                    m_base_texture_unit + i,
                    texture_name
                );
                gl::bind_texture_unit(m_base_texture_unit + i, erhe::graphics::get_texture_from_handle(fallback_handle));
            }

            if (
                (sampler_name == 0) ||
                (gl::is_sampler(sampler_name) == GL_TRUE)
            ) {
                gl::bind_sampler(m_base_texture_unit + i, sampler_name);
                SPDLOG_LOGGER_TRACE(
                    log_texture,
                    "texture unit {} + {} = {}: bound sampler {}",
                    m_base_texture_unit,
                    i,
                    m_base_texture_unit + i,
                    sampler_name
                );
            } else {
                gl::bind_sampler(m_base_texture_unit + i, erhe::graphics::get_sampler_from_handle(fallback_handle));
                log_texture->warn(
                    "texture unit {} + {} = {}: {} is not a sampler",
                    m_base_texture_unit,
                    i,
                    m_base_texture_unit + i,
                    sampler_name
                );
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

} // namespace erhe::graphics
