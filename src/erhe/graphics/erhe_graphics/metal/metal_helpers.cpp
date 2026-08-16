#include "erhe_graphics/metal/metal_helpers.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

auto to_mtl_pixel_format(const erhe::dataformat::Format format) -> MTL::PixelFormat
{
    using F = erhe::dataformat::Format;
    switch (format) {
        case F::format_8_scalar_unorm:  return MTL::PixelFormatR8Unorm;
        case F::format_8_scalar_snorm:  return MTL::PixelFormatR8Snorm;
        case F::format_8_scalar_uint:   return MTL::PixelFormatR8Uint;
        case F::format_8_scalar_sint:   return MTL::PixelFormatR8Sint;
        case F::format_8_vec2_unorm:    return MTL::PixelFormatRG8Unorm;
        case F::format_8_vec2_snorm:    return MTL::PixelFormatRG8Snorm;
        case F::format_8_vec2_uint:     return MTL::PixelFormatRG8Uint;
        case F::format_8_vec2_sint:     return MTL::PixelFormatRG8Sint;
        case F::format_8_vec4_srgb:      return MTL::PixelFormatRGBA8Unorm_sRGB;
        case F::format_8_vec4_bgra_srgb: return MTL::PixelFormatBGRA8Unorm_sRGB;
        case F::format_8_vec4_bgra_unorm: return MTL::PixelFormatBGRA8Unorm;
        case F::format_8_vec4_unorm:    return MTL::PixelFormatRGBA8Unorm;
        case F::format_8_vec4_snorm:    return MTL::PixelFormatRGBA8Snorm;
        case F::format_8_vec4_uint:     return MTL::PixelFormatRGBA8Uint;
        case F::format_8_vec4_sint:     return MTL::PixelFormatRGBA8Sint;
        case F::format_16_scalar_unorm: return MTL::PixelFormatR16Unorm;
        case F::format_16_scalar_snorm: return MTL::PixelFormatR16Snorm;
        case F::format_16_scalar_uint:  return MTL::PixelFormatR16Uint;
        case F::format_16_scalar_sint:  return MTL::PixelFormatR16Sint;
        case F::format_16_scalar_float: return MTL::PixelFormatR16Float;
        case F::format_16_vec2_unorm:   return MTL::PixelFormatRG16Unorm;
        case F::format_16_vec2_snorm:   return MTL::PixelFormatRG16Snorm;
        case F::format_16_vec2_uint:    return MTL::PixelFormatRG16Uint;
        case F::format_16_vec2_sint:    return MTL::PixelFormatRG16Sint;
        case F::format_16_vec2_float:   return MTL::PixelFormatRG16Float;
        case F::format_16_vec4_unorm:   return MTL::PixelFormatRGBA16Unorm;
        case F::format_16_vec4_snorm:   return MTL::PixelFormatRGBA16Snorm;
        case F::format_16_vec4_uint:    return MTL::PixelFormatRGBA16Uint;
        case F::format_16_vec4_sint:    return MTL::PixelFormatRGBA16Sint;
        case F::format_16_vec4_float:   return MTL::PixelFormatRGBA16Float;
        case F::format_32_scalar_uint:  return MTL::PixelFormatR32Uint;
        case F::format_32_scalar_sint:  return MTL::PixelFormatR32Sint;
        case F::format_32_scalar_float: return MTL::PixelFormatR32Float;
        case F::format_32_vec2_uint:    return MTL::PixelFormatRG32Uint;
        case F::format_32_vec2_sint:    return MTL::PixelFormatRG32Sint;
        case F::format_32_vec2_float:   return MTL::PixelFormatRG32Float;
        case F::format_32_vec4_uint:    return MTL::PixelFormatRGBA32Uint;
        case F::format_32_vec4_sint:    return MTL::PixelFormatRGBA32Sint;
        case F::format_32_vec4_float:   return MTL::PixelFormatRGBA32Float;
        case F::format_packed1010102_vec4_unorm: return MTL::PixelFormatRGB10A2Unorm;
        case F::format_packed1010102_vec4_uint:  return MTL::PixelFormatRGB10A2Uint;
        case F::format_packed111110_vec3_unorm:  return MTL::PixelFormatRG11B10Float;
        case F::format_bc1_rgb_unorm:   return MTL::PixelFormatBC1_RGBA;
        case F::format_bc1_rgb_srgb:    return MTL::PixelFormatBC1_RGBA_sRGB;
        case F::format_bc1_rgba_unorm:  return MTL::PixelFormatBC1_RGBA;
        case F::format_bc1_rgba_srgb:   return MTL::PixelFormatBC1_RGBA_sRGB;
        case F::format_bc2_unorm:       return MTL::PixelFormatBC2_RGBA;
        case F::format_bc2_srgb:        return MTL::PixelFormatBC2_RGBA_sRGB;
        case F::format_bc3_unorm:       return MTL::PixelFormatBC3_RGBA;
        case F::format_bc3_srgb:        return MTL::PixelFormatBC3_RGBA_sRGB;
        case F::format_bc4_unorm:       return MTL::PixelFormatBC4_RUnorm;
        case F::format_bc4_snorm:       return MTL::PixelFormatBC4_RSnorm;
        case F::format_bc5_unorm:       return MTL::PixelFormatBC5_RGUnorm;
        case F::format_bc5_snorm:       return MTL::PixelFormatBC5_RGSnorm;
        case F::format_bc6h_ufloat:     return MTL::PixelFormatBC6H_RGBUfloat;
        case F::format_bc6h_sfloat:     return MTL::PixelFormatBC6H_RGBFloat;
        case F::format_bc7_unorm:       return MTL::PixelFormatBC7_RGBAUnorm;
        case F::format_bc7_srgb:        return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
        case F::format_astc_4x4_unorm:  return MTL::PixelFormatASTC_4x4_LDR;
        case F::format_astc_4x4_srgb:   return MTL::PixelFormatASTC_4x4_sRGB;
        case F::format_d16_unorm:           return MTL::PixelFormatDepth16Unorm;
        case F::format_d32_sfloat:          return MTL::PixelFormatDepth32Float;
        case F::format_s8_uint:             return MTL::PixelFormatStencil8;
        case F::format_d32_sfloat_s8_uint:  return MTL::PixelFormatDepth32Float_Stencil8;
        case F::format_d24_unorm_s8_uint:   return MTL::PixelFormatDepth24Unorm_Stencil8;
        default: return MTL::PixelFormatInvalid;
    }
}

auto to_mtl_texture_type(const Texture_type type, const int sample_count) -> MTL::TextureType
{
    const bool is_multisample = sample_count > 1;

    switch (type) {
        case Texture_type::texture_1d:
            return MTL::TextureType1D;
        case Texture_type::texture_2d:
            return is_multisample ? MTL::TextureType2DMultisample : MTL::TextureType2D;
        case Texture_type::texture_2d_array:
            return is_multisample ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DArray;
        case Texture_type::texture_3d:
            return MTL::TextureType3D;
        case Texture_type::texture_cube_map:
            return MTL::TextureTypeCube;
        case Texture_type::texture_cube_map_array:
            return MTL::TextureTypeCubeArray;
        default:
            return MTL::TextureType2D;
    }
}

auto to_mtl_vertex_format(const erhe::dataformat::Format format) -> MTL::VertexFormat
{
    using F = erhe::dataformat::Format;
    switch (format) {
        case F::format_8_scalar_uint:    return MTL::VertexFormatUChar;
        case F::format_8_vec2_uint:      return MTL::VertexFormatUChar2;
        case F::format_8_vec3_uint:      return MTL::VertexFormatUChar3;
        case F::format_8_vec4_uint:      return MTL::VertexFormatUChar4;
        case F::format_8_scalar_sint:    return MTL::VertexFormatChar;
        case F::format_8_vec2_sint:      return MTL::VertexFormatChar2;
        case F::format_8_vec3_sint:      return MTL::VertexFormatChar3;
        case F::format_8_vec4_sint:      return MTL::VertexFormatChar4;
        case F::format_8_vec2_unorm:     return MTL::VertexFormatUChar2Normalized;
        case F::format_8_vec4_unorm:     return MTL::VertexFormatUChar4Normalized;
        case F::format_8_vec2_snorm:     return MTL::VertexFormatChar2Normalized;
        case F::format_8_vec4_snorm:     return MTL::VertexFormatChar4Normalized;
        case F::format_16_scalar_uint:   return MTL::VertexFormatUShort;
        case F::format_16_vec2_uint:     return MTL::VertexFormatUShort2;
        case F::format_16_vec3_uint:     return MTL::VertexFormatUShort3;
        case F::format_16_vec4_uint:     return MTL::VertexFormatUShort4;
        case F::format_16_scalar_sint:   return MTL::VertexFormatShort;
        case F::format_16_vec2_sint:     return MTL::VertexFormatShort2;
        case F::format_16_vec3_sint:     return MTL::VertexFormatShort3;
        case F::format_16_vec4_sint:     return MTL::VertexFormatShort4;
        case F::format_16_scalar_float:  return MTL::VertexFormatHalf;
        case F::format_16_vec2_float:    return MTL::VertexFormatHalf2;
        case F::format_16_vec4_float:    return MTL::VertexFormatHalf4;
        case F::format_16_scalar_unorm:  return MTL::VertexFormatUShortNormalized;
        case F::format_16_vec2_unorm:    return MTL::VertexFormatUShort2Normalized;
        case F::format_16_vec4_unorm:    return MTL::VertexFormatUShort4Normalized;
        case F::format_16_scalar_snorm:  return MTL::VertexFormatShortNormalized;
        case F::format_16_vec2_snorm:    return MTL::VertexFormatShort2Normalized;
        case F::format_16_vec4_snorm:    return MTL::VertexFormatShort4Normalized;
        case F::format_32_scalar_float:  return MTL::VertexFormatFloat;
        case F::format_32_vec2_float:    return MTL::VertexFormatFloat2;
        case F::format_32_vec3_float:    return MTL::VertexFormatFloat3;
        case F::format_32_vec4_float:    return MTL::VertexFormatFloat4;
        case F::format_32_scalar_sint:   return MTL::VertexFormatInt;
        case F::format_32_vec2_sint:     return MTL::VertexFormatInt2;
        case F::format_32_vec3_sint:     return MTL::VertexFormatInt3;
        case F::format_32_vec4_sint:     return MTL::VertexFormatInt4;
        case F::format_32_scalar_uint:   return MTL::VertexFormatUInt;
        case F::format_32_vec2_uint:     return MTL::VertexFormatUInt2;
        case F::format_32_vec3_uint:     return MTL::VertexFormatUInt3;
        case F::format_32_vec4_uint:     return MTL::VertexFormatUInt4;
        case F::format_packed1010102_vec4_snorm: return MTL::VertexFormatInt1010102Normalized;
        case F::format_packed1010102_vec4_unorm: return MTL::VertexFormatUInt1010102Normalized;
        default:
            ERHE_FATAL("Unsupported vertex format %u", static_cast<unsigned int>(format));
    }
}

auto to_mtl_compare_function(const Compare_operation op) -> MTL::CompareFunction
{
    switch (op) {
        case Compare_operation::never:            return MTL::CompareFunctionNever;
        case Compare_operation::less:             return MTL::CompareFunctionLess;
        case Compare_operation::equal:            return MTL::CompareFunctionEqual;
        case Compare_operation::less_or_equal:    return MTL::CompareFunctionLessEqual;
        case Compare_operation::greater:          return MTL::CompareFunctionGreater;
        case Compare_operation::not_equal:        return MTL::CompareFunctionNotEqual;
        case Compare_operation::greater_or_equal: return MTL::CompareFunctionGreaterEqual;
        case Compare_operation::always:           return MTL::CompareFunctionAlways;
        default:                                  return MTL::CompareFunctionAlways;
    }
}

auto to_mtl_blend_factor(const Blending_factor factor) -> MTL::BlendFactor
{
    switch (factor) {
        case Blending_factor::zero:                     return MTL::BlendFactorZero;
        case Blending_factor::one:                      return MTL::BlendFactorOne;
        case Blending_factor::src_color:                return MTL::BlendFactorSourceColor;
        case Blending_factor::one_minus_src_color:      return MTL::BlendFactorOneMinusSourceColor;
        case Blending_factor::dst_color:                return MTL::BlendFactorDestinationColor;
        case Blending_factor::one_minus_dst_color:      return MTL::BlendFactorOneMinusDestinationColor;
        case Blending_factor::src_alpha:                return MTL::BlendFactorSourceAlpha;
        case Blending_factor::one_minus_src_alpha:      return MTL::BlendFactorOneMinusSourceAlpha;
        case Blending_factor::dst_alpha:                return MTL::BlendFactorDestinationAlpha;
        case Blending_factor::one_minus_dst_alpha:      return MTL::BlendFactorOneMinusDestinationAlpha;
        case Blending_factor::constant_color:           return MTL::BlendFactorBlendColor;
        case Blending_factor::one_minus_constant_color: return MTL::BlendFactorOneMinusBlendColor;
        case Blending_factor::constant_alpha:           return MTL::BlendFactorBlendAlpha;
        case Blending_factor::one_minus_constant_alpha: return MTL::BlendFactorOneMinusBlendAlpha;
        case Blending_factor::src_alpha_saturate:       return MTL::BlendFactorSourceAlphaSaturated;
        case Blending_factor::src1_color:               return MTL::BlendFactorSource1Color;
        case Blending_factor::one_minus_src1_color:     return MTL::BlendFactorOneMinusSource1Color;
        case Blending_factor::src1_alpha:               return MTL::BlendFactorSource1Alpha;
        case Blending_factor::one_minus_src1_alpha:     return MTL::BlendFactorOneMinusSource1Alpha;
        default:                                        return MTL::BlendFactorOne;
    }
}

auto to_mtl_blend_operation(const Blend_equation_mode mode) -> MTL::BlendOperation
{
    switch (mode) {
        case Blend_equation_mode::func_add:              return MTL::BlendOperationAdd;
        case Blend_equation_mode::func_subtract:         return MTL::BlendOperationSubtract;
        case Blend_equation_mode::func_reverse_subtract: return MTL::BlendOperationReverseSubtract;
        case Blend_equation_mode::min_:                  return MTL::BlendOperationMin;
        case Blend_equation_mode::max_:                  return MTL::BlendOperationMax;
        default:                                         return MTL::BlendOperationAdd;
    }
}

auto to_mtl_stencil_operation(const Stencil_op op) -> MTL::StencilOperation
{
    switch (op) {
        case Stencil_op::zero:      return MTL::StencilOperationZero;
        case Stencil_op::keep:      return MTL::StencilOperationKeep;
        case Stencil_op::replace:   return MTL::StencilOperationReplace;
        case Stencil_op::incr:      return MTL::StencilOperationIncrementClamp;
        case Stencil_op::incr_wrap: return MTL::StencilOperationIncrementWrap;
        case Stencil_op::decr:      return MTL::StencilOperationDecrementClamp;
        case Stencil_op::decr_wrap: return MTL::StencilOperationDecrementWrap;
        case Stencil_op::invert:    return MTL::StencilOperationInvert;
        default:                    return MTL::StencilOperationKeep;
    }
}

auto to_mtl_winding(const Front_face_direction direction) -> MTL::Winding
{
    switch (direction) {
        case Front_face_direction::ccw: return MTL::WindingCounterClockwise;
        case Front_face_direction::cw:  return MTL::WindingClockwise;
        default:                        return MTL::WindingCounterClockwise;
    }
}

auto to_mtl_cull_mode(const bool face_cull_enable, const Cull_face_mode mode) -> MTL::CullMode
{
    if (!face_cull_enable) {
        return MTL::CullModeNone;
    }
    switch (mode) {
        case Cull_face_mode::front:          return MTL::CullModeFront;
        case Cull_face_mode::back:           return MTL::CullModeBack;
        case Cull_face_mode::front_and_back: return MTL::CullModeNone;
        default:                             return MTL::CullModeNone;
    }
}

auto to_mtl_triangle_fill_mode(const Polygon_mode mode) -> MTL::TriangleFillMode
{
    switch (mode) {
        case Polygon_mode::fill:  return MTL::TriangleFillModeFill;
        case Polygon_mode::line:  return MTL::TriangleFillModeLines;
        // Metal has no point fill mode. Point rendering must go through
        // Primitive_type::point at draw time; fall back to Fill here.
        case Polygon_mode::point: return MTL::TriangleFillModeFill;
        default:                  return MTL::TriangleFillModeFill;
    }
}

auto to_mtl_depth_clip_mode(const bool depth_clamp_enable) -> MTL::DepthClipMode
{
    return depth_clamp_enable ? MTL::DepthClipModeClamp : MTL::DepthClipModeClip;
}

auto to_mtl_depth_resolve_filter(const Resolve_mode mode) -> MTL::MultisampleDepthResolveFilter
{
    switch (mode) {
        case Resolve_mode::sample_zero: return MTL::MultisampleDepthResolveFilterSample0;
        case Resolve_mode::min:         return MTL::MultisampleDepthResolveFilterMin;
        case Resolve_mode::max:         return MTL::MultisampleDepthResolveFilterMax;
        // Metal has no depth average filter; sample_zero is the safest fallback.
        case Resolve_mode::average:     return MTL::MultisampleDepthResolveFilterSample0;
        default:                        return MTL::MultisampleDepthResolveFilterSample0;
    }
}

auto to_mtl_stencil_resolve_filter(const Resolve_mode mode) -> MTL::MultisampleStencilResolveFilter
{
    // Stencil only has Sample0 / DepthResolvedSample. We don't expose
    // DepthResolvedSample through the cross-API enum (no Vulkan analogue), so
    // every requested mode collapses to Sample0.
    static_cast<void>(mode);
    return MTL::MultisampleStencilResolveFilterSample0;
}

} // namespace erhe::graphics
