#include "erhe_graphics/metal/metal_helpers.hpp"

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
        case F::format_8_vec4_srgb:     return MTL::PixelFormatRGBA8Unorm_sRGB;
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
        case F::format_d16_unorm:           return MTL::PixelFormatDepth16Unorm;
        case F::format_d32_sfloat:          return MTL::PixelFormatDepth32Float;
        case F::format_s8_uint:             return MTL::PixelFormatStencil8;
        case F::format_d32_sfloat_s8_uint:  return MTL::PixelFormatDepth32Float_Stencil8;
        case F::format_d24_unorm_s8_uint:   return MTL::PixelFormatDepth24Unorm_Stencil8;
        default: return MTL::PixelFormatInvalid;
    }
}

auto to_mtl_texture_type(const Texture_type type, const int sample_count, const int array_layer_count) -> MTL::TextureType
{
    const bool is_array       = array_layer_count > 0;
    const bool is_multisample = sample_count > 1;

    switch (type) {
        case Texture_type::texture_1d:
            return is_array ? MTL::TextureType1DArray : MTL::TextureType1D;
        case Texture_type::texture_2d:
            if (is_multisample) {
                return is_array ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DMultisample;
            }
            return is_array ? MTL::TextureType2DArray : MTL::TextureType2D;
        case Texture_type::texture_3d:
            return MTL::TextureType3D;
        case Texture_type::texture_cube_map:
            return is_array ? MTL::TextureTypeCubeArray : MTL::TextureTypeCube;
        default:
            return MTL::TextureType2D;
    }
}

} // namespace erhe::graphics
