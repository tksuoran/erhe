// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "gltf_fastgltf.hpp"
#include "gltf_log.hpp"
#include "image_transfer.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_log/log_geogram.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_time/timer.hpp"

#include "erhe_file/file.hpp"
#include "erhe_verify/verify.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <fmt/chrono.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <limits>
#include <numeric>
#include <string>
#include <variant>
#include <vector>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace erhe::gltf {

constexpr glm::mat4 mat4_yup_from_zup{
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f,-1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

[[nodiscard]] auto is_float(const fastgltf::Accessor& accessor)
{
    switch (accessor.componentType) {
        case fastgltf::ComponentType::Invalid      : return false;
        case fastgltf::ComponentType::Byte         : return accessor.normalized;
        case fastgltf::ComponentType::UnsignedByte : return accessor.normalized;
        case fastgltf::ComponentType::Short        : return accessor.normalized;
        case fastgltf::ComponentType::UnsignedShort: return accessor.normalized;
        case fastgltf::ComponentType::Int          : return accessor.normalized;
        case fastgltf::ComponentType::UnsignedInt  : return accessor.normalized;
        case fastgltf::ComponentType::Float        : return true;
        case fastgltf::ComponentType::Double       : return true;
        default: return false;
    }
}

[[nodiscard]] auto c_str(erhe::dataformat::Vertex_attribute_usage usage_type) -> const char*
{
    using namespace erhe::dataformat;
    switch (usage_type) {
        case Vertex_attribute_usage::position     : return "position";
        case Vertex_attribute_usage::tangent      : return "tangent";
        case Vertex_attribute_usage::bitangent    : return "bitangent";
        case Vertex_attribute_usage::normal       : return "normal";
        case Vertex_attribute_usage::color        : return "color";
        case Vertex_attribute_usage::joint_indices: return "joint_indices";
        case Vertex_attribute_usage::joint_weights: return "joint_weights";
        case Vertex_attribute_usage::tex_coord    : return "tex_coord";
        default: {
            ERHE_FATAL("bad vertex usage");
            return "?";
        }
    }
}

auto to_erhe_attribute(const fastgltf::Accessor& accessor) -> erhe::dataformat::Format
{
    using namespace erhe::dataformat;
    switch (accessor.type) {
        case fastgltf::AccessorType::Scalar: {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return Format::format_8_scalar_sint;
                case fastgltf::ComponentType::UnsignedByte : return Format::format_8_scalar_uint;
                case fastgltf::ComponentType::Short        : return Format::format_16_scalar_sint;
                case fastgltf::ComponentType::UnsignedShort: return Format::format_16_scalar_uint;
                case fastgltf::ComponentType::Int          : return Format::format_32_scalar_sint;
                case fastgltf::ComponentType::UnsignedInt  : return Format::format_32_scalar_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_scalar_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec2: {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return Format::format_8_vec2_sint;
                case fastgltf::ComponentType::UnsignedByte : return Format::format_8_vec2_uint;
                case fastgltf::ComponentType::Short        : return Format::format_16_vec2_sint;
                case fastgltf::ComponentType::UnsignedShort: return Format::format_16_vec2_uint;
                case fastgltf::ComponentType::Int          : return Format::format_32_vec2_sint;
                case fastgltf::ComponentType::UnsignedInt  : return Format::format_32_vec2_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_vec2_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec3:{
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return Format::format_8_vec3_sint;
                case fastgltf::ComponentType::UnsignedByte : return Format::format_8_vec3_uint;
                case fastgltf::ComponentType::Short        : return Format::format_16_vec3_sint;
                case fastgltf::ComponentType::UnsignedShort: return Format::format_16_vec3_uint;
                case fastgltf::ComponentType::Int          : return Format::format_32_vec3_sint;
                case fastgltf::ComponentType::UnsignedInt  : return Format::format_32_vec3_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_vec3_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec4: {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return Format::format_8_vec4_sint;
                case fastgltf::ComponentType::UnsignedByte : return Format::format_8_vec4_uint;
                case fastgltf::ComponentType::Short        : return Format::format_16_vec4_sint;
                case fastgltf::ComponentType::UnsignedShort: return Format::format_16_vec4_uint;
                case fastgltf::ComponentType::Int          : return Format::format_32_vec4_sint;
                case fastgltf::ComponentType::UnsignedInt  : return Format::format_32_vec4_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_vec4_float;
                default: break;
            }
        }
        default: {
            break;
        }
    }
    ERHE_FATAL("Unsupported attribute type");
}

#if 0
[[nodiscard]] auto is_per_point(erhe::graphics::Vertex_attribute::Usage_type value) -> bool
{
    switch (value)
    {
        case erhe::graphics::Vertex_attribute::Usage_type::none         : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::automatic    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::position     : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::tangent      : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::bitangent    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::normal       : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::color        : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: return true;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: return true;
        case erhe::graphics::Vertex_attribute::Usage_type::tex_coord    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::id           : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::material     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::aniso_control: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::custom       : return false;
        default: return "?";
    }
}

[[nodiscard]] auto is_per_corner(erhe::graphics::Vertex_attribute::Usage_type value) -> bool
{
    switch (value)
    {
        case erhe::graphics::Vertex_attribute::Usage_type::none         : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::automatic    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::position     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::tangent      : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::bitangent    : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::normal       : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::color        : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::tex_coord    : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::id           : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::material     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::aniso_control: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::custom       : return false;
        default: return "?";
    }
}
#endif

[[nodiscard]] auto c_str(const fastgltf::AnimationInterpolation value) -> const char*
{
    switch (value)
    {
        case fastgltf::AnimationInterpolation::Linear:      return "Linear";
        case fastgltf::AnimationInterpolation::Step:        return "Step";
        case fastgltf::AnimationInterpolation::CubicSpline: return "CubicSpline";
        default: return "?";
    }
}

[[nodiscard]] auto c_str(const fastgltf::PrimitiveType value) -> const char*
{
    switch (value) {
        case fastgltf::PrimitiveType::Points:        return "Points";
        case fastgltf::PrimitiveType::Lines:         return "Lines";
        case fastgltf::PrimitiveType::LineLoop:      return "LineLoop";
        case fastgltf::PrimitiveType::LineStrip:     return "LineStrip";
        case fastgltf::PrimitiveType::Triangles:     return "Triangles";
        case fastgltf::PrimitiveType::TriangleStrip: return "TriangleStrip";
        case fastgltf::PrimitiveType::TriangleFan:   return "TriangleFan";
        default:                                     return "?";
    }
}

[[nodiscard]] auto c_str(const fastgltf::ComponentType value) -> const char*
{
    switch (value) {
        case fastgltf::ComponentType::Invalid:       return "Invalid";
        case fastgltf::ComponentType::Byte:          return "Byte";
        case fastgltf::ComponentType::UnsignedByte:  return "UnsignedByte";
        case fastgltf::ComponentType::Short:         return "Short";
        case fastgltf::ComponentType::UnsignedShort: return "UnsignedShort";
        case fastgltf::ComponentType::Int:           return "Int";
        case fastgltf::ComponentType::UnsignedInt:   return "UnsignedInt";
        case fastgltf::ComponentType::Float:         return "Float";
        case fastgltf::ComponentType::Double:        return "Double";
        default: return "?";
    }
}

[[nodiscard]] auto to_erhe(const fastgltf::PrimitiveType value) -> gl::Primitive_type
{
    switch (value) {
        case fastgltf::PrimitiveType::Points:        return gl::Primitive_type::points;
        case fastgltf::PrimitiveType::Lines:         return gl::Primitive_type::lines;
        case fastgltf::PrimitiveType::LineLoop:      return gl::Primitive_type::line_loop;
        case fastgltf::PrimitiveType::LineStrip:     return gl::Primitive_type::line_strip;
        case fastgltf::PrimitiveType::Triangles:     return gl::Primitive_type::triangles;
        case fastgltf::PrimitiveType::TriangleStrip: return gl::Primitive_type::triangle_strip;
        case fastgltf::PrimitiveType::TriangleFan:   return gl::Primitive_type::triangle_fan;
        default:                                     return gl::Primitive_type::points;
    }
}

[[nodiscard]] auto to_erhe(const fastgltf::AnimationPath value) -> erhe::scene::Animation_path
{
    switch (value) {
        case fastgltf::AnimationPath::Translation: return erhe::scene::Animation_path::TRANSLATION;
        case fastgltf::AnimationPath::Rotation:    return erhe::scene::Animation_path::ROTATION;
        case fastgltf::AnimationPath::Scale:       return erhe::scene::Animation_path::SCALE;
        case fastgltf::AnimationPath::Weights:     return erhe::scene::Animation_path::WEIGHTS;
        default:                                   return erhe::scene::Animation_path::INVALID;
    }
}

[[nodiscard]] auto to_erhe(const fastgltf::AnimationInterpolation value) -> erhe::scene::Animation_interpolation_mode
{
    switch (value) {
        case fastgltf::AnimationInterpolation::Linear:      return erhe::scene::Animation_interpolation_mode::LINEAR;
        case fastgltf::AnimationInterpolation::Step:        return erhe::scene::Animation_interpolation_mode::STEP;
        case fastgltf::AnimationInterpolation::CubicSpline: return erhe::scene::Animation_interpolation_mode::CUBICSPLINE;
        default:                                            return erhe::scene::Animation_interpolation_mode::STEP;
    }
}

using namespace erhe::geometry;

[[nodiscard]] auto to_erhe(const fastgltf::LightType gltf_light_type) -> erhe::scene::Light_type
{
    switch (gltf_light_type) {
        case fastgltf::LightType::Directional: return erhe::scene::Light_type::directional;
        case fastgltf::LightType::Point:       return erhe::scene::Light_type::point;
        case fastgltf::LightType::Spot:        return erhe::scene::Light_type::spot;
        default:                               return erhe::scene::Light_type::directional;
    }
}

auto is_number(std::string_view s) -> bool
{
    return 
        !s.empty() &&
        std::find_if(
            s.begin(), s.end(), [](unsigned char c) {
                return !std::isdigit(c);
            }
        ) == s.end();
}

auto get_attribute_index(std::string_view lhs, std::string_view rhs) -> std::size_t
{
    ERHE_PROFILE_FUNCTION();

    bool prefix_match = lhs.starts_with(rhs);
    ERHE_VERIFY(prefix_match);
    std::string_view number_part = lhs.substr(rhs.length());
    bool is_indexed = is_number(number_part);
    ERHE_VERIFY(is_indexed);
    std::string number_string{number_part};
    int integer_value{0};
    try {
        integer_value = std::stoi(number_string);
    } catch (...) {
    }
    return integer_value;
    //auto result = std::stoi(number_string);//std::from_chars(number_part.data(), number_part.data() + number_part.size(), integer_value);
    //if (result.ec == std::errc::invalid_argument) {
    //    return 0;
    //}
    //return integer_value;
}

auto is_indexed_attribute(std::string_view lhs, std::string_view rhs) -> bool
{
    return lhs.starts_with(rhs) && is_number(lhs.substr(rhs.length()));
};

[[nodiscard]] auto get_attribute_index(std::string_view gltf_attribute_name) -> std::size_t
{
    ERHE_PROFILE_FUNCTION();

    std::size_t last_underscore_pos = gltf_attribute_name.find_last_of("_");
    if (last_underscore_pos == std::string_view::npos) {
        return 0;
    }

    std::string_view number_part = gltf_attribute_name.substr(last_underscore_pos);
    bool is_indexed = is_number(number_part);
    if (!is_indexed) {
        return 0;
    }
    std::string number_string{number_part};
    int integer_value{0};
    try {
        integer_value = std::stoi(number_string);
    } catch (...) {
    }
    return integer_value;
    //// auto result = std::from_chars(number_part.data(), number_part.data() + number_part.size(), integer_value);
    //// if (result.ec == std::errc::invalid_argument) {
    ////     return 0;
    //// }
    //// return 0;
}

[[nodiscard]] auto to_erhe(std::string_view gltf_attribute_name) -> erhe::dataformat::Vertex_attribute_usage
{
    ERHE_PROFILE_FUNCTION();

    using Vertex_attribute_usage = erhe::dataformat::Vertex_attribute_usage;

    static constexpr std::string_view POSITION {"POSITION"};
    static constexpr std::string_view NORMAL   {"NORMAL"};
    static constexpr std::string_view TANGENT  {"TANGENT"};
    static constexpr std::string_view TEXCOORD_{"TEXCOORD_"};
    static constexpr std::string_view COLOR_   {"COLOR_"};
    static constexpr std::string_view JOINTS_  {"JOINTS_"};
    static constexpr std::string_view WEIGHTS_ {"WEIGHTS_"};
    
    if (gltf_attribute_name == POSITION) return Vertex_attribute_usage::position;
    if (gltf_attribute_name == NORMAL  ) return Vertex_attribute_usage::normal;
    if (gltf_attribute_name == TANGENT ) return Vertex_attribute_usage::tangent;
    if (is_indexed_attribute(gltf_attribute_name, TEXCOORD_)) return Vertex_attribute_usage::tex_coord;
    if (is_indexed_attribute(gltf_attribute_name, COLOR_   )) return Vertex_attribute_usage::color;
    if (is_indexed_attribute(gltf_attribute_name, JOINTS_  )) return Vertex_attribute_usage::joint_indices;
    if (is_indexed_attribute(gltf_attribute_name, WEIGHTS_ )) return Vertex_attribute_usage::joint_weights;
    return Vertex_attribute_usage::custom;
}

[[nodiscard]] auto vertex_attribute_usage_from_erhe(const erhe::dataformat::Vertex_attribute& attribute) -> std::string
{
    switch (attribute.usage_type) {
        case erhe::dataformat::Vertex_attribute_usage::position:      return "POSITION";
        case erhe::dataformat::Vertex_attribute_usage::normal:        return "NORMAL";
        case erhe::dataformat::Vertex_attribute_usage::tangent:       return "TANGENT";
        case erhe::dataformat::Vertex_attribute_usage::bitangent:     return "BITANGENT";
        case erhe::dataformat::Vertex_attribute_usage::color:         return fmt::format("COLOR_{}",    attribute.usage_index);
        case erhe::dataformat::Vertex_attribute_usage::tex_coord:     return fmt::format("TEXCOORD_{}", attribute.usage_index);
        case erhe::dataformat::Vertex_attribute_usage::joint_indices: return fmt::format("JOINTS_{}",   attribute.usage_index);
        case erhe::dataformat::Vertex_attribute_usage::joint_weights: return fmt::format("WEIGHTS_{}",  attribute.usage_index);
        default: return {};
    }
}

[[nodiscard]] auto get_accessor_type(const erhe::dataformat::Format format) -> fastgltf::AccessorType
{
    const std::size_t component_count = erhe::dataformat::get_component_count(format);
    switch (component_count) {
        case 1: return fastgltf::AccessorType::Scalar;
        case 2: return fastgltf::AccessorType::Vec2;
        case 3: return fastgltf::AccessorType::Vec3;
        case 4: return fastgltf::AccessorType::Vec4;
        default: return fastgltf::AccessorType::Invalid;
    }
}

[[nodiscard]] auto get_component_type(const erhe::dataformat::Format format) -> fastgltf::ComponentType
{
    switch (format) {
        case erhe::dataformat::Format::format_undefined               : return fastgltf::ComponentType::Invalid;
        case erhe::dataformat::Format::format_8_scalar_unorm          : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_scalar_snorm          : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_scalar_uscaled        : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_scalar_sscaled        : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_scalar_uint           : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_scalar_sint           : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec2_unorm            : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec2_snorm            : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec2_uscaled          : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec2_sscaled          : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec2_uint             : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec2_sint             : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec3_unorm            : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec3_snorm            : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec3_uscaled          : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec3_sscaled          : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec3_uint             : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec3_sint             : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec4_unorm            : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec4_snorm            : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec4_uscaled          : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec4_sscaled          : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_8_vec4_uint             : return fastgltf::ComponentType::UnsignedByte;
        case erhe::dataformat::Format::format_8_vec4_sint             : return fastgltf::ComponentType::Byte;
        case erhe::dataformat::Format::format_16_scalar_unorm         : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_scalar_snorm         : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_scalar_uscaled       : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_scalar_sscaled       : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_scalar_uint          : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_scalar_sint          : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec2_unorm           : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec2_snorm           : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec2_uscaled         : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec2_sscaled         : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec2_uint            : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec2_sint            : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec3_unorm           : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec3_snorm           : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec3_uscaled         : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec3_sscaled         : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec3_uint            : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec3_sint            : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec4_unorm           : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec4_snorm           : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec4_uscaled         : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec4_sscaled         : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_16_vec4_uint            : return fastgltf::ComponentType::UnsignedShort;
        case erhe::dataformat::Format::format_16_vec4_sint            : return fastgltf::ComponentType::Short;
        case erhe::dataformat::Format::format_32_scalar_unorm         : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_scalar_snorm         : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_scalar_uscaled       : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_scalar_sscaled       : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_scalar_uint          : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_scalar_sint          : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_scalar_float         : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_32_vec2_unorm           : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec2_snorm           : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec2_uscaled         : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec2_sscaled         : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec2_uint            : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec2_sint            : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec2_float           : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_32_vec3_unorm           : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec3_snorm           : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec3_uscaled         : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec3_sscaled         : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec3_uint            : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec3_sint            : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec3_float           : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_32_vec4_unorm           : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec4_snorm           : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec4_uscaled         : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec4_sscaled         : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec4_uint            : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec4_sint            : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec4_float           : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return fastgltf::ComponentType::Invalid;
        case erhe::dataformat::Format::format_packed1010102_vec4_snorm: return fastgltf::ComponentType::Invalid;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint : return fastgltf::ComponentType::Invalid;
        case erhe::dataformat::Format::format_packed1010102_vec4_sint : return fastgltf::ComponentType::Invalid;
        default: return fastgltf::ComponentType::Invalid;
    }
}

[[nodiscard]] auto get_normalized(const erhe::dataformat::Format format) -> bool
{
    switch (format) {
        case erhe::dataformat::Format::format_undefined               : return false;
        case erhe::dataformat::Format::format_8_scalar_unorm          : return true;
        case erhe::dataformat::Format::format_8_scalar_snorm          : return true;
        case erhe::dataformat::Format::format_8_scalar_uscaled        : return false;
        case erhe::dataformat::Format::format_8_scalar_sscaled        : return false;
        case erhe::dataformat::Format::format_8_scalar_uint           : return false;
        case erhe::dataformat::Format::format_8_scalar_sint           : return false;
        case erhe::dataformat::Format::format_8_vec2_unorm            : return true;
        case erhe::dataformat::Format::format_8_vec2_snorm            : return true;
        case erhe::dataformat::Format::format_8_vec2_uscaled          : return false;
        case erhe::dataformat::Format::format_8_vec2_sscaled          : return false;
        case erhe::dataformat::Format::format_8_vec2_uint             : return false;
        case erhe::dataformat::Format::format_8_vec2_sint             : return false;
        case erhe::dataformat::Format::format_8_vec3_unorm            : return true;
        case erhe::dataformat::Format::format_8_vec3_snorm            : return true;
        case erhe::dataformat::Format::format_8_vec3_uscaled          : return false;
        case erhe::dataformat::Format::format_8_vec3_sscaled          : return false;
        case erhe::dataformat::Format::format_8_vec3_uint             : return false;
        case erhe::dataformat::Format::format_8_vec3_sint             : return false;
        case erhe::dataformat::Format::format_8_vec4_unorm            : return true;
        case erhe::dataformat::Format::format_8_vec4_snorm            : return true;
        case erhe::dataformat::Format::format_8_vec4_uscaled          : return false;
        case erhe::dataformat::Format::format_8_vec4_sscaled          : return false;
        case erhe::dataformat::Format::format_8_vec4_uint             : return false;
        case erhe::dataformat::Format::format_8_vec4_sint             : return false;
        case erhe::dataformat::Format::format_16_scalar_unorm         : return true;
        case erhe::dataformat::Format::format_16_scalar_snorm         : return true;
        case erhe::dataformat::Format::format_16_scalar_uscaled       : return false;
        case erhe::dataformat::Format::format_16_scalar_sscaled       : return false;
        case erhe::dataformat::Format::format_16_scalar_uint          : return false;
        case erhe::dataformat::Format::format_16_scalar_sint          : return false;
        case erhe::dataformat::Format::format_16_vec2_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec2_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec2_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec2_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec2_uint            : return false;
        case erhe::dataformat::Format::format_16_vec2_sint            : return false;
        case erhe::dataformat::Format::format_16_vec3_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec3_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec3_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec3_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec3_uint            : return false;
        case erhe::dataformat::Format::format_16_vec3_sint            : return false;
        case erhe::dataformat::Format::format_16_vec4_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec4_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec4_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec4_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec4_uint            : return false;
        case erhe::dataformat::Format::format_16_vec4_sint            : return false;
        case erhe::dataformat::Format::format_32_scalar_unorm         : return true;
        case erhe::dataformat::Format::format_32_scalar_snorm         : return true;
        case erhe::dataformat::Format::format_32_scalar_uscaled       : return false;
        case erhe::dataformat::Format::format_32_scalar_sscaled       : return false;
        case erhe::dataformat::Format::format_32_scalar_uint          : return false;
        case erhe::dataformat::Format::format_32_scalar_sint          : return false;
        case erhe::dataformat::Format::format_32_scalar_float         : return false;
        case erhe::dataformat::Format::format_32_vec2_unorm           : return true;
        case erhe::dataformat::Format::format_32_vec2_snorm           : return true;
        case erhe::dataformat::Format::format_32_vec2_uscaled         : return false;
        case erhe::dataformat::Format::format_32_vec2_sscaled         : return false;
        case erhe::dataformat::Format::format_32_vec2_uint            : return false;
        case erhe::dataformat::Format::format_32_vec2_sint            : return false;
        case erhe::dataformat::Format::format_32_vec2_float           : return false;
        case erhe::dataformat::Format::format_32_vec3_unorm           : return true;
        case erhe::dataformat::Format::format_32_vec3_snorm           : return true;
        case erhe::dataformat::Format::format_32_vec3_uscaled         : return false;
        case erhe::dataformat::Format::format_32_vec3_sscaled         : return false;
        case erhe::dataformat::Format::format_32_vec3_uint            : return false;
        case erhe::dataformat::Format::format_32_vec3_sint            : return false;
        case erhe::dataformat::Format::format_32_vec3_float           : return false;
        case erhe::dataformat::Format::format_32_vec4_unorm           : return true;
        case erhe::dataformat::Format::format_32_vec4_snorm           : return true;
        case erhe::dataformat::Format::format_32_vec4_uscaled         : return false;
        case erhe::dataformat::Format::format_32_vec4_sscaled         : return false;
        case erhe::dataformat::Format::format_32_vec4_uint            : return false;
        case erhe::dataformat::Format::format_32_vec4_sint            : return false;
        case erhe::dataformat::Format::format_32_vec4_float           : return false;
        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return true;
        case erhe::dataformat::Format::format_packed1010102_vec4_snorm: return true;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint : return false;
        case erhe::dataformat::Format::format_packed1010102_vec4_sint : return false;
        default: return false;
    }
}

using Item_flags = erhe::Item_flags;

[[nodiscard]] auto to_gl(const erhe::graphics::Image_format format) -> gl::Internal_format
{
    switch (format) {
        //using enum erhe::graphics::Image_format;
        case erhe::graphics::Image_format::srgb8:        return gl::Internal_format::srgb8;
        case erhe::graphics::Image_format::srgb8_alpha8: return gl::Internal_format::srgb8_alpha8;
        default: {
            ERHE_FATAL("Bad image format %04x", static_cast<unsigned int>(format));
        }
    }
    // std::unreachable() return gl::Internal_format::rgba8;
}

void accessor_read_floats(
    const fastgltf::Asset&    asset,
    const fastgltf::Accessor& accessor,
    size_t                    index,
    float*                    out_value,
    std::size_t               component_count_of_out_out_value
)
{
    ERHE_PROFILE_FUNCTION();

    std::size_t component_count = std::min(fastgltf::getNumComponents(accessor.type), component_count_of_out_out_value);
    switch (component_count) {
        case 1: {
            float value = fastgltf::getAccessorElement<float>(asset, accessor, index);
            out_value[0] = value;
            break;
        }
        case 2: {
            fastgltf::math::fvec2 value = fastgltf::getAccessorElement<fastgltf::math::fvec2>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            break;
        }
        case 3: {
            fastgltf::math::fvec3 value = fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            out_value[2] = value.z();
            break;
        }
        case 4: {
            fastgltf::math::fvec4 value = fastgltf::getAccessorElement<fastgltf::math::fvec4>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            out_value[2] = value.z();
            out_value[3] = value.w();
            break;
        }
        default: {
            break;
        }
    }
}

void accessor_read_u32s(
    const fastgltf::Asset&    asset,
    const fastgltf::Accessor& accessor,
    size_t                    index,
    uint32_t*                 out_value,
    std::size_t               component_count_of_out_out_value
)
{
    ERHE_PROFILE_FUNCTION();

    std::size_t component_count = std::min(fastgltf::getNumComponents(accessor.type), component_count_of_out_out_value);
    switch (component_count) {
        case 1: {
            uint32_t value = fastgltf::getAccessorElement<uint32_t>(asset, accessor, index);
            out_value[0] = value;
            break;
        }
        case 2: {
            fastgltf::math::u32vec2 value = fastgltf::getAccessorElement<fastgltf::math::u32vec2>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            break;
        }
        case 3: {
            fastgltf::math::u32vec3 value = fastgltf::getAccessorElement<fastgltf::math::u32vec3>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            out_value[2] = value.z();
            break;
        }
        case 4: {
            fastgltf::math::u32vec4 value = fastgltf::getAccessorElement<fastgltf::math::u32vec4>(asset, accessor, index);
            out_value[0] = value.x();
            out_value[1] = value.y();
            out_value[2] = value.z();
            out_value[3] = value.w();
            break;
        }
        default:
        {
        }
    }
}

// Derived from fastgltf::copyComponentsFromAccessor()
void copyComponentsFromAccessor(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, void* dest, std::size_t destStride)
{
    ERHE_PROFILE_FUNCTION();

    assert((!bool(accessor.sparse) || accessor.sparse->count == 0) && "copyComponentsFromAccessor currently does not support sparse accessors.");

    auto* dstBytes = static_cast<std::byte*>(dest);

    auto elemSize = getElementByteSize(accessor.type, accessor.componentType);
    //auto componentCount = getNumComponents(accessor.type);

    auto& view = asset.bufferViews[*accessor.bufferViewIndex];
    auto srcStride = view.byteStride.value_or(elemSize);

    const fastgltf::DefaultBufferDataAdapter adapter{};
    auto srcBytes = adapter(asset, *accessor.bufferViewIndex).subspan(accessor.byteOffset);

    for (std::size_t i = 0; i < accessor.count; ++i) {
        std::memcpy(dstBytes + destStride * i, &srcBytes[srcStride * i], elemSize);
    }
}

class Gltf_parser
{
private:
    Gltf_data&                          m_data_out;
    Gltf_parse_arguments                m_arguments;
    fastgltf::Expected<fastgltf::Asset> m_asset;

public:
    Gltf_parser(
        fastgltf::Expected<fastgltf::Asset>&& asset,
        Gltf_data&                            gltf_data,
        const Gltf_parse_arguments&           arguments
    )
        : m_data_out {gltf_data}
        , m_arguments{arguments}
        , m_asset    {std::move(asset)}
    {
        trace_info();
    }

    [[nodiscard]] auto safe_resource_name(
        std::string_view  name,
        std::string_view  resource_type,
        const std::size_t resource_index
    ) const -> std::string
    {
        if (name.empty() || name.size() == 0) {
            return fmt::format("{}-{}-{}", m_arguments.path.filename().string(), resource_type, resource_index);
        }
        return std::string{name};
    }

    void parse_and_build()
    {
        ERHE_PROFILE_FUNCTION();

        if (m_asset.error() != fastgltf::Error::None) {
            log_gltf->error("No data loaded to parse glTF");
            return;
        }

        log_gltf->trace("parsing images");
        m_data_out.images.resize(m_asset->images.size());
        for (std::size_t i = 0, end = m_asset->images.size(); i < end; ++i) {
            parse_image(i);
        }

        log_gltf->trace("parsing samplers");
        m_data_out.samplers.resize(m_asset->samplers.size());
        for (std::size_t i = 0, end = m_asset->samplers.size(); i < end; ++i) {
            parse_sampler(i);
        }

        log_gltf->trace("parsing materials");
        m_data_out.materials.resize(m_asset->materials.size());
        for (std::size_t i = 0, end = m_asset->materials.size(); i < end; ++i) {
            parse_material(i);
        }

        log_gltf->trace("parsing cameras");
        m_data_out.cameras.resize(m_asset->cameras.size());
        for (std::size_t i = 0, end = m_asset->cameras.size(); i < end; ++i) {
            parse_camera(i);
        }

        log_gltf->trace("parsing lights");
        m_data_out.lights.resize(m_asset->lights.size());
        for (std::size_t i = 0, end = m_asset->lights.size(); i < end; ++i) {
            parse_light(i);
        }

        log_gltf->trace("parsing meshes");
        m_data_out.meshes.resize(m_asset->meshes.size());
        for (std::size_t i = 0, end = m_asset->meshes.size(); i < end; ++i) {
            parse_mesh(i);
        }

        log_gltf->trace("parsing nodes");
        m_data_out.nodes.resize(m_asset->nodes.size());
        m_data_out.skins.resize(m_asset->skins.size()); // Skins are parsed just in time during node parsing
        if (!m_asset->scenes.empty()) {
            // TODO For now, only one scene per glTF is supported.
            const fastgltf::Scene& scene = m_asset->scenes.front();
            for (std::size_t i = 0, end = scene.nodeIndices.size(); i < end; ++i) {
                parse_node(scene.nodeIndices[i], m_arguments.root_node);
            }
        } else {
            // TODO Make a graph and sort by depth.
            std::vector<bool> is_child(m_asset->nodes.size()); // default initialized to false
            for (std::size_t i = 0, end = m_asset->nodes.size(); i < end; ++i) {
                const fastgltf::Node& node = m_asset->nodes[i];
                for (std::size_t child_index : node.children) {
                    is_child[child_index] = true;
                }
            }
            for (std::size_t i = 0, end = m_asset->nodes.size(); i < end; ++i) {
                if (is_child[i]) {
                    continue;
                }
                parse_node(i, m_arguments.root_node);
            }
        }

        log_gltf->trace("parsing skins");
        for (std::size_t i = 0, end = m_asset->skins.size(); i < end; ++i) {
            if (!m_data_out.skins[i]) {
                parse_skin(i);
            }
        }

        log_gltf->trace("parsing animations");
        m_data_out.animations.reserve(m_asset->animations.size());
        for (std::size_t i = 0, end = m_asset->animations.size(); i < end; ++i) {
            parse_animation(i);
        }
    }

private:
    void trace_info() const
    {
        if (m_asset->assetInfo.has_value()) {
            const fastgltf::AssetInfo& asset_info = m_asset->assetInfo.value();
            log_gltf->trace("Asset Version:    {}", asset_info.gltfVersion);
            log_gltf->trace("Asset Copyright:  {}", asset_info.copyright);
            log_gltf->trace("Asset Generator:  {}", asset_info.generator);
        }
        log_gltf->trace("Node Count:       {}", m_asset->nodes.size());
        log_gltf->trace("Camera Count:     {}", m_asset->cameras.size());
        log_gltf->trace("Light Count:      {}", m_asset->lights.size());
        log_gltf->trace("Material Count:   {}", m_asset->materials.size());
        log_gltf->trace("Mesh Count:       {}", m_asset->meshes.size());
        log_gltf->trace("Skin Count:       {}", m_asset->skins.size());
        log_gltf->trace("Image Count:      {}", m_asset->images.size());
        log_gltf->trace("Texture Count:    {}", m_asset->textures.size());
        log_gltf->trace("Sampler Count:    {}", m_asset->samplers.size());
        log_gltf->trace("Buffer Count:     {}", m_asset->buffers.size());
        log_gltf->trace("BufferView Count: {}", m_asset->bufferViews.size());
        log_gltf->trace("Accessor Count:   {}", m_asset->accessors.size());
        log_gltf->trace("Animation Count:  {}", m_asset->animations.size());
        log_gltf->trace("Scene Count:      {}", m_asset->scenes.size());

        for (std::size_t i = 0; i < m_asset->extensionsUsed.size(); ++i) {
            log_gltf->trace("Extension Used: {}", m_asset->extensionsUsed.at(i));
        }

        for (std::size_t i = 0; i < m_asset->extensionsRequired.size(); ++i) {
            log_gltf->trace("Extension Required: {}", m_asset->extensionsRequired.at(i));
        }
    }
    void parse_animation(const std::size_t animation_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Animation& animation = m_asset->animations[animation_index];
        const std::string animation_name = safe_resource_name(animation.name, "animation", animation_index);
        log_gltf->trace("Animation: id = {}, name = {}", animation_index, animation_name);

        auto erhe_animation = std::make_shared<erhe::scene::Animation>(animation_name);
        erhe_animation->set_source_path(m_arguments.path);
        erhe_animation->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
        m_data_out.animations.push_back(erhe_animation);
        erhe_animation->samplers.resize(animation.samplers.size());
        for (std::size_t sampler_index = 0, end = animation.samplers.size(); sampler_index < end; ++sampler_index) {
            const fastgltf::AnimationSampler& sampler = animation.samplers[sampler_index];
            const fastgltf::Accessor& inputAccessor = m_asset->accessors.at(sampler.inputAccessor);
            const fastgltf::Accessor& outputAccessor = m_asset->accessors.at(sampler.outputAccessor);
            auto& erhe_sampler = erhe_animation->samplers[sampler_index];
            log_gltf->trace(
                "Animation `{}` sampler index = {}, "
                "input type = {}, output type = {}, "
                "interpolation = {}, "
                "input timestamp count = {}, value count = {}",
                animation_name,
                sampler_index,
                fastgltf::getAccessorTypeName(inputAccessor.type),
                fastgltf::getAccessorTypeName(outputAccessor.type),
                c_str(sampler.interpolation),
                inputAccessor.count,
                outputAccessor.count
            );

            erhe_sampler.interpolation_mode = to_erhe(sampler.interpolation);
            const std::size_t output_component_count = getNumComponents(outputAccessor.type);
            const std::size_t output_float_count     = outputAccessor.count * output_component_count;
            std::vector<float> timestamps(inputAccessor.count);
            std::vector<float> values   (output_float_count);
            const fastgltf::Asset& asset = m_asset.get();
            fastgltf::iterateAccessorWithIndex<float>(
                asset, inputAccessor,
                [&](float value, std::size_t idx) {
                    timestamps[idx] = value;
                }
            );

            switch (outputAccessor.type) {
                case fastgltf::AccessorType::Vec3: {
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        asset, outputAccessor,
                        [&](fastgltf::math::fvec3 value, std::size_t idx) {
                           values[idx * 3 + 0] = value.x();
                           values[idx * 3 + 1] = value.y();
                           values[idx * 3 + 2] = value.z();
                        }
                    );
                    break;
                }
                case fastgltf::AccessorType::Vec4: {
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                        asset, outputAccessor,
                        [&](fastgltf::math::fvec4 value, std::size_t idx) {
                           values[idx * 4 + 0] = value.x();
                           values[idx * 4 + 1] = value.y();
                           values[idx * 4 + 2] = value.z();
                           values[idx * 4 + 3] = value.w();
                        }
                    );
                    break;
                }
                default: {
                    // TODO log warning
                    break;
                }
            }

            erhe_sampler.set(std::move(timestamps), std::move(values));
        }
        erhe_animation->channels.resize(animation.channels.size());
        for (std::size_t channel_index = 0, end = animation.channels.size(); channel_index < end; ++channel_index) {
            const fastgltf::AnimationChannel& channel = animation.channels.at(channel_index);
            if (!channel.nodeIndex.has_value()) {
                log_gltf->warn("Animation `{}` channel {} has no target", animation_name, channel_index);
                continue;
            }
            erhe::scene::Animation_path path = to_erhe(channel.path);
            size_t target_node_index = channel.nodeIndex.value();
            const auto& target_node = m_data_out.nodes.at(target_node_index);
            log_gltf->trace(
                "Channel {} target = {}, pos = {} path = {}, sampler index = {}",
                channel_index,
                target_node->get_name(),
                target_node->position_in_world(),
                c_str(path),
                channel.samplerIndex
            );

            //const auto& sampler = new_animation->samplers.at(sampler_index);
            //const auto component_count = erhe::scene::get_component_count(to_erhe(channel.target_path));
            //std::size_t t = 0;
            //for (std::size_t j = 0; j < sampler.data.size();) {
            //    std::stringstream ss;
            //    ss << fmt::format("t = {}: ", sampler.timestamps.at(t++));
            //    bool first = true;
            //    for (std::size_t k = 0; k < component_count; ++k) {
            //        if (!first) {
            //            ss << ", ";
            //        } else {
            //            first = false;
            //        }
            //        ss << fmt::format("v{} = {}", k, sampler.data[j]);
            //        ++j;
            //    }
            //    log_gltf->trace(ss.str());
            //}

            const fastgltf::AnimationSampler& sampler = animation.samplers.at(channel.samplerIndex);
            erhe_animation->channels[channel_index] = erhe::scene::Animation_channel{
                .path           = path,
                .sampler_index  = channel.samplerIndex,
                .target         = target_node,
                .start_position = 0,
                .value_offset   = (sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline)
                    ? get_component_count(path)
                    : 0
            };
        }
        m_data_out.animations[animation_index] = erhe_animation;
    }
    auto load_image_file(
        const std::filesystem::path& path,
        std::string_view             image_name
    ) -> std::shared_ptr<erhe::graphics::Texture>
    {
        ERHE_PROFILE_FUNCTION();

        const bool file_is_ok = erhe::file::check_is_existing_non_empty_regular_file("Gltf_parser::load_image_file", path);
        if (!file_is_ok) {
            return {};
        }

        erhe::graphics::Image_info image_info;
        erhe::graphics::Image_loader loader;

        if (!loader.open(path, image_info)) {
            return {};
        }

        auto& slot = m_arguments.image_transfer.get_slot();

        erhe::graphics::Texture_create_info texture_create_info{
            .instance        = m_arguments.graphics_instance,
            .internal_format = to_gl(image_info.format),
            .use_mipmaps     = true, //(image_info.level_count > 1),
            .width           = image_info.width,
            .height          = image_info.height,
            .depth           = image_info.depth,
            .level_count     = image_info.level_count,
            .row_stride      = image_info.row_stride,
            .debug_label     = std::string{image_name} // path.filename().string()
        };
        const int  mipmap_count    = texture_create_info.calculate_level_count();
        const bool generate_mipmap = mipmap_count != image_info.level_count;
        if (generate_mipmap) {
            texture_create_info.level_count = mipmap_count;
        }
        std::span<std::uint8_t> span = slot.begin_span_for(image_info.width, image_info.height, texture_create_info.internal_format);

        const bool ok = loader.load(span);
        loader.close();
        slot.end(ok);
        if (!ok) {
            return {};
        }

        auto texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
        texture->set_source_path(path);
        texture->set_debug_label(image_name.empty() ? erhe::file::to_string(path) : image_name);

        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        if (generate_mipmap) {
            gl::generate_texture_mipmap(texture->gl_name());
        }
        return texture;
    }
    auto load_image_buffer(
        const std::size_t buffer_view_index,
        const std::size_t image_index,
        std::string_view  image_name
    ) -> std::shared_ptr<erhe::graphics::Texture>
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::BufferView&  buffer_view      = m_asset->bufferViews[buffer_view_index];
        const fastgltf::Buffer&      buffer           = m_asset->buffers.at(buffer_view.bufferIndex);
        const std::string            buffer_view_name = safe_resource_name(buffer_view.name, "buffer_view", buffer_view_index);
        const std::string            name             = !image_name.empty() 
            ? std::string{image_name}
            : fmt::format("{} image {} {}", m_arguments.path.filename().string(), image_index, buffer_view_name);
        erhe::graphics::Image_info   image_info;
        erhe::graphics::Image_loader loader;

        bool load_ok = false;
        auto& slot = m_arguments.image_transfer.get_slot();
        erhe::graphics::Texture_create_info texture_create_info{
            .instance    = m_arguments.graphics_instance,
            .debug_label = name
        };
        int  mipmap_count    = 0;
        bool generate_mipmap = false;

        std::visit(
            fastgltf::visitor{
                [](auto& arg) {
                    static_cast<void>(arg);
                    ERHE_FATAL("TODO Unsupported image buffer view source");
                },
                [&](const fastgltf::sources::Array& data) {
                    std::span<const std::uint8_t> image_encoded_buffer_view{
                        reinterpret_cast<const std::uint8_t*>(data.bytes.data()) + buffer_view.byteOffset,
                        buffer_view.byteLength
                    };
                    if (!loader.open(image_encoded_buffer_view, image_info)) {
                        log_gltf->error("Failed to parse image from buffer view '{}'", name);
                        return;
                    }

                    texture_create_info.internal_format = to_gl(image_info.format);
                    texture_create_info.use_mipmaps     = true; //(image_info.level_count > 1),
                    texture_create_info.width           = image_info.width;
                    texture_create_info.height          = image_info.height;
                    texture_create_info.depth           = image_info.depth;
                    texture_create_info.level_count     = image_info.level_count;
                    texture_create_info.row_stride      = image_info.row_stride;
                    texture_create_info.debug_label     = name;

                    mipmap_count    = texture_create_info.calculate_level_count();
                    generate_mipmap = mipmap_count != image_info.level_count;
                    if (generate_mipmap) {
                        texture_create_info.level_count = mipmap_count;
                    }
                    std::span<std::uint8_t> span = slot.begin_span_for(image_info.width, image_info.height, texture_create_info.internal_format);

                    load_ok = loader.load(span);
                    loader.close();
                }
            },
            buffer.data
        );
        slot.end(load_ok);
        if (!load_ok) {
            log_gltf->warn(
                "Image '{}' load failed: image index = {}, width = {}, height = {}",
                name, image_index, texture_create_info.width, texture_create_info.height
            );
            return {};
        }

        log_gltf->info(
            "Loaded image '{}': image index = {}, width = {}, height = {}",
            name, image_index, texture_create_info.width, texture_create_info.height
        );

        auto texture = std::make_shared<erhe::graphics::Texture>(texture_create_info);
        texture->set_source_path(m_arguments.path);
        texture->set_debug_label(name);
        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        if (generate_mipmap) {
            gl::generate_texture_mipmap(texture->gl_name());
        }
        return texture;
    }
    void parse_image(const std::size_t image_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Image& image      = m_asset->images[image_index];
        const std::string      image_name = safe_resource_name(image.name, "image", image_index);
        log_gltf->trace("Image: image index = {}, name = {}", image_index, image_name);
        std::shared_ptr<erhe::graphics::Texture> erhe_texture;
        std::visit(
            fastgltf::visitor {
                [](auto& arg) {
                    static_cast<void>(arg);
                    ERHE_FATAL("TODO Unsupported image source");
                },
                [&](const fastgltf::sources::BufferView& buffer_view_source){
                    erhe_texture = load_image_buffer(buffer_view_source.bufferViewIndex, image_index, image_name);
                },
                [&](const fastgltf::sources::URI& uri){
                    std::filesystem::path relative_path = uri.uri.fspath();
                    erhe_texture = load_image_file(m_arguments.path.replace_filename(relative_path), image_name);
                }
            },
            image.data
        );

        if (erhe_texture) {
            erhe_texture->set_debug_label(image_name);
            m_data_out.images.push_back(erhe_texture);
        }
        m_data_out.images[image_index] = erhe_texture;
    }
    void parse_sampler(const std::size_t sampler_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Sampler& sampler = m_asset->samplers[sampler_index];
        const std::string sampler_name = safe_resource_name(sampler.name, "sampler", sampler_index);
        log_gltf->trace("Sampler: sampler index = {}, name = {}", sampler_index, sampler_name);

        erhe::graphics::Sampler_create_info create_info;
        create_info.min_filter     = sampler.minFilter.has_value() ? static_cast<gl::Texture_min_filter>(sampler.minFilter.value()) : gl::Texture_min_filter::nearest_mipmap_nearest;
        create_info.mag_filter     = sampler.magFilter.has_value() ? static_cast<gl::Texture_mag_filter>(sampler.magFilter.value()) : gl::Texture_mag_filter::nearest;
        create_info.wrap_mode[0]   = static_cast<gl::Texture_wrap_mode>(sampler.wrapS);
        create_info.wrap_mode[1]   = static_cast<gl::Texture_wrap_mode>(sampler.wrapT);
        create_info.max_anisotropy = m_arguments.graphics_instance.limits.max_texture_max_anisotropy;
        create_info.debug_label    = sampler_name;

        auto erhe_sampler = std::make_shared<erhe::graphics::Sampler>(create_info);
        // TODO erhe_sampler->set_source_path(m_path);
        erhe_sampler->set_debug_label(sampler_name);
        m_data_out.samplers[sampler_index] = erhe_sampler;
        m_data_out.samplers.push_back(erhe_sampler);
    }
    void parse_material(const std::size_t material_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Material& material = m_asset->materials[material_index];
        const std::string material_name = safe_resource_name(material.name, "material", material_index);
        log_gltf->trace("Primitive material: id = {}, name = {}", material_index, material_name);

        auto new_material = std::make_shared<erhe::primitive::Material>(material_name);
        new_material->set_source_path(m_arguments.path);
        m_data_out.materials[material_index] = new_material;
        {
            const fastgltf::PBRData& pbr_data = material.pbrData;
            if (pbr_data.baseColorTexture.has_value()) {
                const fastgltf::TextureInfo& texture_info = pbr_data.baseColorTexture.value();
                const fastgltf::Texture& texture = m_asset->textures[texture_info.textureIndex];
                if (texture.imageIndex.has_value()) {
                    new_material->textures.base_color = m_data_out.images[texture.imageIndex.value()];
                }
                if (texture.samplerIndex.has_value()) {
                    new_material->samplers.base_color = m_data_out.samplers[texture.samplerIndex.value()];
                }
                new_material->tex_coords.base_color = static_cast<uint8_t>(texture_info.textureIndex);
                // TODO texture transform
            }
            if (pbr_data.metallicRoughnessTexture.has_value()) {
                const fastgltf::TextureInfo& texture_info = pbr_data.metallicRoughnessTexture.value();
                const fastgltf::Texture& texture = m_asset->textures[texture_info.textureIndex];
                if (texture.imageIndex.has_value()) {
                    new_material->textures.metallic_roughness = m_data_out.images[texture.imageIndex.value()];
                }
                if (texture.samplerIndex.has_value()) {
                    new_material->samplers.metallic_roughness = m_data_out.samplers[texture.samplerIndex.value()];
                }
                new_material->tex_coords.metallic_roughness = static_cast<uint8_t>(texture_info.textureIndex);
            }
            new_material->base_color = glm::vec4{
                pbr_data.baseColorFactor[0],
                pbr_data.baseColorFactor[1],
                pbr_data.baseColorFactor[2],
                pbr_data.baseColorFactor[3]
            };
            new_material->metallic    = pbr_data.metallicFactor;
            new_material->roughness.x = pbr_data.roughnessFactor;
            new_material->roughness.y = pbr_data.roughnessFactor;
            new_material->emissive    = glm::vec4{0.0f, 0.0f, 0.0f, 0.0f};
            new_material->enable_flag_bits(erhe::Item_flags::show_in_ui);
            log_gltf->trace(
                "Material PBR metallic roughness base color factor = {}, {}, {}, {}",
                pbr_data.baseColorFactor[0],
                pbr_data.baseColorFactor[1],
                pbr_data.baseColorFactor[2],
                pbr_data.baseColorFactor[3]
            );
            log_gltf->trace("Material PBR metallic roughness metallic factor = {}", pbr_data.metallicFactor);
            log_gltf->trace("Material PBR metallic roughness roughness factor = {}", pbr_data.roughnessFactor);
        }
    }
    void parse_node_transform(const fastgltf::Node& node, const std::shared_ptr<erhe::scene::Node>& erhe_node)
    {
        ERHE_PROFILE_FUNCTION();

        std::visit(
            fastgltf::visitor {
                [&](const fastgltf::TRS& trs) {
                    const auto& t = trs.translation;
                    const auto& r = trs.rotation;
                    const auto& s = trs.scale;

                    erhe_node->node_data.transforms.parent_from_node.set_trs(
                        glm::vec3{t[0], t[1], t[2]},
                        glm::quat{r[3], r[0], r[1], r[2]}, // glm has [w x y z], gltf has [x y z w]
                        glm::vec3{s[0], s[1], s[2]}
                    );
                },
                [&](const fastgltf::math::fmat4x4 matrix) {
                    glm::mat4 glm_matrix = glm::make_mat4x4(matrix.data());
                    erhe_node->node_data.transforms.parent_from_node.set(glm_matrix);
                },
            },
            node.transform
        );
        erhe_node->update_world_from_node();
        erhe_node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
    }
    void parse_camera(const std::size_t camera_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Camera& camera = m_asset->cameras[camera_index];
        const std::string camera_name = safe_resource_name(camera.name, "camera", camera_index);
        log_gltf->trace("Camera: camera index = {}, name = {}", camera_index, camera_name);

        auto erhe_camera = std::make_shared<erhe::scene::Camera>(camera_name);
        erhe_camera->set_source_path(m_arguments.path);
        m_data_out.cameras[camera_index] = erhe_camera;
        erhe_camera->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        auto* projection = erhe_camera->projection();

	    std::visit(
            fastgltf::visitor{
		        [&](const fastgltf::Camera::Perspective& perspective) {
                    if (perspective.aspectRatio.has_value()) {
                        log_gltf->trace("Camera.aspect_ratio:      {}", perspective.aspectRatio.value());
                    }
                    log_gltf->trace("Camera.yfov:              {}", perspective.yfov);
                    if (perspective.zfar.has_value()) {
                        log_gltf->trace("Camera.zfar:              {}", perspective.zfar.value());
                    }
                    log_gltf->trace("Camera.znear:             {}", perspective.znear);
                    projection->projection_type = erhe::scene::Projection::Type::perspective_vertical;
                    projection->fov_y           = perspective.yfov;
                    projection->z_far           = perspective.zfar.has_value()
                        ? perspective.zfar.value()
                        : 0.0f;
                },
                [&](const fastgltf::Camera::Orthographic& orthographic) {
                    log_gltf->trace("Camera.xmag:              {}", orthographic.xmag);
                    log_gltf->trace("Camera.ymag:              {}", orthographic.ymag);
                    log_gltf->trace("Camera.zfar:              {}", orthographic.zfar);
                    log_gltf->trace("Camera.znear:             {}", orthographic.znear);
                    projection->projection_type = erhe::scene::Projection::Type::orthogonal;
                    projection->ortho_width     = orthographic.xmag;
                    projection->ortho_height    = orthographic.ymag;
                    projection->z_far           = orthographic.zfar;
                    projection->z_near          = orthographic.znear;
                }
            },
            camera.camera
        );
    }
    void parse_light(const std::size_t light_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Light& light = m_asset->lights[light_index];
        const std::string light_name = safe_resource_name(light.name, "light", light_index);
        log_gltf->trace("Light: camera index = {}, name = {}", light_index, light_name);

        auto erhe_light = std::make_shared<erhe::scene::Light>(light_name);
        erhe_light->set_source_path(m_arguments.path);
        m_data_out.lights[light_index] = erhe_light;
        erhe_light->color = glm::vec3{
            light.color[0],
            light.color[1],
            light.color[2]
        };
        erhe_light->intensity        = light.intensity;
        erhe_light->type             = to_erhe(light.type);
        erhe_light->range            = light.range.has_value() ? light.range.value() : 0.0f;
        erhe_light->inner_spot_angle = light.innerConeAngle.has_value() ? light.innerConeAngle.value() : 0.0f;
        erhe_light->outer_spot_angle = light.outerConeAngle.has_value() ? light.outerConeAngle.value() : 0.0f;
        // TODO Sensible defaults for inner and outer cone angles
        erhe_light->layer_id = 0;
        erhe_light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    }

    class Primitive_entry
    {
    public:
        std::size_t                                     index_accessor;
        std::vector<std::size_t>                        attribute_accessors;
        std::shared_ptr<erhe::primitive::Triangle_soup> triangle_soup;
    };
    std::vector<Primitive_entry> m_primitive_entries;

    void load_new_primitive_geometry(const fastgltf::Primitive& primitive, Primitive_entry& primitive_entry)
    {
        ERHE_PROFILE_FUNCTION();

        primitive_entry.triangle_soup.reset();

        if (!primitive.indicesAccessor.has_value()) {
            return;
        }

        primitive_entry.triangle_soup = std::make_shared<erhe::primitive::Triangle_soup>();
        erhe::primitive::Triangle_soup& triangle_soup = *primitive_entry.triangle_soup.get();

        // Copy indices
        const fastgltf::Accessor& indices_accessor = m_asset->accessors[primitive.indicesAccessor.value()];
        log_gltf->trace("index count = {}", indices_accessor.count);
        triangle_soup.index_data.resize(indices_accessor.count);
        fastgltf::iterateAccessorWithIndex<uint32_t>(
            m_asset.get(),
            indices_accessor,
            [&](uint32_t index_value, std::size_t index) {
                triangle_soup.index_data[index] = index_value;
            }
        );

        // Gather attributes
        std::size_t vertex_count = std::numeric_limits<std::size_t>::max();
        triangle_soup.vertex_format.streams.emplace_back(0);
        for (std::size_t i = 0, end = primitive.attributes.size(); i < end; ++i) {
            const fastgltf::Attribute&                     gltf_attribute        = primitive.attributes[i];
            const erhe::dataformat::Vertex_attribute_usage attribute_usage_type  = to_erhe(gltf_attribute.name);
            const std::size_t                              attribute_usage_index = get_attribute_index(gltf_attribute.name);
            const fastgltf::Accessor&                      accessor              = m_asset->accessors[gltf_attribute.accessorIndex];
            const erhe::dataformat::Format                 format                = to_erhe_attribute(accessor);
            vertex_count = std::min(accessor.count, vertex_count);
            triangle_soup.vertex_format.streams.front().emplace_back(
                format,
                attribute_usage_type,
                attribute_usage_index
            );
        }
        std::size_t vertex_stride = triangle_soup.vertex_format.streams.front().stride;
        ERHE_VERIFY(triangle_soup.vertex_format.streams.size() == 1);
        triangle_soup.vertex_data.resize(vertex_count * vertex_stride);
        const std::vector<erhe::dataformat::Attribute_stream> erhe_attributes = triangle_soup.vertex_format.get_attributes();

        // Gather vertex data
        for (std::size_t i = 0, end = primitive.attributes.size(); i < end; ++i) {
            const fastgltf::Attribute&                     gltf_attribute  = primitive.attributes[i];
            const erhe::dataformat::Vertex_attribute_usage attribute_usage = to_erhe(gltf_attribute.name);
            const std::size_t                              attribute_index = get_attribute_index(gltf_attribute.name);
            const fastgltf::Accessor&                      accessor        = m_asset->accessors[gltf_attribute.accessorIndex];
            const erhe::dataformat::Attribute_stream&      erhe_attribute  = erhe_attributes[i];
            ERHE_VERIFY(erhe_attribute.attribute != nullptr);
            ERHE_VERIFY(erhe_attribute.stream != nullptr);
            copyComponentsFromAccessor(
                m_asset.get(),
                accessor,
                triangle_soup.vertex_data.data() + erhe_attribute.attribute->offset,
                erhe_attribute.stream->stride
            );

            log_gltf->trace(
                "Primitive attribute[{}]: name = {}, attribute type = {}[{}], "
                "component type = {}, accessor type = {}, normalized = {}, count = {}, "
                "accessor offset = {}",
                i,
                gltf_attribute.name.c_str(),
                erhe::dataformat::c_str(attribute_usage), // semantics
                attribute_index,
                c_str(accessor.componentType),
                fastgltf::getAccessorTypeName(accessor.type),
                accessor.normalized != 0,
                accessor.count,
                accessor.byteOffset
            );
        }
    }
    auto get_primitive_geometry(const fastgltf::Primitive& primitive, Primitive_entry& primitive_entry)
    {
        ERHE_PROFILE_FUNCTION();

        if (!primitive.indicesAccessor.has_value()) {
            return; // TODO
        }
        primitive_entry.index_accessor = primitive.indicesAccessor.value();
        primitive_entry.attribute_accessors.clear();
        for (std::size_t i = 0, end = primitive.attributes.size(); i < end; ++i) {
            const fastgltf::Attribute& attribute = primitive.attributes[i];
            primitive_entry.attribute_accessors.push_back(attribute.accessorIndex);
        }

        for (const auto& old_entry : m_primitive_entries) {
            if (old_entry.index_accessor != primitive_entry.index_accessor) continue;
            if (old_entry.attribute_accessors != primitive_entry.attribute_accessors) continue;
            // Found existing entry
            primitive_entry = old_entry;
            return;
        }

        load_new_primitive_geometry(primitive, primitive_entry);
    }

    void parse_primitive(
        const std::shared_ptr<erhe::scene::Mesh>& erhe_mesh,
        const fastgltf::Mesh&                     mesh,
        const std::size_t                         primitive_index
    )
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Primitive& primitive = mesh.primitives[primitive_index];
        std::string name = fmt::format("{}[{}]", mesh.name.c_str(), primitive_index);
        std::shared_ptr<erhe::primitive::Material> erhe_material = primitive.materialIndex.has_value()
            ? m_data_out.materials.at(primitive.materialIndex.value())
            : std::shared_ptr<erhe::primitive::Material>{};

        Primitive_entry primitive_entry;
        get_primitive_geometry(primitive, primitive_entry);

        erhe::primitive::Primitive new_primitive{primitive_entry.triangle_soup};
        erhe_mesh->add_primitive(new_primitive, erhe_material);
    }
    void parse_skin(const std::size_t skin_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Skin& skin = m_asset->skins[skin_index];
        const std::string skin_name = safe_resource_name(skin.name, "skin", skin_index);
        log_gltf->info("Skin: skin index = {}, name = {}", skin_index, skin_name);

        auto erhe_skin = std::make_shared<erhe::scene::Skin>(skin_name);
        erhe_skin->set_source_path(m_arguments.path);
        m_data_out.skins[skin_index] = erhe_skin;
        erhe_skin->enable_flag_bits(
            Item_flags::content    |
            Item_flags::show_in_ui |
            Item_flags::id
        );
        erhe_skin->skin_data.joints.resize(skin.joints.size());
        erhe_skin->skin_data.inverse_bind_matrices.resize(skin.joints.size());
        for (std::size_t i = 0, end = skin.joints.size(); i < end; ++i) {
            const std::size_t joint_node_index = skin.joints[i];
            auto& erhe_joint_node = m_data_out.nodes.at(joint_node_index);
            ERHE_VERIFY(erhe_joint_node);
            erhe_skin->skin_data.joints[i] = erhe_joint_node;
        }

        if (skin.inverseBindMatrices.has_value()) {
            const fastgltf::Accessor& inverseBindMatrixAccessor = m_asset->accessors[skin.inverseBindMatrices.value()];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
                m_asset.get(), inverseBindMatrixAccessor,
                [&](const fastgltf::math::fmat4x4 matrix, std::size_t index) {
                    glm::mat4 glm_matrix = glm::make_mat4x4(matrix.data());
                    erhe_skin->skin_data.inverse_bind_matrices[index] = glm_matrix;
                }
            );
        }

        //if (skin->skeleton != nullptr) {
        //    const std::size_t skeleton_node_index = skin->skeleton - m_data->nodes;
        //    erhe_skin->skin_data.skeleton = m_data_out.nodes.at(skeleton_node_index);
        //} else {
        //    erhe_skin->skin_data.skeleton.reset();;
        //}
    }
    void parse_mesh(const std::size_t mesh_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Mesh& mesh = m_asset->meshes[mesh_index];
        const std::string mesh_name = safe_resource_name(mesh.name, "mesh", mesh_index);
        log_gltf->trace("Mesh: mesh index = {}, name = {}", mesh_index, mesh_name);

        auto erhe_mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
        erhe_mesh->set_source_path(m_arguments.path);
        erhe_mesh->layer_id = m_arguments.mesh_layer_id;
        m_data_out.meshes[mesh_index] = erhe_mesh;
        erhe_mesh->enable_flag_bits(
            Item_flags::content     |
            Item_flags::visible     |
            Item_flags::show_in_ui  |
            Item_flags::shadow_cast |
            Item_flags::opaque      |
            Item_flags::id
        );
        for (std::size_t i = 0, end = mesh.primitives.size(); i < end; ++i) {
            parse_primitive(erhe_mesh, mesh, i);
        }
    }

    void parse_node(const std::size_t node_index, const std::shared_ptr<erhe::scene::Node>& parent)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Node& node = m_asset->nodes[node_index];

        const std::string node_name = safe_resource_name(node.name, "node", node_index);
        log_gltf->trace("Node: node index = {}, name = {}", node_index, node.name);
        auto erhe_node = std::make_shared<erhe::scene::Node>(node_name);
        erhe_node->set_source_path(m_arguments.path);
        erhe_node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
        m_data_out.nodes[node_index] = erhe_node;
        erhe_node->Hierarchy::set_parent(parent);
        parse_node_transform(node, erhe_node);

        for (std::size_t i = 0, end = node.children.size(); i < end; ++i) {
            std::size_t child_index = node.children[i];
            parse_node(child_index, erhe_node);
        }

        for (std::size_t i = 0, end = node.children.size(); i < end; ++i) {
            std::size_t child_node_index = node.children[i];
            auto& erhe_child_node = m_data_out.nodes.at(child_node_index);
            erhe_child_node->set_parent(erhe_node);
        }

        if (node.cameraIndex.has_value()) {
            const std::size_t camera_index = node.cameraIndex.value();
            erhe_node->attach(m_data_out.cameras[camera_index]);
        }

        if (node.lightIndex.has_value()) {
            const std::size_t light_index = node.lightIndex.value();
            erhe_node->attach(m_data_out.lights[light_index]);
        }

        if (node.meshIndex.has_value()) {
            const std::size_t mesh_index = node.meshIndex.value();
            const erhe::scene::Mesh& template_mesh = *m_data_out.meshes[mesh_index].get();

            // Mesh needs to be cloned, because erhe currently puts skin into the mesh.
            std::shared_ptr<erhe::scene::Mesh> erhe_mesh = std::make_shared<erhe::scene::Mesh>(
                template_mesh,
                erhe::for_clone{true}
            );
            if (node.skinIndex.has_value()) {
                const std::size_t skin_index = node.skinIndex.value();
                if (!m_data_out.skins[skin_index]) {
                    parse_skin(skin_index);
                }
                erhe_mesh->skin = m_data_out.skins[skin_index];
            }
            erhe_node->attach(erhe_mesh);
        }
    }
};

[[nodiscard]] auto format_duration(std::chrono::steady_clock::duration duration) -> std::string
{
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(duration);
    auto s = duration_cast<seconds>(ms);
    ms -= duration_cast<milliseconds>(s);
    auto m = duration_cast<minutes>(s);
    s -= duration_cast<seconds>(m);
    auto h = duration_cast<hours>(m);
    m -= duration_cast<minutes>(h);

    const int hours = static_cast<int>(h.count());
    const int minutes = static_cast<int>(m.count());
    const int seconds = static_cast<int>(s.count());
    const int milliseconds = static_cast<int>(ms.count());
    if (hours > 0) {
        return fmt::format("{}:{:02}:{:02}.{:03}", hours, minutes, seconds, milliseconds);
    } else if (minutes) {
        return fmt::format("{}:{:02}.{:03}", minutes, seconds, milliseconds);
    } else {
        return fmt::format("{}.{:03}", seconds, milliseconds);
    }
}

auto parse_gltf(const Gltf_parse_arguments& arguments) -> Gltf_data
{
    ERHE_PROFILE_FUNCTION();

    erhe::time::Timer timer{"parse_gltf"};
    timer.begin();

    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(arguments.path);
    if (data.error() != fastgltf::Error::None) {
        log_gltf->error("glTF load error: {}", fastgltf::getErrorMessage(data.error()));
        return {};
    }

    fastgltf::Parser fastgltf_parser;
    fastgltf::Expected<fastgltf::Asset> asset = fastgltf_parser.loadGltf(
        data.get(),
        arguments.path.parent_path(),
        fastgltf::Options::LoadExternalBuffers // TODO Consider | fastgltf::Options::DecomposeNodeMatrices
    );
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        log_gltf->error("glTF parse error: {}", fastgltf::getErrorMessage(error));
        return {};
    }

    Gltf_data result;
    Gltf_parser erhe_parser{std::move(asset), result, arguments};
    erhe_parser.parse_and_build();

    timer.end();
    if (timer.duration().has_value()) {
        log_gltf->info("glTF loaded {} in {}", arguments.path.string(), format_duration(timer.duration().value()));
    }

    return result;
}

auto scan_gltf(std::filesystem::path path) -> Gltf_scan
{
    ERHE_PROFILE_FUNCTION();

    erhe::time::Timer timer{"scan_gltf"};
    timer.begin();

    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        log_gltf->error("glTF load error: {}", fastgltf::getErrorMessage(data.error()));
        return {};
    }

    fastgltf::Parser fastgltf_parser;
    fastgltf::Expected<fastgltf::Asset> asset_expected = fastgltf_parser.loadGltf(
        data.get(),
        path.parent_path(),
        fastgltf::Options::None
    );
    if (auto error = asset_expected.error(); error != fastgltf::Error::None) {
        log_gltf->error("glTF parse error: {}", fastgltf::getErrorMessage(error));
        return {};
    }

    fastgltf::Asset* asset = asset_expected.get_if();
    if (asset == nullptr) {
        return {};
    }

    const auto resource_name = [&](
        std::string_view  name,
        std::string_view  resource_type,
        const std::size_t resource_index
    ) -> std::string
    {
        if (name.empty() || name.size() == 0) {
            return fmt::format("{}-{}-{}", path.filename().string(), resource_type, resource_index);
        }
        return std::string{name};
    };

    Gltf_scan result;
    result.images.resize(asset->images.size());
    for (std::size_t i = 0, end = asset->images.size(); i < end; ++i) {
        result.images[i] = resource_name(asset->images[i].name, "image", i);
    }

    result.samplers.resize(asset->samplers.size());
    for (std::size_t i = 0, end = asset->samplers.size(); i < end; ++i) {
        result.samplers[i] = resource_name(asset->samplers[i].name, "sampler", i);
    }

    result.materials.resize(asset->materials.size());
    for (std::size_t i = 0, end = asset->materials.size(); i < end; ++i) {
        result.materials[i] = resource_name(asset->materials[i].name, "material", i);
    }

    result.cameras.resize(asset->cameras.size());
    for (std::size_t i = 0, end = asset->cameras.size(); i < end; ++i) {
        result.cameras[i] = resource_name(asset->cameras[i].name, "camera", i);
    }

    result.lights.resize(asset->lights.size());
    for (std::size_t i = 0, end = asset->lights.size(); i < end; ++i) {
        result.lights[i] = resource_name(asset->lights[i].name, "light", i);
    }

    result.meshes.resize(asset->meshes.size());
    for (std::size_t i = 0, end = asset->meshes.size(); i < end; ++i) {
        result.meshes[i] = resource_name(asset->meshes[i].name, "mesh", i);
    }

    result.nodes.resize(asset->nodes.size());
    for (std::size_t i = 0, end = asset->nodes.size(); i < end; ++i) {
        result.nodes[i] = resource_name(asset->nodes[i].name, "node", i);
    }

    result.skins.resize(asset->skins.size());
    for (std::size_t i = 0, end = asset->skins.size(); i < end; ++i) {
        result.skins[i] = resource_name(asset->skins[i].name, "skin", i);
    }

    result.animations.resize(asset->animations.size());
    for (std::size_t i = 0, end = asset->animations.size(); i < end; ++i) {
        result.animations[i] = resource_name(asset->animations[i].name, "animation", i);
    }

    result.scenes.resize(asset->scenes.size());
    for (std::size_t i = 0, end = asset->scenes.size(); i < end; ++i) {
        result.scenes[i] = resource_name(asset->scenes[i].name, "scene", i);
    }

    timer.end();
    if (timer.duration().has_value()) {
        log_gltf->info("glTF scanned {} in {}", path.string(), format_duration(timer.duration().value()));
    }

    return result;
}

class Gltf_exporter
{
public:
    Gltf_exporter(const erhe::scene::Node& root_node, bool binary)
        : m_erhe_root_node{root_node}
        , m_binary        {binary}
    {
    }

    [[nodiscard]] auto export_gltf() -> std::string;

private:
    [[nodiscard]] static auto from_erhe(const erhe::scene::Trs_transform& erhe_trs_transform) -> fastgltf::TRS
    {
        // glm quat has [w x y z], gltf has [x y z w]

        const glm::vec3 t = erhe_trs_transform.get_translation();
        const glm::quat r = erhe_trs_transform.get_rotation();
        const glm::vec3 s = erhe_trs_transform.get_scale();

        return fastgltf::TRS{
            .translation = fastgltf::math::fvec3{t.x, t.y, t.z},
            .rotation    = fastgltf::math::fquat{r[0], r[1], r[2], r[3]},
            //.rotation    = fastgltf::math::fquat{r[1], r[2], r[3], r[0]},
            .scale       = fastgltf::math::fvec3{s.x, s.y, s.z}
        };
    }

    struct Export_entry
    {
        std::size_t                      index_buffer;
        std::size_t                      index_buffer_view;
        std::size_t                      index_buffer_accessor;
        std::size_t                      vertex_buffer;
        std::size_t                      vertex_buffer_view;
        std::vector<fastgltf::Attribute> attributes;
    };
    void add_index_data_source(
        Export_entry&              entry,
        std::size_t                vertex_count, 
        std::size_t                index_count, 
        std::span<const std::byte> index_data_source
    )
    {
        const std::size_t index_data_size = index_count * sizeof(uint32_t);

        entry.index_buffer = m_gltf_asset.buffers.size();
        fastgltf::Buffer gltf_index_buffer{
            .byteLength = index_data_size,
            .data = fastgltf::sources::Vector{
                .bytes    = std::vector<std::byte>{index_data_source.begin(), index_data_source.end()},
                .mimeType = fastgltf::MimeType::GltfBuffer
            },
            .name = {}
        };

        m_gltf_asset.buffers.emplace_back(std::move(gltf_index_buffer));

        entry.index_buffer_view = m_gltf_asset.bufferViews.size();
        fastgltf::BufferView buffer_view{};
        buffer_view.bufferIndex = entry.index_buffer;
        buffer_view.byteOffset  = 0;
        buffer_view.byteLength  = index_data_size;
        buffer_view.target      = fastgltf::BufferTarget::ElementArrayBuffer;
        m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));

        fastgltf::AccessorBoundsArray min_value{1, fastgltf::AccessorBoundsArray::BoundsType::int64};
        fastgltf::AccessorBoundsArray max_value{1, fastgltf::AccessorBoundsArray::BoundsType::int64};
        max_value.set<std::int64_t>(0, static_cast<std::int64_t>(vertex_count - 1));
        min_value.set<std::int64_t>(0, 0);
        fastgltf::Accessor indices_accessor{
            .byteOffset      = 0,
            .count           = index_count,
            .type            = fastgltf::AccessorType::Scalar,
            .componentType   = fastgltf::ComponentType::UnsignedInt,
            .normalized      = false,
            .max             = {},
            .min             = {},
            .bufferViewIndex = entry.index_buffer_view,
            .sparse          = {},
            .name            = "indices"
        };
        indices_accessor.max = std::move(max_value);
        indices_accessor.min = std::move(min_value);
        entry.index_buffer_accessor = m_gltf_asset.accessors.size();
        m_gltf_asset.accessors.emplace_back(std::move(indices_accessor));
    }
    void add_index_data_source(Export_entry& entry, size_t vertex_count, const std::vector<uint32_t>& index_data_source)
    {
        const std::byte* const index_data      = reinterpret_cast<const std::byte*>(index_data_source.data());
        const std::size_t      index_count     = index_data_source.size();
        const std::size_t      index_data_size = index_count * sizeof(uint32_t);
        add_index_data_source(entry, index_count, vertex_count, std::span<const std::byte>(index_data, index_data_size));
    }
    void add_vertex_data_source(
        Export_entry&                          entry,
        std::size_t                            vertex_count,
        const erhe::dataformat::Vertex_format& vertex_format,
        std::span<const std::byte>             vertex_data_source
    )
    {
        entry.vertex_buffer = m_gltf_asset.buffers.size();
        fastgltf::Buffer gltf_vertex_buffer{
            .byteLength = vertex_data_source.size_bytes(),
            .data = fastgltf::sources::Vector{
                .bytes    = std::vector<std::byte>{vertex_data_source.begin(), vertex_data_source.end()},
                .mimeType = fastgltf::MimeType::GltfBuffer
            },
            .name = {}
        };
        m_gltf_asset.buffers.emplace_back(std::move(gltf_vertex_buffer));

        entry.vertex_buffer_view = m_gltf_asset.bufferViews.size();

        fastgltf::BufferView buffer_view{};
        buffer_view.bufferIndex = entry.vertex_buffer;
        buffer_view.byteOffset  = 0;
        buffer_view.byteLength  = vertex_data_source.size_bytes();
        buffer_view.byteStride  = vertex_format.streams.front().stride,
        buffer_view.target      = fastgltf::BufferTarget::ArrayBuffer;
        m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));

        const std::vector<erhe::dataformat::Attribute_stream>& erhe_attributes = vertex_format.get_attributes();
        //const std::size_t vertex_stride = vertex_format.stride();
        for (const erhe::dataformat::Attribute_stream& erhe_attribute : erhe_attributes) {
            ERHE_VERIFY(erhe_attribute.attribute != nullptr);
            ERHE_VERIFY(erhe_attribute.stream != nullptr);
            const std::string attribute_name = vertex_attribute_usage_from_erhe(*erhe_attribute.attribute);
            if (attribute_name.empty()) {
                continue; // TODO export erhe-specific non-standard attributes?
            }

            // Scan for attribute min and max
            const std::size_t dimension = get_component_count(erhe_attribute.attribute->format);
            fastgltf::AccessorBoundsArray min_value{dimension, fastgltf::AccessorBoundsArray::BoundsType::float64};
            fastgltf::AccessorBoundsArray max_value{dimension, fastgltf::AccessorBoundsArray::BoundsType::float64};

            for (size_t c = 0; c < dimension; ++c) {
                max_value.set<double>(c, static_cast<double>(std::numeric_limits<float>::lowest()));
                min_value.set<double>(c, static_cast<double>(std::numeric_limits<float>::max()));
            }

            const std::byte* attribute_base_ = vertex_data_source.data() + erhe_attribute.attribute->offset;
            const std::uint8_t* attribute_base = reinterpret_cast<const std::uint8_t*>(attribute_base_);
            for (size_t i = 0; i < vertex_count; ++i) {
                float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                const std::uint8_t* src = attribute_base + erhe_attribute.stream->stride * i;
                std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&v[0]);
                erhe::dataformat::convert(
                    src, erhe_attribute.attribute->format,
                    dst, erhe::dataformat::Format::format_32_vec4_float, 
                    1.0f
                );
                for (std::size_t c = 0; c < dimension; ++c) {
                    min_value.set<double>(c, std::min(min_value.get<double>(c), static_cast<double>(v[c])));
                    max_value.set<double>(c, std::max(max_value.get<double>(c), static_cast<double>(v[c])));
                }
            }
            fastgltf::Accessor accessor{
                .byteOffset      = erhe_attribute.attribute->offset,
                .count           = vertex_count,
                .type            = get_accessor_type (erhe_attribute.attribute->format),
                .componentType   = get_component_type(erhe_attribute.attribute->format),
                .normalized      = get_normalized    (erhe_attribute.attribute->format),
                .max             = {},
                .min             = {},
                .bufferViewIndex = entry.vertex_buffer_view,
                .sparse          = {},
                .name            = FASTGLTF_STD_PMR_NS::string{attribute_name}
            };
            accessor.max = std::move(max_value);
            accessor.min = std::move(min_value);
            const size_t accessor_index = m_gltf_asset.accessors.size();
            m_gltf_asset.accessors.emplace_back(std::move(accessor));
            FASTGLTF_STD_PMR_NS::string gltf_attribute_name{attribute_name};
            entry.attributes.emplace_back(gltf_attribute_name, accessor_index);
        }
    }
    void add_vertex_data_source(
        Export_entry&                          entry,
        std::size_t                            vertex_count,
        const erhe::dataformat::Vertex_format& vertex_format,
        const std::vector<uint8_t>&            vertex_data_source
    )
    {
        const std::byte*  vertex_data      = reinterpret_cast<const std::byte*>(vertex_data_source.data());
        const std::size_t vertex_data_size = vertex_data_source.size();
        add_vertex_data_source(entry, vertex_count, vertex_format, std::span<const std::byte>(vertex_data, vertex_data_size));
    }

    std::unordered_map<erhe::primitive::Triangle_soup*, Export_entry> m_erhe_triangle_soup_entries;
    [[nodiscard]] auto process_triangle_soup(erhe::primitive::Triangle_soup* triangle_soup) -> Export_entry
    {
        ERHE_VERIFY(triangle_soup != nullptr);

        const auto fi = m_erhe_triangle_soup_entries.find(triangle_soup);
        if (fi != m_erhe_triangle_soup_entries.end()) {
            return fi->second;
        }

        Export_entry entry{};
        add_index_data_source(entry, triangle_soup->get_vertex_count(), triangle_soup->index_data);
        add_vertex_data_source(entry, triangle_soup->get_vertex_count(), triangle_soup->vertex_format, triangle_soup->vertex_data);

        m_erhe_triangle_soup_entries.insert({triangle_soup, entry});
        return entry;
    }

    std::unordered_map<erhe::geometry::Geometry*, Export_entry> m_erhe_geometry_entries;
    [[nodiscard]] auto process_geometry(erhe::geometry::Geometry* geometry) -> Export_entry
    {
        ERHE_VERIFY(geometry != nullptr);

        const auto fi = m_erhe_geometry_entries.find(geometry);
        if (fi != m_erhe_geometry_entries.end()) {
            return fi->second;
        }

        const GEO::Mesh& geo_mesh = geometry->get_mesh();

        // TODO
        const erhe::dataformat::Vertex_format vertex_format{
            {
                0,
                {
                    { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position,  0},
                    { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal,    0},
                    { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0}
                }
            }
        };

        const Mesh_info   mesh_info     = get_mesh_info(geo_mesh);
        const std::size_t vertex_stride = vertex_format.streams.front().stride;
        const std::size_t index_stride  = 4;
        const std::size_t index_count   = mesh_info.index_count_fill_triangles;
        const std::size_t vertex_count  = mesh_info.vertex_count_corners;

        erhe::primitive::Buffer_mesh buffer_mesh;
        erhe::buffer::Cpu_buffer vertex_buffer{"", vertex_count * vertex_stride};
        erhe::buffer::Cpu_buffer index_buffer {"", index_count * index_stride};

        erhe::primitive::Cpu_buffer_sink buffer_sink{{&vertex_buffer}, index_buffer};
        const erhe::primitive::Build_info build_info{
            .primitive_types = {
                .fill_triangles = true,
            },
            .buffer_info = {
                .normal_style  = erhe::primitive::Normal_style::corner_normals,
                .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
                .vertex_format = vertex_format,
                .buffer_sink   = buffer_sink
            }
        };

        erhe::primitive::Element_mappings dummy_mappings;
        const bool build_ok = erhe::primitive::build_buffer_mesh(
            buffer_mesh,
            geo_mesh, 
            build_info, 
            dummy_mappings, 
            erhe::primitive::Normal_style::corner_normals
        );
        ERHE_VERIFY(build_ok); // TODO

        Export_entry entry;
        add_index_data_source(entry, vertex_count, index_count, index_buffer.span());
        add_vertex_data_source(entry, vertex_count, vertex_format, vertex_buffer.span());

        m_erhe_geometry_entries.insert({geometry, entry});
        return entry;
    }

    std::unordered_map<const erhe::primitive::Material*, std::size_t> m_exported_materials;
    [[nodiscard]] auto process_material(const erhe::primitive::Material* material) -> std::size_t
    {
        ERHE_VERIFY(material != nullptr);

        const auto fi = m_exported_materials.find(material);
        if (fi != m_exported_materials.end()) {
            return fi->second;
        }

        const size_t gltf_material_index = m_gltf_asset.materials.size();
        {
            fastgltf::Material gltf_material{
                .pbrData = {
                    .baseColorFactor = {
                        material->base_color.r,
                        material->base_color.g,
                        material->base_color.b,
                        material->base_color.a
                    },
                    .metallicFactor           = material->metallic,
                    .roughnessFactor          = material->roughness.x,
                    .baseColorTexture         = {},
                    .metallicRoughnessTexture = {}
                },
                .normalTexture    = {},
                .occlusionTexture = {},
                .emissiveTexture  = {},
                .emissiveFactor = {
                    material->emissive.r,
                    material->emissive.g,
                    material->emissive.b
                },
                .anisotropy                               = {},
                .clearcoat                                = {},
                .iridescence                              = {},
                .sheen                                    = {},
                .specular                                 = {},
                .transmission                             = {},
                .volume                                   = {},
                .packedNormalMetallicRoughnessTexture     = {},
                .packedOcclusionRoughnessMetallicTextures = {},
                .name                                     = FASTGLTF_STD_PMR_NS::string{material->get_name()}
            };
            m_gltf_asset.materials.push_back(std::move(gltf_material));
        }
        m_exported_materials.insert({material, gltf_material_index});
        return gltf_material_index;
    }

    std::unordered_map<const erhe::scene::Mesh*, std::size_t> m_erhe_mesh_to_gltf_mesh_index;
    [[nodiscard]] auto process_mesh(const erhe::scene::Mesh* erhe_mesh) -> std::size_t
    {
        ERHE_VERIFY(erhe_mesh != nullptr);

        // Check if mesh has already been exported
        const auto fi = m_erhe_mesh_to_gltf_mesh_index.find(erhe_mesh);
        if (fi != m_erhe_mesh_to_gltf_mesh_index.end()) {
            return fi->second;
        }

        fastgltf::Mesh gltf_mesh{};
        for (const erhe::primitive::Primitive& erhe_primitive : erhe_mesh->get_primitives()) {
            fastgltf::Primitive gltf_primitive;
            const erhe::primitive::Primitive_render_shape* primitive_render_shape = erhe_primitive.render_shape.get();
            if (primitive_render_shape == nullptr) {
                continue;
            }
            const std::shared_ptr<erhe::primitive::Triangle_soup>& triangle_soup = primitive_render_shape->get_triangle_soup();
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive_render_shape->get_geometry_const();
            if (!geometry && !triangle_soup) {
                log_gltf->warn("Mesh primitive has neither triangle soup nor geometry");
                continue;
            }
            Export_entry export_entry = triangle_soup
                ? process_triangle_soup(triangle_soup.get())
                : process_geometry(geometry.get());
            for (const fastgltf::Attribute& attribute : export_entry.attributes) {
                gltf_primitive.attributes.emplace_back(attribute.name, attribute.accessorIndex);
            }
            gltf_primitive.indicesAccessor = export_entry.index_buffer_accessor;
            if (erhe_primitive.material) {
                gltf_primitive.materialIndex = process_material(erhe_primitive.material.get());
            }
            gltf_mesh.primitives.emplace_back(std::move(gltf_primitive));
        }
        std::size_t gltf_mesh_index = m_gltf_asset.meshes.size();
        m_gltf_asset.meshes.emplace_back(std::move(gltf_mesh));
        m_erhe_mesh_to_gltf_mesh_index.insert({erhe_mesh, gltf_mesh_index});
        return gltf_mesh_index;
    }

    std::unordered_map<const erhe::scene::Camera*, std::size_t> m_erhe_camera_to_gltf_camera_index;
    [[nodiscard]] auto process_camera(erhe::scene::Camera* erhe_camera) -> std::size_t
    {
        ERHE_VERIFY(erhe_camera != nullptr);
        erhe::scene::Projection* erhe_projection = erhe_camera->projection();
        ERHE_VERIFY(erhe_projection != nullptr);

        auto fi = m_erhe_camera_to_gltf_camera_index.find(erhe_camera);
        if (fi != m_erhe_camera_to_gltf_camera_index.end()) {
            return fi->second;
        }

        fastgltf::Camera gltf_camera{};
        switch (erhe_projection->projection_type) {
            case erhe::scene::Projection::Type::perspective_horizontal:
            case erhe::scene::Projection::Type::perspective_vertical:
            case erhe::scene::Projection::Type::perspective:
            case erhe::scene::Projection::Type::perspective_xr: {
                gltf_camera.camera = fastgltf::Camera::Perspective{
                    .aspectRatio = (erhe_projection->fov_right - erhe_projection->fov_left) / (erhe_projection->fov_up - erhe_projection->fov_down),
                    .yfov = erhe_projection->fov_up - erhe_projection->fov_down,
                    .zfar = std::max(erhe_projection->z_far, erhe_projection->z_near),
                    .znear = std::min(erhe_projection->z_far, erhe_projection->z_near)
                };
                break;
            }
            case erhe::scene::Projection::Type::orthogonal_horizontal:
            case erhe::scene::Projection::Type::orthogonal_vertical:
            case erhe::scene::Projection::Type::orthogonal:
            case erhe::scene::Projection::Type::orthogonal_rectangle: {
                gltf_camera.camera = fastgltf::Camera::Orthographic{
                    .xmag = erhe_projection->ortho_width,
                    .ymag = erhe_projection->ortho_height,
                    .zfar = std::max(erhe_projection->z_far, erhe_projection->z_near),
                    .znear = std::min(erhe_projection->z_far, erhe_projection->z_near)
                };
                break;
            }
            case erhe::scene::Projection::Type::other:
            case erhe::scene::Projection::Type::generic_frustum: {
                ERHE_FATAL("Not implemented");
            }
        }
        gltf_camera.name = erhe_camera->get_name();
        std::size_t gltf_camera_index = m_gltf_asset.cameras.size();
        m_gltf_asset.cameras.emplace_back(std::move(gltf_camera));
        m_erhe_camera_to_gltf_camera_index.insert({erhe_camera, gltf_camera_index});
        return gltf_camera_index;
    }

    auto process_node(const erhe::scene::Node& erhe_node) -> std::size_t
    {
        fastgltf::Node gltf_node{};

        gltf_node.name = erhe_node.get_name();
        gltf_node.transform = from_erhe(erhe_node.parent_from_node_transform());

        const std::shared_ptr<erhe::scene::Mesh> erhe_mesh = get_mesh(&erhe_node);
        if (erhe_mesh) {
            gltf_node.meshIndex = process_mesh(erhe_mesh.get());
        }

        const std::shared_ptr<erhe::scene::Camera> erhe_camera = get_camera(&erhe_node);
        if (erhe_camera) {
            gltf_node.cameraIndex = process_camera(erhe_camera.get());
        }

        erhe_node.for_each_child_const<erhe::scene::Node>(
            [this, &gltf_node](const erhe::scene::Node& erhe_child_node) -> bool
            {
                std::size_t gltf_child_node_index = process_node(erhe_child_node);
                gltf_node.children.push_back(gltf_child_node_index);
                return true;
            }
        );

        size_t gltf_node_index = m_gltf_asset.nodes.size();
        m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
        return gltf_node_index;
    }

    static auto get_index_stride(fastgltf::ComponentType componentType) -> std::size_t
    {
        switch (componentType) {
            case fastgltf::ComponentType::Invalid      : ERHE_FATAL("Bad index component type"); return 0;
            case fastgltf::ComponentType::Byte         : ERHE_FATAL("Bad index component type"); return 0;
            case fastgltf::ComponentType::UnsignedByte : return 1;
            case fastgltf::ComponentType::Short        : ERHE_FATAL("Bad index component type"); return 0;
            case fastgltf::ComponentType::UnsignedShort: return 2;
            case fastgltf::ComponentType::Int          : ERHE_FATAL("Bad index component type"); return 0;
            case fastgltf::ComponentType::UnsignedInt  : return 4;
            case fastgltf::ComponentType::Float        : ERHE_FATAL("Bad index component type"); return 0;
            case fastgltf::ComponentType::Double       : ERHE_FATAL("Bad index component type"); return 0;
            default:                                     ERHE_FATAL("Bad index component type"); return 0;
        }
    }

    void validate_buffers()
    {
        for (fastgltf::BufferView& buffer_view : m_gltf_asset.bufferViews) {
            fastgltf::Buffer& buffer = m_gltf_asset.buffers.at(buffer_view.bufferIndex);
            std::size_t start = buffer_view.byteOffset;
            std::size_t end = start + buffer_view.byteLength;
            ERHE_VERIFY(start < buffer.byteLength);
            ERHE_VERIFY(end <= buffer.byteLength);
        }
        for (fastgltf::Accessor& accessor : m_gltf_asset.accessors) {
            ERHE_VERIFY(accessor.bufferViewIndex.has_value());
            const fastgltf::BufferView& buffer_view = m_gltf_asset.bufferViews.at(accessor.bufferViewIndex.value());
            const fastgltf::Buffer& buffer = m_gltf_asset.buffers.at(buffer_view.bufferIndex);
            std::size_t stride = (buffer_view.target == fastgltf::BufferTarget::ArrayBuffer) 
                ? buffer_view.byteStride.value() 
                : get_index_stride(accessor.componentType);
            std::size_t start = buffer_view.byteOffset + accessor.byteOffset;
            std::size_t end = start + (accessor.count - 1) * stride; // TODO add bytes used by last vertex
            ERHE_VERIFY(start < buffer.byteLength);
            ERHE_VERIFY(end <= buffer.byteLength);
        }
    }

    void combine_buffers()
    {
        validate_buffers();

        fastgltf::sources::Vector combined_buffer;
        std::vector<std::size_t> buffer_offsets;
        buffer_offsets.reserve(m_gltf_asset.buffers.size());
        for (fastgltf::Buffer& buffer : m_gltf_asset.buffers) {
            const fastgltf::sources::Vector& byte_view = std::get<fastgltf::sources::Vector>(buffer.data);
            std::span<const std::byte> source_span{byte_view.bytes.data(), byte_view.bytes.size()};

            // Padding
            std::size_t combined_buffer_size = combined_buffer.bytes.size();
            while ((combined_buffer_size & 3) != 0) {
                combined_buffer.bytes.push_back(std::byte{0});
                ++combined_buffer_size;
            }
            buffer_offsets.push_back(combined_buffer_size);

            combined_buffer.bytes.insert(
                combined_buffer.bytes.end(),
                source_span.begin(),
                source_span.end()
            );
        }

        for (fastgltf::BufferView& buffer_view : m_gltf_asset.bufferViews) {
            std::size_t original_buffer = buffer_view.bufferIndex;
            buffer_view.bufferIndex = 0;
            buffer_view.byteOffset += buffer_offsets[original_buffer];
        }

        m_gltf_asset.buffers.clear();
        m_gltf_asset.buffers.emplace_back(combined_buffer.bytes.size(), std::move(combined_buffer));

        validate_buffers();
    }

private:
    const erhe::scene::Node& m_erhe_root_node;
    fastgltf::Asset          m_gltf_asset;
    bool                     m_binary{true};
};

auto Gltf_exporter::export_gltf() -> std::string
{
    m_gltf_asset.assetInfo = fastgltf::AssetInfo{
        .gltfVersion = "2.0",
        .copyright   = "",
        .generator   = "erhe"
    };
    m_gltf_asset.defaultScene = std::size_t{0};

    fastgltf::Scene scene{};
    m_erhe_root_node.for_each_child_const<erhe::scene::Node>(
        [this, &scene](const erhe::scene::Node& erhe_node) -> bool
        {
            size_t node_index = process_node(erhe_node);
            scene.nodeIndices.push_back(node_index);
            return true;
        }
    );
    m_gltf_asset.scenes.push_back(std::move(scene));

    combine_buffers();

    fastgltf::Exporter exporter{};

    //static_cast<void>(m_binary);
    if (m_binary) 
    {
        auto expected_result = exporter.writeGltfBinary(m_gltf_asset, fastgltf::ExportOptions::ValidateAsset);
        auto* result = expected_result.get_if();
        if (result == nullptr) {
            return {};
        }
        return std::string(
            reinterpret_cast<const char*>(result->output.data()),
            result->output.size()
        );
    } 
    else
    {
        auto expected_result = exporter.writeGltfJson(m_gltf_asset, 
            fastgltf::ExportOptions::ValidateAsset |
            fastgltf::ExportOptions::PrettyPrintJson);
        auto* result = expected_result.get_if();
        if (result == nullptr) {
            return {};
        }
        return result->output;
    }
}

[[nodiscard]] auto export_gltf(const erhe::scene::Node& root_node, bool binary) -> std::string
{
    Gltf_exporter exporter{root_node, binary};
    return exporter.export_gltf();
}

} // namespace erhe::gltf

