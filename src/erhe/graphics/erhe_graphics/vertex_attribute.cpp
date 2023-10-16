#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics
{

auto get_dimension(igl::VertexAttributeFormat format) -> std::size_t
{
    switch (format)
    {
        case igl::VertexAttributeFormat::Float1:             return 1;
        case igl::VertexAttributeFormat::Float2:             return 2;
        case igl::VertexAttributeFormat::Float3:             return 3;
        case igl::VertexAttributeFormat::Float4:             return 4;
        case igl::VertexAttributeFormat::Byte1:              return 1;
        case igl::VertexAttributeFormat::Byte2:              return 2;
        case igl::VertexAttributeFormat::Byte3:              return 3;
        case igl::VertexAttributeFormat::Byte4:              return 4;
        case igl::VertexAttributeFormat::UByte1:             return 1;
        case igl::VertexAttributeFormat::UByte2:             return 2;
        case igl::VertexAttributeFormat::UByte3:             return 3;
        case igl::VertexAttributeFormat::UByte4:             return 4;
        case igl::VertexAttributeFormat::Short1:             return 1;
        case igl::VertexAttributeFormat::Short2:             return 2;
        case igl::VertexAttributeFormat::Short3:             return 3;
        case igl::VertexAttributeFormat::Short4:             return 4;
        case igl::VertexAttributeFormat::UShort1:            return 1;
        case igl::VertexAttributeFormat::UShort2:            return 2;
        case igl::VertexAttributeFormat::UShort3:            return 3;
        case igl::VertexAttributeFormat::UShort4:            return 4;
        case igl::VertexAttributeFormat::Byte2Norm:          return 2;
        case igl::VertexAttributeFormat::Byte4Norm:          return 4;
        case igl::VertexAttributeFormat::UByte2Norm:         return 2;
        case igl::VertexAttributeFormat::UByte4Norm:         return 4;
        case igl::VertexAttributeFormat::Short2Norm:         return 2;
        case igl::VertexAttributeFormat::Short4Norm:         return 4;
        case igl::VertexAttributeFormat::UShort2Norm:        return 2;
        case igl::VertexAttributeFormat::UShort4Norm:        return 4;
        case igl::VertexAttributeFormat::Int1:               return 1;
        case igl::VertexAttributeFormat::Int2:               return 2;
        case igl::VertexAttributeFormat::Int3:               return 3;
        case igl::VertexAttributeFormat::Int4:               return 4;
        case igl::VertexAttributeFormat::UInt1:              return 1;
        case igl::VertexAttributeFormat::UInt2:              return 2;
        case igl::VertexAttributeFormat::UInt3:              return 3;
        case igl::VertexAttributeFormat::UInt4:              return 4;
        case igl::VertexAttributeFormat::HalfFloat1:         return 1;
        case igl::VertexAttributeFormat::HalfFloat2:         return 2;
        case igl::VertexAttributeFormat::HalfFloat3:         return 3;
        case igl::VertexAttributeFormat::HalfFloat4:         return 4;
        case igl::VertexAttributeFormat::Int_2_10_10_10_REV: return 4;
        default: return 0;
    }
}

auto get_size(igl::VertexAttributeFormat format) -> std::size_t
{
    switch (format)
    {
        case igl::VertexAttributeFormat::Float1:             return 1 * sizeof(float);
        case igl::VertexAttributeFormat::Float2:             return 2 * sizeof(float);
        case igl::VertexAttributeFormat::Float3:             return 3 * sizeof(float);
        case igl::VertexAttributeFormat::Float4:             return 4 * sizeof(float);
        case igl::VertexAttributeFormat::Byte1:              return 1 * sizeof(int8_t);
        case igl::VertexAttributeFormat::Byte2:              return 2 * sizeof(int8_t);
        case igl::VertexAttributeFormat::Byte3:              return 3 * sizeof(int8_t);
        case igl::VertexAttributeFormat::Byte4:              return 4 * sizeof(int8_t);
        case igl::VertexAttributeFormat::UByte1:             return 1 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::UByte2:             return 2 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::UByte3:             return 3 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::UByte4:             return 4 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::Short1:             return 1 * sizeof(int16_t);
        case igl::VertexAttributeFormat::Short2:             return 2 * sizeof(int16_t);
        case igl::VertexAttributeFormat::Short3:             return 3 * sizeof(int16_t);
        case igl::VertexAttributeFormat::Short4:             return 4 * sizeof(int16_t);
        case igl::VertexAttributeFormat::UShort1:            return 1 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::UShort2:            return 2 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::UShort3:            return 3 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::UShort4:            return 4 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::Byte2Norm:          return 2 * sizeof(int8_t);
        case igl::VertexAttributeFormat::Byte4Norm:          return 4 * sizeof(int8_t);
        case igl::VertexAttributeFormat::UByte2Norm:         return 2 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::UByte4Norm:         return 4 * sizeof(uint8_t);
        case igl::VertexAttributeFormat::Short2Norm:         return 2 * sizeof(int16_t);
        case igl::VertexAttributeFormat::Short4Norm:         return 4 * sizeof(int16_t);
        case igl::VertexAttributeFormat::UShort2Norm:        return 2 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::UShort4Norm:        return 4 * sizeof(uint16_t);
        case igl::VertexAttributeFormat::Int1:               return 1 * sizeof(int32_t);
        case igl::VertexAttributeFormat::Int2:               return 2 * sizeof(int32_t);
        case igl::VertexAttributeFormat::Int3:               return 3 * sizeof(int32_t);
        case igl::VertexAttributeFormat::Int4:               return 4 * sizeof(int32_t);
        case igl::VertexAttributeFormat::UInt1:              return 1 * sizeof(uint32_t);
        case igl::VertexAttributeFormat::UInt2:              return 2 * sizeof(uint32_t);
        case igl::VertexAttributeFormat::UInt3:              return 3 * sizeof(uint32_t);
        case igl::VertexAttributeFormat::UInt4:              return 4 * sizeof(uint32_t);
        case igl::VertexAttributeFormat::HalfFloat1:         return 1 * sizeof(int16_t);
        case igl::VertexAttributeFormat::HalfFloat2:         return 2 * sizeof(int16_t);
        case igl::VertexAttributeFormat::HalfFloat3:         return 3 * sizeof(int16_t);
        case igl::VertexAttributeFormat::HalfFloat4:         return 4 * sizeof(int16_t);
        case igl::VertexAttributeFormat::Int_2_10_10_10_REV: return sizeof(uint32_t);
        default: return 0;
    }
}

auto c_str(Glsl_attribute_type type) -> const char*
{
    switch (type) {
        //using enum igl::UniformType;
        case Glsl_attribute_type::invalid:           return "error_type";
        case Glsl_attribute_type::float_:            return "float ";
        case Glsl_attribute_type::float_vec2:        return "vec2  ";
        case Glsl_attribute_type::float_vec3:        return "vec3  ";
        case Glsl_attribute_type::float_vec4:        return "vec4  ";
        case Glsl_attribute_type::bool_:             return "bool  ";
        case Glsl_attribute_type::int_:              return "int   ";
        case Glsl_attribute_type::int_vec2:          return "ivec2 ";
        case Glsl_attribute_type::int_vec3:          return "ivec3 ";
        case Glsl_attribute_type::int_vec4:          return "ivec4 ";
        case Glsl_attribute_type::unsigned_int:      return "uint  ";
        case Glsl_attribute_type::unsigned_int_vec2: return "uivec2";
        case Glsl_attribute_type::unsigned_int_vec3: return "uivec3";
        case Glsl_attribute_type::unsigned_int_vec4: return "uivec4";
        case Glsl_attribute_type::float_mat_2x2:     return "mat2  ";
        case Glsl_attribute_type::float_mat_3x3:     return "mat3  ";
        case Glsl_attribute_type::float_mat_4x4:     return "mat4  ";
        default: return "error_type";
    }
}

auto Vertex_attribute::Usage::operator==(const Usage& other) const -> bool
{
    // TODO index is not compared. Is this a bug or design?
    return (type == other.type);
}

auto Vertex_attribute::Usage::operator!=(const Usage& other) const -> bool
{
    return !(*this == other);
}

auto Vertex_attribute::size() const -> size_t
{
    return get_size(data_type);
}

auto Vertex_attribute::operator==(const Vertex_attribute& other) const -> bool
{
    return
        (usage       == other.usage      ) &&
        (shader_type == other.shader_type) &&
        (data_type   == other.data_type  ) &&
        (offset      == other.offset     ) &&
        (divisor     == other.divisor);
}

auto Vertex_attribute::operator!=(const Vertex_attribute& other) const -> bool
{
    return !(*this == other);
}

auto Vertex_attribute::desc(const Usage_type usage) -> const char*
{
    if (usage == (Usage_type::position | Usage_type::tex_coord)) {
        return "position | tex_coord";
    }

    switch (usage) {
        //using enum Usage_type;
        case Usage_type::none:          return "none";
        case Usage_type::position:      return "position";
        case Usage_type::tangent:       return "tangent";
        case Usage_type::bitangent:     return "bitangent";
        case Usage_type::normal:        return "normal";
        case Usage_type::color:         return "color";
        case Usage_type::joint_weights: return "joint_weights";
        case Usage_type::joint_indices: return "joint_indices";
        case Usage_type::tex_coord:     return "tex_coord";
        case Usage_type::id:            return "id";
        case Usage_type::material:      return "material";
        case Usage_type::aniso_control: return "aniso_control";
        case Usage_type::custom:        return "custom";
        default:                        return "?";
    }
}

} // namespace erhe::graphics
