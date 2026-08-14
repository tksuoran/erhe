#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/dynamic_load.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_log/log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <iostream>

#if !defined(WIN32)
# include <csignal>
#endif

namespace gl_helpers {

std::shared_ptr<spdlog::logger> log_gl_debug;

static bool           enable_error_checking = true;
static Error_callback error_callback;

void initialize_logging()
{
    log_gl_debug = erhe::log::make_logger("erhe.gl.debug");
}

void set_error_checking(const bool enable)
{
    enable_error_checking = enable;
}

void set_error_callback(Error_callback callback)
{
    error_callback = std::move(callback);
}

auto peek_error() -> unsigned int
{
#if defined(ERHE_DLOAD_ALL_GL_SYMBOLS)
    return static_cast<unsigned int>(gl::glGetError());
#else
    return static_cast<unsigned int>(glGetError());
#endif
}

auto format_gl_state() -> std::string
{
    // Suspend wrapper-level error checking around the state queries so
    // the get_*-call wrappers do not invoke check_error recursively, and
    // any errors produced by unsupported state queries on this GL
    // version do not leak into subsequent error attribution.
    const bool prev_enable = enable_error_checking;
    enable_error_checking = false;

    GLint cur_program             {0};
    GLint cur_vao                 {0};
    GLint cur_draw_fbo            {0};
    GLint cur_array_buf           {0};
    GLint cur_elem_buf            {0};
    GLint cur_uniform_buf         {0};
    GLint cur_active_texture      {0};
    GLint program_link_status     {0};
    GLint program_validate_status {0};
    GLint program_active_attribs  {0};
    gl::get_integer_v(gl::Get_p_name::current_program,              &cur_program);
    gl::get_integer_v(gl::Get_p_name::vertex_array_binding,         &cur_vao);
    gl::get_integer_v(gl::Get_p_name::draw_framebuffer_binding,     &cur_draw_fbo);
    gl::get_integer_v(gl::Get_p_name::array_buffer_binding,         &cur_array_buf);
    gl::get_integer_v(gl::Get_p_name::element_array_buffer_binding, &cur_elem_buf);
    gl::get_integer_v(gl::Get_p_name::uniform_buffer_binding,       &cur_uniform_buf);
    gl::get_integer_v(gl::Get_p_name::active_texture,               &cur_active_texture);
    if (cur_program != 0) {
        gl::get_program_iv(static_cast<GLuint>(cur_program), gl::Program_property::link_status,       &program_link_status);
        gl::get_program_iv(static_cast<GLuint>(cur_program), gl::Program_property::validate_status,   &program_validate_status);
        gl::get_program_iv(static_cast<GLuint>(cur_program), gl::Program_property::active_attributes, &program_active_attribs);
    }

    // Drain any errors the queries themselves produced so the next
    // wrapper-level check_error attributes errors to a fresh GL call.
    while (peek_error() != static_cast<unsigned int>(gl::Error_code::no_error)) {}

    enable_error_checking = prev_enable;

    return fmt::format(
        "program={} (link_status={} validate_status={} active_attribs={}) "
        "vao={} draw_fbo={} array_buf={} elem_buf={} ubo={} active_tex_unit=0x{:x}",
        cur_program, program_link_status, program_validate_status, program_active_attribs,
        cur_vao, cur_draw_fbo, cur_array_buf, cur_elem_buf,
        cur_uniform_buf,
        static_cast<unsigned int>(cur_active_texture)
    );
}

void check_error()
{
    if (!enable_error_checking) {
        return;
    }

    const auto error_code = static_cast<gl::Error_code>(peek_error());

    if (error_code != gl::Error_code::no_error) {
        if (error_callback) {
            const std::string state   = format_gl_state();
            const std::string message = fmt::format("GL error: {}\n{}", gl::c_str(error_code), state);
            error_callback(message);
            return;
        }
        log_gl_debug->error("{}", gl::c_str(error_code));
#if defined(WIN32)
        DebugBreak();
#endif
    }
}

auto size_of_type(const gl::Draw_elements_type type) -> size_t
{
    switch (type) {
        //using enum gl::Draw_elements_type;
        case gl::Draw_elements_type::unsigned_byte:  return 1;
        case gl::Draw_elements_type::unsigned_short: return 2;
        case gl::Draw_elements_type::unsigned_int:   return 4;
        default: {
            ERHE_FATAL("Bad draw elements index type");
        }
    }
}

auto size_of_type(const gl::Vertex_attrib_type type) -> size_t
{
    switch (type) {
        //using enum gl::Vertex_attrib_type;
        case gl::Vertex_attrib_type::byte:
        case gl::Vertex_attrib_type::unsigned_byte: {
            return 1;
        }

        case gl::Vertex_attrib_type::half_float:
        case gl::Vertex_attrib_type::short_:
        case gl::Vertex_attrib_type::unsigned_short: {
            return 2;
        }

        case gl::Vertex_attrib_type::fixed:
        case gl::Vertex_attrib_type::float_:
        case gl::Vertex_attrib_type::int_:
        case gl::Vertex_attrib_type::int_2_10_10_10_rev:
        case gl::Vertex_attrib_type::unsigned_int:
        case gl::Vertex_attrib_type::unsigned_int_10f_11f_11f_rev:
        case gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev: {
            return 4;
        }

        case gl::Vertex_attrib_type::double_: {
            return 8;
        }

        default: {
            ERHE_FATAL("Bad vertex attribute type");
        }
    }
}

auto is_indexed(const gl::Buffer_target type) -> bool
{
    switch (type) {
        case gl::Buffer_target::array_buffer             : return false;
        case gl::Buffer_target::atomic_counter_buffer    : return true;
        case gl::Buffer_target::copy_read_buffer         : return false;
        case gl::Buffer_target::copy_write_buffer        : return false;
        case gl::Buffer_target::dispatch_indirect_buffer : return false;
        case gl::Buffer_target::draw_indirect_buffer     : return false;
        case gl::Buffer_target::element_array_buffer     : return false;
        case gl::Buffer_target::parameter_buffer         : return false;
        case gl::Buffer_target::pixel_pack_buffer        : return false;
        case gl::Buffer_target::pixel_unpack_buffer      : return false;
        case gl::Buffer_target::query_buffer             : return false;
        case gl::Buffer_target::shader_storage_buffer    : return true;
        case gl::Buffer_target::texture_buffer           : return false;
        case gl::Buffer_target::transform_feedback_buffer: return true;
        case gl::Buffer_target::uniform_buffer           : return true;
        default: {
            ERHE_FATAL("Bad buffer target type");
        }
    }
}

auto is_compressed(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return true;
        case gl::Internal_format::compressed_red                            : return true;
        case gl::Internal_format::compressed_red_rgtc1                      : return true;
        case gl::Internal_format::compressed_rg                             : return true;
        case gl::Internal_format::compressed_rg11_eac                       : return true;
        case gl::Internal_format::compressed_rg_rgtc2                       : return true;
        case gl::Internal_format::compressed_rgb                            : return true;
        case gl::Internal_format::compressed_rgb8_etc2                      : return true;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return true;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return true;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return true;
        case gl::Internal_format::compressed_rgba                           : return true;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return true;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return true;
        case gl::Internal_format::compressed_signed_r11_eac                 : return true;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return true;
        case gl::Internal_format::compressed_signed_rg11_eac                : return true;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return true;
        case gl::Internal_format::compressed_srgb                           : return true;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return true;
        case gl::Internal_format::compressed_srgb8_etc2                     : return true;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return true;
        case gl::Internal_format::compressed_srgb_alpha                     : return true;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return true;
        case gl::Internal_format::depth24_stencil8                          : return false;
        case gl::Internal_format::depth32f_stencil8                         : return false;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return false;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return false;
        case gl::Internal_format::r16ui                                     : return false;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return false;
        case gl::Internal_format::r32ui                                     : return false;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return false;
        case gl::Internal_format::r8ui                                      : return false;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return false;
        case gl::Internal_format::rg16ui                                    : return false;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return false;
        case gl::Internal_format::rg32ui                                    : return false;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return false;
        case gl::Internal_format::rg8ui                                     : return false;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return false;
        case gl::Internal_format::rgb10_a2ui                                : return false;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return false;
        case gl::Internal_format::rgb16ui                                   : return false;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return false;
        case gl::Internal_format::rgb32ui                                   : return false;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return false;
        case gl::Internal_format::rgb8ui                                    : return false;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return false;
        case gl::Internal_format::rgba12                                    : return false;
        case gl::Internal_format::rgba16                                    : return false;
        case gl::Internal_format::rgba16f                                   : return false;
        case gl::Internal_format::rgba16i                                   : return false;
        case gl::Internal_format::rgba16ui                                  : return false;
        case gl::Internal_format::rgba32f                                   : return false;
        case gl::Internal_format::rgba32i                                   : return false;
        case gl::Internal_format::rgba32ui                                  : return false;
        case gl::Internal_format::rgba4                                     : return false;
        case gl::Internal_format::rgba8                                     : return false;
        case gl::Internal_format::rgba8_snorm                               : return false;
        case gl::Internal_format::rgba8i                                    : return false;
        case gl::Internal_format::rgba8ui                                   : return false;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return false;
        case gl::Internal_format::srgb_alpha                                : return false;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto is_integer(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return false;
        case gl::Internal_format::compressed_red                            : return false;
        case gl::Internal_format::compressed_red_rgtc1                      : return false;
        case gl::Internal_format::compressed_rg                             : return false;
        case gl::Internal_format::compressed_rg11_eac                       : return false;
        case gl::Internal_format::compressed_rg_rgtc2                       : return false;
        case gl::Internal_format::compressed_rgb                            : return false;
        case gl::Internal_format::compressed_rgb8_etc2                      : return false;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return false;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return false;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return false;
        case gl::Internal_format::compressed_rgba                           : return false;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return false;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return false;
        case gl::Internal_format::compressed_signed_r11_eac                 : return false;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return false;
        case gl::Internal_format::compressed_signed_rg11_eac                : return false;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return false;
        case gl::Internal_format::compressed_srgb                           : return false;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return false;
        case gl::Internal_format::compressed_srgb8_etc2                     : return false;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return false;
        case gl::Internal_format::compressed_srgb_alpha                     : return false;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return false;
        case gl::Internal_format::depth24_stencil8                          : return false;
        case gl::Internal_format::depth32f_stencil8                         : return false;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return false;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return true;
        case gl::Internal_format::r16ui                                     : return false;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return true;
        case gl::Internal_format::r32ui                                     : return false;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return true;
        case gl::Internal_format::r8ui                                      : return false;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return true;
        case gl::Internal_format::rg16ui                                    : return false;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return true;
        case gl::Internal_format::rg32ui                                    : return false;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return true;
        case gl::Internal_format::rg8ui                                     : return false;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return false;
        case gl::Internal_format::rgb10_a2ui                                : return false;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return true;
        case gl::Internal_format::rgb16ui                                   : return false;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return true;
        case gl::Internal_format::rgb32ui                                   : return false;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return true;
        case gl::Internal_format::rgb8ui                                    : return true;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return false;
        case gl::Internal_format::rgba12                                    : return false;
        case gl::Internal_format::rgba16                                    : return false;
        case gl::Internal_format::rgba16f                                   : return false;
        case gl::Internal_format::rgba16i                                   : return true;
        case gl::Internal_format::rgba16ui                                  : return true;
        case gl::Internal_format::rgba32f                                   : return false;
        case gl::Internal_format::rgba32i                                   : return true;
        case gl::Internal_format::rgba32ui                                  : return true;
        case gl::Internal_format::rgba4                                     : return false;
        case gl::Internal_format::rgba8                                     : return false;
        case gl::Internal_format::rgba8_snorm                               : return false;
        case gl::Internal_format::rgba8i                                    : return true;
        case gl::Internal_format::rgba8ui                                   : return true;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return false;
        case gl::Internal_format::srgb_alpha                                : return false;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto is_unsigned_integer(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return false;
        case gl::Internal_format::compressed_red                            : return false;
        case gl::Internal_format::compressed_red_rgtc1                      : return false;
        case gl::Internal_format::compressed_rg                             : return false;
        case gl::Internal_format::compressed_rg11_eac                       : return false;
        case gl::Internal_format::compressed_rg_rgtc2                       : return false;
        case gl::Internal_format::compressed_rgb                            : return false;
        case gl::Internal_format::compressed_rgb8_etc2                      : return false;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return false;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return false;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return false;
        case gl::Internal_format::compressed_rgba                           : return false;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return false;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return false;
        case gl::Internal_format::compressed_signed_r11_eac                 : return false;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return false;
        case gl::Internal_format::compressed_signed_rg11_eac                : return false;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return false;
        case gl::Internal_format::compressed_srgb                           : return false;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return false;
        case gl::Internal_format::compressed_srgb8_etc2                     : return false;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return false;
        case gl::Internal_format::compressed_srgb_alpha                     : return false;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return false;
        case gl::Internal_format::depth24_stencil8                          : return false;
        case gl::Internal_format::depth32f_stencil8                         : return false;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return false;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return true;
        case gl::Internal_format::r16ui                                     : return true;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return true;
        case gl::Internal_format::r32ui                                     : return true;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return true;
        case gl::Internal_format::r8ui                                      : return true;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return true;
        case gl::Internal_format::rg16ui                                    : return true;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return true;
        case gl::Internal_format::rg32ui                                    : return true;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return true;
        case gl::Internal_format::rg8ui                                     : return true;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return false;
        case gl::Internal_format::rgb10_a2ui                                : return true;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return true;
        case gl::Internal_format::rgb16ui                                   : return true;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return true;
        case gl::Internal_format::rgb32ui                                   : return true;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return true;
        case gl::Internal_format::rgb8ui                                    : return true;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return false;
        case gl::Internal_format::rgba12                                    : return false;
        case gl::Internal_format::rgba16                                    : return false;
        case gl::Internal_format::rgba16f                                   : return false;
        case gl::Internal_format::rgba16i                                   : return true;
        case gl::Internal_format::rgba16ui                                  : return true;
        case gl::Internal_format::rgba32f                                   : return false;
        case gl::Internal_format::rgba32i                                   : return true;
        case gl::Internal_format::rgba32ui                                  : return true;
        case gl::Internal_format::rgba4                                     : return false;
        case gl::Internal_format::rgba8                                     : return false;
        case gl::Internal_format::rgba8_snorm                               : return false;
        case gl::Internal_format::rgba8i                                    : return true;
        case gl::Internal_format::rgba8ui                                   : return true;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return false;
        case gl::Internal_format::srgb_alpha                                : return false;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto has_color(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return true;
        case gl::Internal_format::compressed_red                            : return true;
        case gl::Internal_format::compressed_red_rgtc1                      : return true;
        case gl::Internal_format::compressed_rg                             : return true;
        case gl::Internal_format::compressed_rg11_eac                       : return true;
        case gl::Internal_format::compressed_rg_rgtc2                       : return true;
        case gl::Internal_format::compressed_rgb                            : return true;
        case gl::Internal_format::compressed_rgb8_etc2                      : return true;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return true;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return true;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return true;
        case gl::Internal_format::compressed_rgba                           : return true;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return true;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return true;
        case gl::Internal_format::compressed_signed_r11_eac                 : return true;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return true;
        case gl::Internal_format::compressed_signed_rg11_eac                : return true;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return true;
        case gl::Internal_format::compressed_srgb                           : return true;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return true;
        case gl::Internal_format::compressed_srgb8_etc2                     : return true;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return true;
        case gl::Internal_format::compressed_srgb_alpha                     : return true;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return true;
        case gl::Internal_format::depth24_stencil8                          : return false;
        case gl::Internal_format::depth32f_stencil8                         : return false;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return false;
        case gl::Internal_format::r11f_g11f_b10f                            : return true;
        case gl::Internal_format::r16                                       : return true;
        case gl::Internal_format::r16_snorm                                 : return true;
        case gl::Internal_format::r16f                                      : return true;
        case gl::Internal_format::r16i                                      : return true;
        case gl::Internal_format::r16ui                                     : return true;
        case gl::Internal_format::r32f                                      : return true;
        case gl::Internal_format::r32i                                      : return true;
        case gl::Internal_format::r32ui                                     : return true;
        case gl::Internal_format::r3_g3_b2                                  : return true;
        case gl::Internal_format::r8                                        : return true;
        case gl::Internal_format::r8_snorm                                  : return true;
        case gl::Internal_format::r8i                                       : return true;
        case gl::Internal_format::r8ui                                      : return true;
        case gl::Internal_format::red                                       : return true;
        case gl::Internal_format::rg                                        : return true;
        case gl::Internal_format::rg16                                      : return true;
        case gl::Internal_format::rg16_snorm                                : return true;
        case gl::Internal_format::rg16f                                     : return true;
        case gl::Internal_format::rg16i                                     : return true;
        case gl::Internal_format::rg16ui                                    : return true;
        case gl::Internal_format::rg32f                                     : return true;
        case gl::Internal_format::rg32i                                     : return true;
        case gl::Internal_format::rg32ui                                    : return true;
        case gl::Internal_format::rg8                                       : return true;
        case gl::Internal_format::rg8_snorm                                 : return true;
        case gl::Internal_format::rg8i                                      : return true;
        case gl::Internal_format::rg8ui                                     : return true;
        case gl::Internal_format::rgb                                       : return true;
        case gl::Internal_format::rgb10                                     : return true;
        case gl::Internal_format::rgb10_a2                                  : return true;
        case gl::Internal_format::rgb10_a2ui                                : return true;
        case gl::Internal_format::rgb12                                     : return true;
        case gl::Internal_format::rgb16                                     : return true;
        case gl::Internal_format::rgb16_snorm                               : return true;
        case gl::Internal_format::rgb16f                                    : return true;
        case gl::Internal_format::rgb16i                                    : return true;
        case gl::Internal_format::rgb16ui                                   : return true;
        case gl::Internal_format::rgb32f                                    : return true;
        case gl::Internal_format::rgb32i                                    : return true;
        case gl::Internal_format::rgb32ui                                   : return true;
        case gl::Internal_format::rgb4                                      : return true;
        case gl::Internal_format::rgb5                                      : return true;
        case gl::Internal_format::rgb5_a1                                   : return true;
        case gl::Internal_format::rgb8                                      : return true;
        case gl::Internal_format::rgb8_snorm                                : return true;
        case gl::Internal_format::rgb8i                                     : return true;
        case gl::Internal_format::rgb8ui                                    : return true;
        case gl::Internal_format::rgb9_e5                                   : return true;
        case gl::Internal_format::rgba                                      : return true;
        case gl::Internal_format::rgba12                                    : return true;
        case gl::Internal_format::rgba16                                    : return true;
        case gl::Internal_format::rgba16f                                   : return true;
        case gl::Internal_format::rgba16i                                   : return true;
        case gl::Internal_format::rgba16ui                                  : return true;
        case gl::Internal_format::rgba32f                                   : return true;
        case gl::Internal_format::rgba32i                                   : return true;
        case gl::Internal_format::rgba32ui                                  : return true;
        case gl::Internal_format::rgba4                                     : return true;
        case gl::Internal_format::rgba8                                     : return true;
        case gl::Internal_format::rgba8_snorm                               : return true;
        case gl::Internal_format::rgba8i                                    : return true;
        case gl::Internal_format::rgba8ui                                   : return true;
        case gl::Internal_format::srgb                                      : return true;
        case gl::Internal_format::srgb8                                     : return true;
        case gl::Internal_format::srgb8_alpha8                              : return true;
        case gl::Internal_format::srgb_alpha                                : return true;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto has_alpha(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return false;
        case gl::Internal_format::compressed_red                            : return false;
        case gl::Internal_format::compressed_red_rgtc1                      : return false;
        case gl::Internal_format::compressed_rg                             : return false;
        case gl::Internal_format::compressed_rg11_eac                       : return false;
        case gl::Internal_format::compressed_rg_rgtc2                       : return false;
        case gl::Internal_format::compressed_rgb                            : return false;
        case gl::Internal_format::compressed_rgb8_etc2                      : return false;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return true;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return false;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return false;
        case gl::Internal_format::compressed_rgba                           : return true;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return true;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return true;
        case gl::Internal_format::compressed_signed_r11_eac                 : return false;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return false;
        case gl::Internal_format::compressed_signed_rg11_eac                : return false;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return false;
        case gl::Internal_format::compressed_srgb                           : return false;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return true;
        case gl::Internal_format::compressed_srgb8_etc2                     : return false;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return true;
        case gl::Internal_format::compressed_srgb_alpha                     : return true;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return true;
        case gl::Internal_format::depth24_stencil8                          : return false;
        case gl::Internal_format::depth32f_stencil8                         : return false;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return false;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return false;
        case gl::Internal_format::r16ui                                     : return false;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return false;
        case gl::Internal_format::r32ui                                     : return false;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return false;
        case gl::Internal_format::r8ui                                      : return false;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return false;
        case gl::Internal_format::rg16ui                                    : return false;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return false;
        case gl::Internal_format::rg32ui                                    : return false;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return false;
        case gl::Internal_format::rg8ui                                     : return false;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return true;
        case gl::Internal_format::rgb10_a2ui                                : return true;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return false;
        case gl::Internal_format::rgb16ui                                   : return false;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return false;
        case gl::Internal_format::rgb32ui                                   : return false;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return false;
        case gl::Internal_format::rgb8ui                                    : return false;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return true;
        case gl::Internal_format::rgba12                                    : return true;
        case gl::Internal_format::rgba16                                    : return true;
        case gl::Internal_format::rgba16f                                   : return true;
        case gl::Internal_format::rgba16i                                   : return true;
        case gl::Internal_format::rgba16ui                                  : return true;
        case gl::Internal_format::rgba32f                                   : return true;
        case gl::Internal_format::rgba32i                                   : return true;
        case gl::Internal_format::rgba32ui                                  : return true;
        case gl::Internal_format::rgba4                                     : return true;
        case gl::Internal_format::rgba8                                     : return true;
        case gl::Internal_format::rgba8_snorm                               : return true;
        case gl::Internal_format::rgba8i                                    : return true;
        case gl::Internal_format::rgba8ui                                   : return true;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return true;
        case gl::Internal_format::srgb_alpha                                : return true;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto has_depth(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return false;
        case gl::Internal_format::compressed_red                            : return false;
        case gl::Internal_format::compressed_red_rgtc1                      : return false;
        case gl::Internal_format::compressed_rg                             : return false;
        case gl::Internal_format::compressed_rg11_eac                       : return false;
        case gl::Internal_format::compressed_rg_rgtc2                       : return false;
        case gl::Internal_format::compressed_rgb                            : return false;
        case gl::Internal_format::compressed_rgb8_etc2                      : return false;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return false;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return false;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return false;
        case gl::Internal_format::compressed_rgba                           : return false;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return false;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return false;
        case gl::Internal_format::compressed_signed_r11_eac                 : return false;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return false;
        case gl::Internal_format::compressed_signed_rg11_eac                : return false;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return false;
        case gl::Internal_format::compressed_srgb                           : return false;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return false;
        case gl::Internal_format::compressed_srgb8_etc2                     : return false;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return false;
        case gl::Internal_format::compressed_srgb_alpha                     : return false;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return false;
        case gl::Internal_format::depth24_stencil8                          : return true;
        case gl::Internal_format::depth32f_stencil8                         : return true;
        case gl::Internal_format::depth_component                           : return true;
        case gl::Internal_format::depth_component16                         : return true;
        case gl::Internal_format::depth_component32f                        : return true;
        case gl::Internal_format::depth_stencil                             : return true;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return false;
        case gl::Internal_format::r16ui                                     : return false;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return false;
        case gl::Internal_format::r32ui                                     : return false;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return false;
        case gl::Internal_format::r8ui                                      : return false;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return false;
        case gl::Internal_format::rg16ui                                    : return false;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return false;
        case gl::Internal_format::rg32ui                                    : return false;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return false;
        case gl::Internal_format::rg8ui                                     : return false;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return false;
        case gl::Internal_format::rgb10_a2ui                                : return false;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return false;
        case gl::Internal_format::rgb16ui                                   : return false;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return false;
        case gl::Internal_format::rgb32ui                                   : return false;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return false;
        case gl::Internal_format::rgb8ui                                    : return false;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return false;
        case gl::Internal_format::rgba12                                    : return false;
        case gl::Internal_format::rgba16                                    : return false;
        case gl::Internal_format::rgba16f                                   : return false;
        case gl::Internal_format::rgba16i                                   : return false;
        case gl::Internal_format::rgba16ui                                  : return false;
        case gl::Internal_format::rgba32f                                   : return false;
        case gl::Internal_format::rgba32i                                   : return false;
        case gl::Internal_format::rgba32ui                                  : return false;
        case gl::Internal_format::rgba4                                     : return false;
        case gl::Internal_format::rgba8                                     : return false;
        case gl::Internal_format::rgba8_snorm                               : return false;
        case gl::Internal_format::rgba8i                                    : return false;
        case gl::Internal_format::rgba8ui                                   : return false;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return false;
        case gl::Internal_format::srgb_alpha                                : return false;
        case gl::Internal_format::stencil_index                             : return false;
        case gl::Internal_format::stencil_index1                            : return false;
        case gl::Internal_format::stencil_index16                           : return false;
        case gl::Internal_format::stencil_index4                            : return false;
        case gl::Internal_format::stencil_index8                            : return false;
        default: return false;
    }
}

auto has_stencil(const gl::Internal_format format) -> bool
{
    switch (format) {
        case gl::Internal_format::compressed_r11_eac                        : return false;
        case gl::Internal_format::compressed_red                            : return false;
        case gl::Internal_format::compressed_red_rgtc1                      : return false;
        case gl::Internal_format::compressed_rg                             : return false;
        case gl::Internal_format::compressed_rg11_eac                       : return false;
        case gl::Internal_format::compressed_rg_rgtc2                       : return false;
        case gl::Internal_format::compressed_rgb                            : return false;
        case gl::Internal_format::compressed_rgb8_etc2                      : return false;
        case gl::Internal_format::compressed_rgb8_punchthrough_alpha1_etc2  : return false;
        case gl::Internal_format::compressed_rgb_bptc_signed_float          : return false;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float        : return false;
        case gl::Internal_format::compressed_rgba                           : return false;
        case gl::Internal_format::compressed_rgba8_etc2_eac                 : return false;
        case gl::Internal_format::compressed_rgba_bptc_unorm                : return false;
        case gl::Internal_format::compressed_signed_r11_eac                 : return false;
        case gl::Internal_format::compressed_signed_red_rgtc1               : return false;
        case gl::Internal_format::compressed_signed_rg11_eac                : return false;
        case gl::Internal_format::compressed_signed_rg_rgtc2                : return false;
        case gl::Internal_format::compressed_srgb                           : return false;
        case gl::Internal_format::compressed_srgb8_alpha8_etc2_eac          : return false;
        case gl::Internal_format::compressed_srgb8_etc2                     : return false;
        case gl::Internal_format::compressed_srgb8_punchthrough_alpha1_etc2 : return false;
        case gl::Internal_format::compressed_srgb_alpha                     : return false;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm          : return false;
        case gl::Internal_format::depth24_stencil8                          : return true;
        case gl::Internal_format::depth32f_stencil8                         : return true;
        case gl::Internal_format::depth_component                           : return false;
        case gl::Internal_format::depth_component16                         : return false;
        case gl::Internal_format::depth_component32f                        : return false;
        case gl::Internal_format::depth_stencil                             : return true;
        case gl::Internal_format::r11f_g11f_b10f                            : return false;
        case gl::Internal_format::r16                                       : return false;
        case gl::Internal_format::r16_snorm                                 : return false;
        case gl::Internal_format::r16f                                      : return false;
        case gl::Internal_format::r16i                                      : return false;
        case gl::Internal_format::r16ui                                     : return false;
        case gl::Internal_format::r32f                                      : return false;
        case gl::Internal_format::r32i                                      : return false;
        case gl::Internal_format::r32ui                                     : return false;
        case gl::Internal_format::r3_g3_b2                                  : return false;
        case gl::Internal_format::r8                                        : return false;
        case gl::Internal_format::r8_snorm                                  : return false;
        case gl::Internal_format::r8i                                       : return false;
        case gl::Internal_format::r8ui                                      : return false;
        case gl::Internal_format::red                                       : return false;
        case gl::Internal_format::rg                                        : return false;
        case gl::Internal_format::rg16                                      : return false;
        case gl::Internal_format::rg16_snorm                                : return false;
        case gl::Internal_format::rg16f                                     : return false;
        case gl::Internal_format::rg16i                                     : return false;
        case gl::Internal_format::rg16ui                                    : return false;
        case gl::Internal_format::rg32f                                     : return false;
        case gl::Internal_format::rg32i                                     : return false;
        case gl::Internal_format::rg32ui                                    : return false;
        case gl::Internal_format::rg8                                       : return false;
        case gl::Internal_format::rg8_snorm                                 : return false;
        case gl::Internal_format::rg8i                                      : return false;
        case gl::Internal_format::rg8ui                                     : return false;
        case gl::Internal_format::rgb                                       : return false;
        case gl::Internal_format::rgb10                                     : return false;
        case gl::Internal_format::rgb10_a2                                  : return false;
        case gl::Internal_format::rgb10_a2ui                                : return false;
        case gl::Internal_format::rgb12                                     : return false;
        case gl::Internal_format::rgb16                                     : return false;
        case gl::Internal_format::rgb16_snorm                               : return false;
        case gl::Internal_format::rgb16f                                    : return false;
        case gl::Internal_format::rgb16i                                    : return false;
        case gl::Internal_format::rgb16ui                                   : return false;
        case gl::Internal_format::rgb32f                                    : return false;
        case gl::Internal_format::rgb32i                                    : return false;
        case gl::Internal_format::rgb32ui                                   : return false;
        case gl::Internal_format::rgb4                                      : return false;
        case gl::Internal_format::rgb5                                      : return false;
        case gl::Internal_format::rgb5_a1                                   : return false;
        case gl::Internal_format::rgb8                                      : return false;
        case gl::Internal_format::rgb8_snorm                                : return false;
        case gl::Internal_format::rgb8i                                     : return false;
        case gl::Internal_format::rgb8ui                                    : return false;
        case gl::Internal_format::rgb9_e5                                   : return false;
        case gl::Internal_format::rgba                                      : return false;
        case gl::Internal_format::rgba12                                    : return false;
        case gl::Internal_format::rgba16                                    : return false;
        case gl::Internal_format::rgba16f                                   : return false;
        case gl::Internal_format::rgba16i                                   : return false;
        case gl::Internal_format::rgba16ui                                  : return false;
        case gl::Internal_format::rgba32f                                   : return false;
        case gl::Internal_format::rgba32i                                   : return false;
        case gl::Internal_format::rgba32ui                                  : return false;
        case gl::Internal_format::rgba4                                     : return false;
        case gl::Internal_format::rgba8                                     : return false;
        case gl::Internal_format::rgba8_snorm                               : return false;
        case gl::Internal_format::rgba8i                                    : return false;
        case gl::Internal_format::rgba8ui                                   : return false;
        case gl::Internal_format::srgb                                      : return false;
        case gl::Internal_format::srgb8                                     : return false;
        case gl::Internal_format::srgb8_alpha8                              : return false;
        case gl::Internal_format::srgb_alpha                                : return false;
        case gl::Internal_format::stencil_index                             : return true;
        case gl::Internal_format::stencil_index1                            : return true;
        case gl::Internal_format::stencil_index16                           : return true;
        case gl::Internal_format::stencil_index4                            : return true;
        case gl::Internal_format::stencil_index8                            : return true;
        default: return false;
    }
}

[[nodiscard]] auto convert_to_gl_index_type(erhe::dataformat::Format format) -> std::optional<gl::Draw_elements_type>
{
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_uint:  return gl::Draw_elements_type::unsigned_byte;
        case erhe::dataformat::Format::format_16_scalar_uint: return gl::Draw_elements_type::unsigned_short;
        case erhe::dataformat::Format::format_32_scalar_uint: return gl::Draw_elements_type::unsigned_int;
        default: return {};
    }
}

[[nodiscard]] auto convert_to_gl(erhe::dataformat::Format format) -> std::optional<gl::Internal_format>
{
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_srgb:            return {};
        case erhe::dataformat::Format::format_8_scalar_unorm:           return gl::Internal_format::r8;
        case erhe::dataformat::Format::format_8_scalar_snorm:           return gl::Internal_format::r8_snorm;
        case erhe::dataformat::Format::format_8_scalar_uint:            return gl::Internal_format::r8ui;
        case erhe::dataformat::Format::format_8_scalar_sint:            return gl::Internal_format::r8i;
        case erhe::dataformat::Format::format_8_vec2_srgb:              return {};
        case erhe::dataformat::Format::format_8_vec2_unorm:             return gl::Internal_format::rg8;
        case erhe::dataformat::Format::format_8_vec2_snorm:             return gl::Internal_format::rg8_snorm;
        case erhe::dataformat::Format::format_8_vec2_uint:              return gl::Internal_format::rg8ui;
        case erhe::dataformat::Format::format_8_vec2_sint:              return gl::Internal_format::rg8i;
        case erhe::dataformat::Format::format_8_vec3_srgb:              return gl::Internal_format::srgb8;
        case erhe::dataformat::Format::format_8_vec3_unorm:             return gl::Internal_format::rgb8;
        case erhe::dataformat::Format::format_8_vec3_snorm:             return gl::Internal_format::rgb8_snorm;
        case erhe::dataformat::Format::format_8_vec3_uint:              return gl::Internal_format::rgb8ui;
        case erhe::dataformat::Format::format_8_vec3_sint:              return gl::Internal_format::rgb8i;
        case erhe::dataformat::Format::format_8_vec4_srgb:              return gl::Internal_format::srgb8_alpha8;
        case erhe::dataformat::Format::format_8_vec4_unorm:             return gl::Internal_format::rgba8;
        case erhe::dataformat::Format::format_8_vec4_snorm:             return gl::Internal_format::rgba8_snorm;
        case erhe::dataformat::Format::format_8_vec4_uint:              return gl::Internal_format::rgba8ui;
        case erhe::dataformat::Format::format_8_vec4_sint:              return gl::Internal_format::rgba8i;
        case erhe::dataformat::Format::format_16_scalar_unorm:          return gl::Internal_format::r16;
        case erhe::dataformat::Format::format_16_scalar_snorm:          return gl::Internal_format::r16_snorm;
        case erhe::dataformat::Format::format_16_scalar_uint:           return gl::Internal_format::r16ui;
        case erhe::dataformat::Format::format_16_scalar_sint:           return gl::Internal_format::r16i;
        case erhe::dataformat::Format::format_16_scalar_float:          return gl::Internal_format::r16f;
        case erhe::dataformat::Format::format_16_vec2_unorm:            return gl::Internal_format::rg16;
        case erhe::dataformat::Format::format_16_vec2_snorm:            return gl::Internal_format::rg16_snorm;
        case erhe::dataformat::Format::format_16_vec2_uint:             return gl::Internal_format::rg16ui;
        case erhe::dataformat::Format::format_16_vec2_sint:             return gl::Internal_format::rg16i;
        case erhe::dataformat::Format::format_16_vec2_float:            return gl::Internal_format::rg16f;
        case erhe::dataformat::Format::format_16_vec3_unorm:            return gl::Internal_format::rgb16;
        case erhe::dataformat::Format::format_16_vec3_snorm:            return gl::Internal_format::rgb16_snorm;
        case erhe::dataformat::Format::format_16_vec3_uint:             return gl::Internal_format::rgb16ui;
        case erhe::dataformat::Format::format_16_vec3_sint:             return gl::Internal_format::rgb16i;
        case erhe::dataformat::Format::format_16_vec3_float:            return gl::Internal_format::rgb16f;
        case erhe::dataformat::Format::format_16_vec4_unorm:            return gl::Internal_format::rgba16;
        case erhe::dataformat::Format::format_16_vec4_snorm:            return gl::Internal_format::rgba16_snorm;
        case erhe::dataformat::Format::format_16_vec4_uint:             return gl::Internal_format::rgba16ui;
        case erhe::dataformat::Format::format_16_vec4_sint:             return gl::Internal_format::rgba16i;
        case erhe::dataformat::Format::format_16_vec4_float:            return gl::Internal_format::rgba16f;
        case erhe::dataformat::Format::format_32_scalar_uint:           return gl::Internal_format::r32ui;
        case erhe::dataformat::Format::format_32_scalar_sint:           return gl::Internal_format::r32i;
        case erhe::dataformat::Format::format_32_scalar_float:          return gl::Internal_format::r32f;
        case erhe::dataformat::Format::format_32_vec2_uint:             return gl::Internal_format::rg32ui;
        case erhe::dataformat::Format::format_32_vec2_sint:             return gl::Internal_format::rg32i;
        case erhe::dataformat::Format::format_32_vec2_float:            return gl::Internal_format::rg32f;
        case erhe::dataformat::Format::format_32_vec3_uint:             return gl::Internal_format::rgb32ui;
        case erhe::dataformat::Format::format_32_vec3_sint:             return gl::Internal_format::rgb32i;
        case erhe::dataformat::Format::format_32_vec3_float:            return gl::Internal_format::rgb32f;
        case erhe::dataformat::Format::format_32_vec4_uint:             return gl::Internal_format::rgba32ui;
        case erhe::dataformat::Format::format_32_vec4_sint:             return gl::Internal_format::rgba32i;    
        case erhe::dataformat::Format::format_32_vec4_float:            return gl::Internal_format::rgba32f;
        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return gl::Internal_format::rgb10_a2;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint:  return gl::Internal_format::rgb10_a2ui;
        case erhe::dataformat::Format::format_packed111110_vec3_unorm:  return gl::Internal_format::r11f_g11f_b10f;
        // BC1..BC3 (S3TC) are extension-only enums not present in the generated core wrappers
        case erhe::dataformat::Format::format_bc4_unorm:                return gl::Internal_format::compressed_red_rgtc1;
        case erhe::dataformat::Format::format_bc4_snorm:                return gl::Internal_format::compressed_signed_red_rgtc1;
        case erhe::dataformat::Format::format_bc5_unorm:                return gl::Internal_format::compressed_rg_rgtc2;
        case erhe::dataformat::Format::format_bc5_snorm:                return gl::Internal_format::compressed_signed_rg_rgtc2;
        case erhe::dataformat::Format::format_bc6h_ufloat:              return gl::Internal_format::compressed_rgb_bptc_unsigned_float;
        case erhe::dataformat::Format::format_bc6h_sfloat:              return gl::Internal_format::compressed_rgb_bptc_signed_float;
        case erhe::dataformat::Format::format_bc7_unorm:                return gl::Internal_format::compressed_rgba_bptc_unorm;
        case erhe::dataformat::Format::format_bc7_srgb:                 return gl::Internal_format::compressed_srgb_alpha_bptc_unorm;
        case erhe::dataformat::Format::format_d16_unorm:                return gl::Internal_format::depth_component16;
        case erhe::dataformat::Format::format_x8_d24_unorm_pack32:      return gl::Internal_format::depth24_stencil8;
        case erhe::dataformat::Format::format_d32_sfloat:               return gl::Internal_format::depth_component32f;
        case erhe::dataformat::Format::format_s8_uint:                  return gl::Internal_format::stencil_index8;
        case erhe::dataformat::Format::format_d24_unorm_s8_uint:        return gl::Internal_format::depth24_stencil8;
        case erhe::dataformat::Format::format_d32_sfloat_s8_uint:       return gl::Internal_format::depth32f_stencil8;
        default: return {};
    }
}

[[nodiscard]] auto convert_from_gl(gl::Internal_format format) -> erhe::dataformat::Format
{
    switch (format) {
        case gl::Internal_format::r8                : return erhe::dataformat::Format::format_8_scalar_unorm;
        case gl::Internal_format::r8_snorm          : return erhe::dataformat::Format::format_8_scalar_snorm;
        case gl::Internal_format::r8ui              : return erhe::dataformat::Format::format_8_scalar_uint;
        case gl::Internal_format::r8i               : return erhe::dataformat::Format::format_8_scalar_sint;
        case gl::Internal_format::rg8               : return erhe::dataformat::Format::format_8_vec2_unorm;
        case gl::Internal_format::rg8_snorm         : return erhe::dataformat::Format::format_8_vec2_snorm;
        case gl::Internal_format::rg8ui             : return erhe::dataformat::Format::format_8_vec2_uint;
        case gl::Internal_format::rg8i              : return erhe::dataformat::Format::format_8_vec2_sint;
        case gl::Internal_format::rgb8              : return erhe::dataformat::Format::format_8_vec3_unorm;
        case gl::Internal_format::rgb8_snorm        : return erhe::dataformat::Format::format_8_vec3_snorm;
        case gl::Internal_format::rgb8ui            : return erhe::dataformat::Format::format_8_vec3_uint;
        case gl::Internal_format::rgb8i             : return erhe::dataformat::Format::format_8_vec3_sint;
        case gl::Internal_format::srgb8_alpha8      : return erhe::dataformat::Format::format_8_vec4_srgb;
        case gl::Internal_format::rgba8             : return erhe::dataformat::Format::format_8_vec4_unorm;
        case gl::Internal_format::rgba8_snorm       : return erhe::dataformat::Format::format_8_vec4_snorm;
        case gl::Internal_format::rgba8ui           : return erhe::dataformat::Format::format_8_vec4_uint;
        case gl::Internal_format::rgba8i            : return erhe::dataformat::Format::format_8_vec4_sint;
        case gl::Internal_format::r16               : return erhe::dataformat::Format::format_16_scalar_unorm;
        case gl::Internal_format::r16_snorm         : return erhe::dataformat::Format::format_16_scalar_snorm;
        case gl::Internal_format::r16ui             : return erhe::dataformat::Format::format_16_scalar_uint;
        case gl::Internal_format::r16i              : return erhe::dataformat::Format::format_16_scalar_sint;
        case gl::Internal_format::r16f              : return erhe::dataformat::Format::format_16_scalar_float;
        case gl::Internal_format::rg16              : return erhe::dataformat::Format::format_16_vec2_unorm;
        case gl::Internal_format::rg16_snorm        : return erhe::dataformat::Format::format_16_vec2_snorm;
        case gl::Internal_format::rg16ui            : return erhe::dataformat::Format::format_16_vec2_uint;
        case gl::Internal_format::rg16i             : return erhe::dataformat::Format::format_16_vec2_sint;
        case gl::Internal_format::rg16f             : return erhe::dataformat::Format::format_16_vec2_float;
        case gl::Internal_format::rgb16             : return erhe::dataformat::Format::format_16_vec3_unorm;
        case gl::Internal_format::rgb16_snorm       : return erhe::dataformat::Format::format_16_vec3_snorm;
        case gl::Internal_format::rgb16ui           : return erhe::dataformat::Format::format_16_vec3_uint;
        case gl::Internal_format::rgb16i            : return erhe::dataformat::Format::format_16_vec3_sint;
        case gl::Internal_format::rgb16f            : return erhe::dataformat::Format::format_16_vec3_float;
        case gl::Internal_format::rgba16            : return erhe::dataformat::Format::format_16_vec4_unorm;
        case gl::Internal_format::rgba16_snorm      : return erhe::dataformat::Format::format_16_vec4_snorm;
        case gl::Internal_format::rgba16ui          : return erhe::dataformat::Format::format_16_vec4_uint;
        case gl::Internal_format::rgba16i           : return erhe::dataformat::Format::format_16_vec4_sint;
        case gl::Internal_format::rgba16f           : return erhe::dataformat::Format::format_16_vec4_float;
        case gl::Internal_format::r32ui             : return erhe::dataformat::Format::format_32_scalar_uint;
        case gl::Internal_format::r32i              : return erhe::dataformat::Format::format_32_scalar_sint;
        case gl::Internal_format::r32f              : return erhe::dataformat::Format::format_32_scalar_float;
        case gl::Internal_format::rg32ui            : return erhe::dataformat::Format::format_32_vec2_uint;
        case gl::Internal_format::rg32i             : return erhe::dataformat::Format::format_32_vec2_sint;
        case gl::Internal_format::rg32f             : return erhe::dataformat::Format::format_32_vec2_float;
        case gl::Internal_format::rgb32ui           : return erhe::dataformat::Format::format_32_vec3_uint;
        case gl::Internal_format::rgb32i            : return erhe::dataformat::Format::format_32_vec3_sint;
        case gl::Internal_format::rgb32f            : return erhe::dataformat::Format::format_32_vec3_float;
        case gl::Internal_format::rgba32ui          : return erhe::dataformat::Format::format_32_vec4_uint;
        case gl::Internal_format::rgba32i           : return erhe::dataformat::Format::format_32_vec4_sint;
        case gl::Internal_format::rgba32f           : return erhe::dataformat::Format::format_32_vec4_float;
        case gl::Internal_format::rgb10_a2          : return erhe::dataformat::Format::format_packed1010102_vec4_unorm;
        case gl::Internal_format::rgb10_a2ui        : return erhe::dataformat::Format::format_packed1010102_vec4_uint;
        case gl::Internal_format::r11f_g11f_b10f    : return erhe::dataformat::Format::format_packed111110_vec3_unorm;
        case gl::Internal_format::compressed_red_rgtc1               : return erhe::dataformat::Format::format_bc4_unorm;
        case gl::Internal_format::compressed_signed_red_rgtc1        : return erhe::dataformat::Format::format_bc4_snorm;
        case gl::Internal_format::compressed_rg_rgtc2                : return erhe::dataformat::Format::format_bc5_unorm;
        case gl::Internal_format::compressed_signed_rg_rgtc2         : return erhe::dataformat::Format::format_bc5_snorm;
        case gl::Internal_format::compressed_rgb_bptc_unsigned_float : return erhe::dataformat::Format::format_bc6h_ufloat;
        case gl::Internal_format::compressed_rgb_bptc_signed_float   : return erhe::dataformat::Format::format_bc6h_sfloat;
        case gl::Internal_format::compressed_rgba_bptc_unorm         : return erhe::dataformat::Format::format_bc7_unorm;
        case gl::Internal_format::compressed_srgb_alpha_bptc_unorm   : return erhe::dataformat::Format::format_bc7_srgb;
        case gl::Internal_format::depth_component16 : return erhe::dataformat::Format::format_d16_unorm;
        //case gl::Internal_format::depth32f_stencil8 : return erhe::dataformat::Format::format_x8_d24_unorm_pack32;
        case gl::Internal_format::depth_component32f: return erhe::dataformat::Format::format_d32_sfloat;
        case gl::Internal_format::stencil_index8    : return erhe::dataformat::Format::format_s8_uint;
        case gl::Internal_format::depth24_stencil8  : return erhe::dataformat::Format::format_d24_unorm_s8_uint;
        case gl::Internal_format::depth32f_stencil8 : return erhe::dataformat::Format::format_d32_sfloat_s8_uint;
        default: return erhe::dataformat::Format::format_undefined;
    }
}
} // namespace gl
