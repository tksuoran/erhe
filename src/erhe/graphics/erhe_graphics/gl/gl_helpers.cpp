#pragma once

#include "erhe_graphics/gl/gl_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

auto to_gl_index_type(const erhe::dataformat::Format format) -> gl::Draw_elements_type
{
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_uint:  return gl::Draw_elements_type::unsigned_byte;
        case erhe::dataformat::Format::format_16_scalar_uint: return gl::Draw_elements_type::unsigned_short;
        case erhe::dataformat::Format::format_32_scalar_uint: return gl::Draw_elements_type::unsigned_int;
        default: {
            ERHE_FATAL("Bad index format");
        }
    }
}

auto to_gl(const Blend_equation_mode equation_mode) -> gl::Blend_equation_mode
{
    switch (equation_mode) {
        case Blend_equation_mode::func_add              : return gl::Blend_equation_mode::func_add             ;
        case Blend_equation_mode::func_reverse_subtract : return gl::Blend_equation_mode::func_reverse_subtract;
        case Blend_equation_mode::func_subtract         : return gl::Blend_equation_mode::func_subtract        ;
        case Blend_equation_mode::max_                  : return gl::Blend_equation_mode::max_                 ;
        case Blend_equation_mode::min_                  : return gl::Blend_equation_mode::min_                 ;
        default: {
            ERHE_FATAL("Bad Blend_equation_mode %u", static_cast<unsigned int>(equation_mode));
        }
    }
}

auto to_gl(const Blending_factor blending_factor) -> gl::Blending_factor
{
    switch (blending_factor) {
        case Blending_factor::zero                    : return gl::Blending_factor::zero                    ;
        case Blending_factor::constant_alpha          : return gl::Blending_factor::constant_alpha          ;
        case Blending_factor::constant_color          : return gl::Blending_factor::constant_color          ;
        case Blending_factor::dst_alpha               : return gl::Blending_factor::dst_alpha               ;
        case Blending_factor::dst_color               : return gl::Blending_factor::dst_color               ;
        case Blending_factor::one                     : return gl::Blending_factor::one                     ;
        case Blending_factor::one_minus_constant_alpha: return gl::Blending_factor::one_minus_constant_alpha;
        case Blending_factor::one_minus_constant_color: return gl::Blending_factor::one_minus_constant_color;
        case Blending_factor::one_minus_dst_alpha     : return gl::Blending_factor::one_minus_dst_alpha     ;
        case Blending_factor::one_minus_dst_color     : return gl::Blending_factor::one_minus_dst_color     ;
        case Blending_factor::one_minus_src1_alpha    : return gl::Blending_factor::one_minus_src1_alpha    ;
        case Blending_factor::one_minus_src1_color    : return gl::Blending_factor::one_minus_src1_color    ;
        case Blending_factor::one_minus_src_alpha     : return gl::Blending_factor::one_minus_src_alpha     ;
        case Blending_factor::one_minus_src_color     : return gl::Blending_factor::one_minus_src_color     ;
        case Blending_factor::src1_alpha              : return gl::Blending_factor::src1_alpha              ;
        case Blending_factor::src1_color              : return gl::Blending_factor::src1_color              ;
        case Blending_factor::src_alpha               : return gl::Blending_factor::src_alpha               ;
        case Blending_factor::src_alpha_saturate      : return gl::Blending_factor::src_alpha_saturate      ;
        case Blending_factor::src_color               : return gl::Blending_factor::src_color               ;
        default: {
            ERHE_FATAL("Bad Blending_factor %u", static_cast<unsigned int>(blending_factor));
        }
    }
}
auto to_gl_depth_function(const Compare_operation compare_operation) -> gl::Depth_function
{
    switch (compare_operation) {
        case Compare_operation::never           : return gl::Depth_function::never   ;
        case Compare_operation::less            : return gl::Depth_function::less    ;
        case Compare_operation::equal           : return gl::Depth_function::equal   ;
        case Compare_operation::less_or_equal   : return gl::Depth_function::lequal  ;
        case Compare_operation::greater         : return gl::Depth_function::greater ;
        case Compare_operation::not_equal       : return gl::Depth_function::notequal;
        case Compare_operation::greater_or_equal: return gl::Depth_function::gequal  ;
        case Compare_operation::always          : return gl::Depth_function::always  ;
        default: {
            ERHE_FATAL("Bad compare operation %u", static_cast<unsigned int>(compare_operation));
        }
    }
}
auto to_gl_stencil_function(const Compare_operation compare_operation) -> gl::Stencil_function
{
    switch (compare_operation) {
        case Compare_operation::never           : return gl::Stencil_function::never   ;
        case Compare_operation::less            : return gl::Stencil_function::less    ;
        case Compare_operation::equal           : return gl::Stencil_function::equal   ;
        case Compare_operation::less_or_equal   : return gl::Stencil_function::lequal  ;
        case Compare_operation::greater         : return gl::Stencil_function::greater ;
        case Compare_operation::not_equal       : return gl::Stencil_function::notequal;
        case Compare_operation::greater_or_equal: return gl::Stencil_function::gequal  ;
        case Compare_operation::always          : return gl::Stencil_function::always  ;
        default: {
            ERHE_FATAL("Bad compare operation %u", static_cast<unsigned int>(compare_operation));
        }
    }
}
auto to_gl(const Cull_face_mode cull_face_mode) -> gl::Cull_face_mode
{
    switch (cull_face_mode) {
        case Cull_face_mode::back          : return gl::Cull_face_mode::back          ;
        case Cull_face_mode::front         : return gl::Cull_face_mode::front         ;
        case Cull_face_mode::front_and_back: return gl::Cull_face_mode::front_and_back;
        default: {
            ERHE_FATAL("Bad cull_face_mode %u", static_cast<unsigned int>(cull_face_mode));
        }
    }
}
auto to_gl(const Front_face_direction front_face_direction) -> gl::Front_face_direction
{
    switch (front_face_direction) {
        case Front_face_direction::ccw: return gl::Front_face_direction::ccw;
        case Front_face_direction::cw : return gl::Front_face_direction::cw ;
        default: {
            ERHE_FATAL("Bad front_face_direction %u", static_cast<unsigned int>(front_face_direction));
        }
    }
}
auto to_gl(const Polygon_mode polygon_mode) -> gl::Polygon_mode
{
    switch (polygon_mode) {
        case Polygon_mode::fill : return gl::Polygon_mode::fill ;
        case Polygon_mode::line : return gl::Polygon_mode::line ;
        case Polygon_mode::point: return gl::Polygon_mode::point;
        default: {
            ERHE_FATAL("Bad polygon_mode %u", static_cast<unsigned int>(polygon_mode));
        }
    }
}
auto to_gl(const Shader_type type) -> gl::Shader_type
{
    switch (type) {
        case Shader_type::vertex_shader         : return gl::Shader_type::vertex_shader         ;
        case Shader_type::fragment_shader       : return gl::Shader_type::fragment_shader       ;
        case Shader_type::compute_shader        : return gl::Shader_type::compute_shader        ;
        case Shader_type::geometry_shader       : return gl::Shader_type::geometry_shader       ;
        case Shader_type::tess_control_shader   : return gl::Shader_type::tess_control_shader   ;
        case Shader_type::tess_evaluation_shader: return gl::Shader_type::tess_evaluation_shader;
        default: {
            ERHE_FATAL("Bad shader type %u", static_cast<unsigned int>(type));
        }
    }
}
auto to_gl(const Stencil_face_direction stencil_face_direction) -> gl::Stencil_face_direction
{
    switch (stencil_face_direction) {
        case Stencil_face_direction::back          : return gl::Stencil_face_direction::back          ;
        case Stencil_face_direction::front         : return gl::Stencil_face_direction::front         ;
        case Stencil_face_direction::front_and_back: return gl::Stencil_face_direction::front_and_back;
        default: {
            ERHE_FATAL("Bad stencil_face_direction %u", static_cast<unsigned int>(stencil_face_direction));
        }
    }
}
auto to_gl(const Stencil_op stencil_op) -> gl::Stencil_op
{
    switch (stencil_op) {
        case Stencil_op::zero     : return gl::Stencil_op::zero     ;
        case Stencil_op::decr     : return gl::Stencil_op::decr     ;
        case Stencil_op::decr_wrap: return gl::Stencil_op::decr_wrap;
        case Stencil_op::incr     : return gl::Stencil_op::incr     ;
        case Stencil_op::incr_wrap: return gl::Stencil_op::incr_wrap;
        case Stencil_op::invert   : return gl::Stencil_op::invert   ;
        case Stencil_op::keep     : return gl::Stencil_op::keep     ;
        case Stencil_op::replace  : return gl::Stencil_op::replace  ;
        default: {
            ERHE_FATAL("Bad stencil_op %u", static_cast<unsigned int>(stencil_op));
        }
    }
}

} // namespace erhe::graphics
