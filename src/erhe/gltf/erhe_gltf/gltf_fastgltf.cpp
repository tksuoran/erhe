// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "gltf_fastgltf.hpp"
#include "gltf_log.hpp"
#include "image_transfer.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
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

#include "erhe_verify/verify.hpp"

#include <simdjson.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "mikktspace/mikktspace.hpp"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <string_view>
#include <string>
#include <variant>
#include <vector>

using erhe::geometry::Mesh_info;
using erhe::geometry::get_mesh_info;

namespace erhe::gltf {

// glTF extras carriers for erhe-specific Material_data fields that do not
// have a standard glTF representation: bxdf_model (when not isotropic and
// not unlit -- unlit rides on KHR_materials_unlit), use_circular_brushed
// _metal, circular_brushed_metal_tex_coord, use_aniso_control, and the
// per-axis roughness_y. Round-trip preservation is import-extras-read +
// export-extras-write keyed on the glTF material index.
class Material_extras
{
public:
    std::optional<float>                                   roughness_y;
    std::optional<erhe::primitive::Bxdf_model>             bxdf_model;
    std::optional<erhe::primitive::Material_blending_mode> blending_mode;
    std::optional<bool>                                    use_circular_brushed_metal;
    std::optional<uint32_t>                                circular_brushed_metal_tex_coord;
    std::optional<bool>                                    use_aniso_control;
};

[[nodiscard]] auto bxdf_model_to_c_str(erhe::primitive::Bxdf_model model) -> const char*
{
    switch (model) {
        case erhe::primitive::Bxdf_model::unlit:                    return "unlit";
        case erhe::primitive::Bxdf_model::isotropic_brdf:           return "isotropic_brdf";
        case erhe::primitive::Bxdf_model::anisotropic_brdf:         return "anisotropic_brdf";
        case erhe::primitive::Bxdf_model::anisotropic_slope:        return "anisotropic_slope";
        case erhe::primitive::Bxdf_model::anisotropic_engine_ready: return "anisotropic_engine_ready";
    }
    return "isotropic_brdf";
}

[[nodiscard]] auto bxdf_model_from_string(std::string_view s) -> std::optional<erhe::primitive::Bxdf_model>
{
    if (s == "unlit")                    return erhe::primitive::Bxdf_model::unlit;
    if (s == "isotropic_brdf")           return erhe::primitive::Bxdf_model::isotropic_brdf;
    if (s == "anisotropic_brdf")         return erhe::primitive::Bxdf_model::anisotropic_brdf;
    if (s == "anisotropic_slope")        return erhe::primitive::Bxdf_model::anisotropic_slope;
    if (s == "anisotropic_engine_ready") return erhe::primitive::Bxdf_model::anisotropic_engine_ready;
    return std::nullopt;
}

// Material_blending_mode <-> string mappings for the extras carrier.
// Only the modes that glTF's alphaMode cannot represent need to round-trip
// through extras (multiply, add, subtract, screen_door). The OPAQUE / MASK
// / BLEND counterparts use the standard alphaMode field instead.
[[nodiscard]] auto blending_mode_to_c_str(erhe::primitive::Material_blending_mode mode) -> const char*
{
    switch (mode) {
        case erhe::primitive::Material_blending_mode::opaque:      return "opaque";
        case erhe::primitive::Material_blending_mode::alpha_blend: return "alpha_blend";
        case erhe::primitive::Material_blending_mode::multiply:    return "multiply";
        case erhe::primitive::Material_blending_mode::add:         return "add";
        case erhe::primitive::Material_blending_mode::subtract:    return "subtract";
        case erhe::primitive::Material_blending_mode::screen_door: return "screen_door";
        case erhe::primitive::Material_blending_mode::alpha_test:  return "alpha_test";
    }
    return "opaque";
}

[[nodiscard]] auto blending_mode_from_string(std::string_view s) -> std::optional<erhe::primitive::Material_blending_mode>
{
    if (s == "opaque")      return erhe::primitive::Material_blending_mode::opaque;
    if (s == "alpha_blend") return erhe::primitive::Material_blending_mode::alpha_blend;
    if (s == "multiply")    return erhe::primitive::Material_blending_mode::multiply;
    if (s == "add")         return erhe::primitive::Material_blending_mode::add;
    if (s == "subtract")    return erhe::primitive::Material_blending_mode::subtract;
    if (s == "screen_door") return erhe::primitive::Material_blending_mode::screen_door;
    if (s == "alpha_test")  return erhe::primitive::Material_blending_mode::alpha_test;
    return std::nullopt;
}


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
                case fastgltf::ComponentType::Byte         : return accessor.normalized ? Format::format_8_scalar_snorm  : Format::format_8_scalar_sint;
                case fastgltf::ComponentType::UnsignedByte : return accessor.normalized ? Format::format_8_scalar_unorm  : Format::format_8_scalar_uint;
                case fastgltf::ComponentType::Short        : return accessor.normalized ? Format::format_16_scalar_snorm : Format::format_16_scalar_sint;
                case fastgltf::ComponentType::UnsignedShort: return accessor.normalized ? Format::format_16_scalar_unorm : Format::format_16_scalar_uint;
                case fastgltf::ComponentType::Int          : return accessor.normalized ? Format::format_32_scalar_float : Format::format_32_scalar_sint;
                case fastgltf::ComponentType::UnsignedInt  : return accessor.normalized ? Format::format_32_scalar_float : Format::format_32_scalar_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_scalar_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec2: {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return accessor.normalized ? Format::format_8_vec2_snorm  : Format::format_8_vec2_sint;
                case fastgltf::ComponentType::UnsignedByte : return accessor.normalized ? Format::format_8_vec2_unorm  : Format::format_8_vec2_uint;
                case fastgltf::ComponentType::Short        : return accessor.normalized ? Format::format_16_vec2_snorm : Format::format_16_vec2_sint;
                case fastgltf::ComponentType::UnsignedShort: return accessor.normalized ? Format::format_16_vec2_unorm : Format::format_16_vec2_uint;
                case fastgltf::ComponentType::Int          : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec2_sint;
                case fastgltf::ComponentType::UnsignedInt  : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec2_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_vec2_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec3:{
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return accessor.normalized ? Format::format_8_vec3_snorm  : Format::format_8_vec3_sint;
                case fastgltf::ComponentType::UnsignedByte : return accessor.normalized ? Format::format_8_vec3_unorm  : Format::format_8_vec3_uint;
                case fastgltf::ComponentType::Short        : return accessor.normalized ? Format::format_16_vec3_snorm : Format::format_16_vec3_sint;
                case fastgltf::ComponentType::UnsignedShort: return accessor.normalized ? Format::format_16_vec3_unorm : Format::format_16_vec3_uint;
                case fastgltf::ComponentType::Int          : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec3_sint;
                case fastgltf::ComponentType::UnsignedInt  : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec3_uint;
                case fastgltf::ComponentType::Float        : return Format::format_32_vec3_float;
                default: break;
            }
        }
        case fastgltf::AccessorType::Vec4: {
            switch (accessor.componentType) {
                case fastgltf::ComponentType::Byte         : return accessor.normalized ? Format::format_8_vec4_snorm  : Format::format_8_vec4_sint;
                case fastgltf::ComponentType::UnsignedByte : return accessor.normalized ? Format::format_8_vec4_unorm  : Format::format_8_vec4_uint;
                case fastgltf::ComponentType::Short        : return accessor.normalized ? Format::format_16_vec4_snorm : Format::format_16_vec4_sint;
                case fastgltf::ComponentType::UnsignedShort: return accessor.normalized ? Format::format_16_vec4_unorm : Format::format_16_vec4_uint;
                case fastgltf::ComponentType::Int          : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec4_sint;
                case fastgltf::ComponentType::UnsignedInt  : return accessor.normalized ? Format::format_32_vec2_float : Format::format_32_vec4_uint;
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

[[nodiscard]] auto c_str(const fastgltf::AnimationInterpolation value) -> const char*
{
    switch (value) {
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

[[nodiscard]] auto to_erhe(const fastgltf::PrimitiveType value) -> erhe::graphics::Primitive_type
{
    // TODO Not all primitive types are natively supported by metal / vulkan
    switch (value) {
        case fastgltf::PrimitiveType::Points:        return erhe::graphics::Primitive_type::point;
        case fastgltf::PrimitiveType::Lines:         return erhe::graphics::Primitive_type::line;
        case fastgltf::PrimitiveType::LineLoop:      return erhe::graphics::Primitive_type::line;
        case fastgltf::PrimitiveType::LineStrip:     return erhe::graphics::Primitive_type::line_strip;
        case fastgltf::PrimitiveType::Triangles:     return erhe::graphics::Primitive_type::triangle;
        case fastgltf::PrimitiveType::TriangleStrip: return erhe::graphics::Primitive_type::triangle_strip;
        case fastgltf::PrimitiveType::TriangleFan:   return erhe::graphics::Primitive_type::triangle;
        default:                                     return erhe::graphics::Primitive_type::point;
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

using erhe::geometry::Mesh_info;
using erhe::geometry::get_mesh_info;

[[nodiscard]] auto to_erhe(const fastgltf::LightType gltf_light_type) -> erhe::scene::Light_type
{
    switch (gltf_light_type) {
        case fastgltf::LightType::Directional: return erhe::scene::Light_type::directional;
        case fastgltf::LightType::Point:       return erhe::scene::Light_type::point;
        case fastgltf::LightType::Spot:        return erhe::scene::Light_type::spot;
        default:                               return erhe::scene::Light_type::directional;
    }
}

[[nodiscard]] auto from_erhe(const erhe::scene::Light_type erhe_light_type) -> fastgltf::LightType
{
    switch (erhe_light_type) {
        case erhe::scene::Light_type::directional: return fastgltf::LightType::Directional;
        case erhe::scene::Light_type::point:       return fastgltf::LightType::Point;
        case erhe::scene::Light_type::spot:        return fastgltf::LightType::Spot;
        default:                                   return fastgltf::LightType::Directional;
    }
}

// erhe Item flags that round-trip through glTF node "extras" as
// {"erhe_flags": ["<name>", ...]}. Neither glTF 2.0 core nor the glTF 2.1
// proposals (KhronosGroup/glTF#2585 and related) define per-node
// editor/authoring flags such as "exclude from external-asset
// instantiation" (see doc/gltf_2_1_item_flags_comment.md), so
// erhe-specific bits ride in extras by name; unknown names are ignored on
// import so the list can grow without breaking older builds.
class Serialized_item_flag
{
public:
    uint64_t         bit;
    std::string_view name;
};

constexpr Serialized_item_flag c_serialized_item_flags[] = {
    { erhe::Item_flags::exclude_from_prefab, "exclude_from_prefab" }
};

[[nodiscard]] constexpr auto serialized_item_flags_mask() -> uint64_t
{
    uint64_t mask = 0;
    for (const Serialized_item_flag& flag : c_serialized_item_flags) {
        mask = mask | flag.bit;
    }
    return mask;
}

[[nodiscard]] auto is_number(std::string_view s) -> bool
{
    return 
        !s.empty() &&
        std::find_if(
            s.begin(), s.end(), [](unsigned char c) {
                return !std::isdigit(c);
            }
        ) == s.end();
}

[[nodiscard]] auto get_attribute_index(std::string_view lhs, std::string_view rhs) -> std::size_t
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

[[nodiscard]] auto is_indexed_attribute(std::string_view lhs, std::string_view rhs) -> bool
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

    std::string_view number_part = gltf_attribute_name.substr(last_underscore_pos + 1);
    bool is_indexed = is_number(number_part);
    if (!is_indexed) {
        return 0;
    }
    std::string number_string{number_part};
    int integer_value{0};
    try {
        integer_value = std::stoi(number_string);
    } catch (...) {
        static int counter = 0;
        ++counter; // breakpoint placeholder
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
        case erhe::dataformat::Format::format_32_scalar_uint          : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_scalar_sint          : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_scalar_float         : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_32_vec2_uint            : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec2_sint            : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec2_float           : return fastgltf::ComponentType::Float;
        case erhe::dataformat::Format::format_32_vec3_uint            : return fastgltf::ComponentType::UnsignedInt;
        case erhe::dataformat::Format::format_32_vec3_sint            : return fastgltf::ComponentType::Int;
        case erhe::dataformat::Format::format_32_vec3_float           : return fastgltf::ComponentType::Float;
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
        case erhe::dataformat::Format::format_16_scalar_float         : return false;
        case erhe::dataformat::Format::format_16_vec2_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec2_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec2_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec2_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec2_uint            : return false;
        case erhe::dataformat::Format::format_16_vec2_sint            : return false;
        case erhe::dataformat::Format::format_16_vec2_float           : return false;
        case erhe::dataformat::Format::format_16_vec3_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec3_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec3_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec3_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec3_uint            : return false;
        case erhe::dataformat::Format::format_16_vec3_sint            : return false;
        case erhe::dataformat::Format::format_16_vec3_float           : return false;
        case erhe::dataformat::Format::format_16_vec4_unorm           : return true;
        case erhe::dataformat::Format::format_16_vec4_snorm           : return true;
        case erhe::dataformat::Format::format_16_vec4_uscaled         : return false;
        case erhe::dataformat::Format::format_16_vec4_sscaled         : return false;
        case erhe::dataformat::Format::format_16_vec4_uint            : return false;
        case erhe::dataformat::Format::format_16_vec4_sint            : return false;
        case erhe::dataformat::Format::format_16_vec4_float           : return false;
        case erhe::dataformat::Format::format_32_scalar_uint          : return false;
        case erhe::dataformat::Format::format_32_scalar_sint          : return false;
        case erhe::dataformat::Format::format_32_scalar_float         : return false;
        case erhe::dataformat::Format::format_32_vec2_uint            : return false;
        case erhe::dataformat::Format::format_32_vec2_sint            : return false;
        case erhe::dataformat::Format::format_32_vec2_float           : return false;
        case erhe::dataformat::Format::format_32_vec3_uint            : return false;
        case erhe::dataformat::Format::format_32_vec3_sint            : return false;
        case erhe::dataformat::Format::format_32_vec3_float           : return false;
        case erhe::dataformat::Format::format_32_vec4_uint            : return false;
        case erhe::dataformat::Format::format_32_vec4_sint            : return false;
        case erhe::dataformat::Format::format_32_vec4_float           : return false;
        case erhe::dataformat::Format::format_packed1010102_vec4_unorm: return true;
        case erhe::dataformat::Format::format_packed1010102_vec4_snorm: return true;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint : return false;
        case erhe::dataformat::Format::format_packed1010102_vec4_sint : return false;
        case erhe::dataformat::Format::format_packed111110_vec3_unorm : return false;
        default: return false;
    }
}

using Item_flags = erhe::Item_flags;

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
// because I need stride
void copyFromAccessorWithOutStride(const fastgltf::Asset& asset, const fastgltf::Accessor& accessor, void* dest, std::size_t destStride)
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

namespace {

// fastgltf's loadGltf() rejects an empty base directory with
// fastgltf::Error::InvalidPath ("The glTF directory passed to load*GLTF is
// invalid"). A relative path with no directory component -- e.g. "scene.glb"
// resolved against the process cwd, which is how load_scene hands a companion
// .glb whose scene file lives in the working directory -- has an empty
// parent_path(). Its correct resolution base for external glTF resources is the
// current directory, so substitute "." rather than letting the whole parse fail
// (which silently drops every material and texture, leaving meshes unlit).
[[nodiscard]] auto gltf_base_directory(const std::filesystem::path& path) -> std::filesystem::path
{
    std::filesystem::path directory = path.parent_path();
    if (directory.empty()) {
        return std::filesystem::path{"."};
    }
    return directory;
}

} // namespace

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

        log_gltf->trace("parsing files and external assets");
        parse_files_and_external_assets();

        /// log_gltf->trace("parsing images");
        m_data_out.images.resize(m_asset->images.size());
        /// for (std::size_t i = 0, end = m_asset->images.size(); i < end; ++i) {
        ///     parse_image(i);
        /// }

        log_gltf->trace("parsing samplers");
        m_data_out.samplers.resize(m_asset->samplers.size());
        for (std::size_t i = 0, end = m_asset->samplers.size(); i < end; ++i) {
            parse_sampler(i);
        }

        const std::size_t material_count = m_asset->materials.size();
        log_gltf->trace("parsing {} materials", material_count);
        m_data_out.materials.resize(material_count);
        for (std::size_t i = 0; i < material_count; ++i) {
            parse_material(i);
        }

        const std::size_t camera_count = m_asset->cameras.size();
        log_gltf->trace("parsing {} cameras", camera_count);
        m_data_out.cameras.resize(camera_count);
        for (std::size_t i = 0; i < camera_count; ++i) {
            parse_camera(i);
        }

        const std::size_t light_count = m_asset->lights.size();
        log_gltf->trace("parsing {} lights", light_count);
        m_data_out.lights.resize(light_count);
        for (std::size_t i = 0; i < light_count; ++i) {
            parse_light(i);
        }

        const std::size_t mesh_count = m_asset->meshes.size();
        log_gltf->trace("parsing {} meshes", mesh_count);
        m_data_out.meshes.resize(mesh_count);
        for (std::size_t i = 0; i < mesh_count; ++i) {
            pre_parse_mesh(i);
        }

        ::tf::Taskflow taskflow;
        for (std::size_t i = 0; i < mesh_count; ++i) {
            const fastgltf::Mesh& mesh = m_asset->meshes[i];
            erhe::scene::Mesh& erhe_mesh = *m_data_out.meshes[i].get();
            taskflow.emplace(
                [this, &mesh, &erhe_mesh]()
                {
                    parse_mesh(mesh, erhe_mesh);
                }
            );
        }
        auto future = m_arguments.executor.run(taskflow);
        future.wait();

        log_gltf->trace("parsing nodes");
        m_data_out.nodes.resize(m_asset->nodes.size());
        m_data_out.node_external_assets.resize(m_asset->nodes.size());
        m_data_out.skins.resize(m_asset->skins.size()); // Skins are parsed just in time during node parsing
        if (!m_asset->scenes.empty()) {
            // TODO For now, only one scene per glTF is supported.
            const fastgltf::Scene& scene = m_asset->scenes.front();
            for (std::size_t i = 0, end = scene.nodeIndices.size(); i < end; ++i) {
                parse_node(scene.nodeIndices[i], m_arguments.root_node);
            }
            for (std::size_t node_index : m_nodes_with_skin) {
                parse_node_skin(node_index);
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

        log_gltf->trace("parsing physics");
        parse_physics();
    }

private:
    // glTF 2.1: top-level "files" and "externalAssets" arrays. File entries
    // with a URI are resolved against the glTF directory; entries whose bytes
    // travel inside the asset (data URIs, GLB-packed payloads) are marked
    // embedded. The referenced assets themselves are not parsed here -- that
    // (plus cross-file cycle detection) is the caller's responsibility.
    void parse_files_and_external_assets()
    {
        const std::filesystem::path base_directory = gltf_base_directory(m_arguments.path);

        m_data_out.files.resize(m_asset->files.size());
        for (std::size_t i = 0, end = m_asset->files.size(); i < end; ++i) {
            const fastgltf::File& file     = m_asset->files[i];
            Gltf_file_reference&  out_file = m_data_out.files[i];
            out_file.name      = safe_resource_name(file.name, "file", i);
            out_file.mime_type = std::string{file.mimeType};
            if (const fastgltf::sources::URI* uri = std::get_if<fastgltf::sources::URI>(&file.data)) {
                const std::filesystem::path uri_path = uri->uri.fspath();
                out_file.resolved_path = uri_path.is_absolute() ? uri_path : (base_directory / uri_path);
            } else if (!std::holds_alternative<std::monostate>(file.data)) {
                out_file.embedded = true;
            }
            log_gltf->trace(
                "File: index = {}, name = {}, mimeType = {}, path = {}, embedded = {}",
                i, out_file.name, out_file.mime_type, out_file.resolved_path.string(), out_file.embedded
            );
        }

        m_data_out.external_assets.resize(m_asset->externalAssets.size());
        for (std::size_t i = 0, end = m_asset->externalAssets.size(); i < end; ++i) {
            const fastgltf::ExternalAsset& external_asset = m_asset->externalAssets[i];
            Gltf_external_asset&           out_external_asset = m_data_out.external_assets[i];
            out_external_asset.name       = safe_resource_name(external_asset.name, "externalAsset", i);
            out_external_asset.file_index = external_asset.fileIndex;
            log_gltf->trace("External asset: index = {}, name = {}, file = {}", i, out_external_asset.name, out_external_asset.file_index);
        }
    }

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
    [[nodiscard]] auto load_image_file(
        const std::filesystem::path& path,
        const bool                   linear,
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

        if (!loader.open(path, image_info, linear)) {
            return {};
        }

        ERHE_VERIFY(!image_name.empty());

        erhe::graphics::Texture_create_info texture_create_info{
            .device      = m_arguments.graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .pixelformat = image_info.format,
            .use_mipmaps = true, //(image_info.level_count > 1),
            .width       = image_info.width,
            .height      = image_info.height,
            .depth       = image_info.depth,
            .level_count = image_info.level_count,
            .row_stride  = image_info.row_stride,
            .debug_label = erhe::utility::Debug_label{image_name} // path.filename().string()
        };
        const int  mipmap_count    = texture_create_info.get_texture_level_count();
        const bool generate_mipmap = mipmap_count != image_info.level_count;
        if (generate_mipmap) {
            texture_create_info.usage_mask |= erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            texture_create_info.level_count = mipmap_count;
        }

        // TODO Handle
        ERHE_VERIFY(image_info.width * erhe::dataformat::get_format_size_bytes(image_info.format) == image_info.row_stride);
        const int byte_count = image_info.row_stride * image_info.height;
        ERHE_VERIFY(byte_count >= 1);

        erhe::graphics::Ring_buffer_range buffer_range = m_arguments.image_transfer.acquire_range(byte_count);
        std::span<std::byte> byte_span = buffer_range.get_span();
        std::span<std::uint8_t> u8_span{
            reinterpret_cast<std::uint8_t*>(byte_span.data()),
            byte_span.size_bytes()
        };
        auto texture = std::make_shared<erhe::graphics::Texture>(m_arguments.graphics_device, texture_create_info);
        texture->set_source_path(path);
        const bool ok = loader.load(u8_span);
        loader.close();
        if (ok) {
            buffer_range.bytes_written(byte_count);
            buffer_range.close();
            m_arguments.image_transfer.upload_to_texture(image_info, buffer_range, *texture.get(), generate_mipmap);
            buffer_range.release();
        } else {
            buffer_range.cancel();
        }

        return texture;
    }
    [[nodiscard]] auto load_image_buffer(
        const std::size_t buffer_view_index,
        const std::size_t image_index,
        const bool        linear,
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

        erhe::graphics::Texture_create_info texture_create_info{
            .device      = m_arguments.graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .debug_label = erhe::utility::Debug_label{name}
        };
        int  mipmap_count    = 0;
        bool generate_mipmap = false;

        std::shared_ptr<erhe::graphics::Texture> texture{};

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
                    if (!loader.open(image_encoded_buffer_view, image_info, linear)) {
                        log_gltf->error("Failed to parse image from buffer view '{}'", name);
                        return;
                    }

                    texture_create_info.pixelformat = image_info.format;
                    texture_create_info.use_mipmaps = true; //(image_info.level_count > 1),
                    texture_create_info.width       = image_info.width;
                    texture_create_info.height      = image_info.height;
                    texture_create_info.depth       = image_info.depth;
                    texture_create_info.level_count = image_info.level_count;
                    texture_create_info.row_stride  = image_info.row_stride;
                    texture_create_info.debug_label = erhe::utility::Debug_label{name};

                    mipmap_count    = texture_create_info.get_texture_level_count();
                    generate_mipmap = mipmap_count != image_info.level_count;
                    if (generate_mipmap) {
                        texture_create_info.level_count = mipmap_count;
                        texture_create_info.usage_mask |= erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
                    }

                    texture = std::make_shared<erhe::graphics::Texture>(m_arguments.graphics_device, texture_create_info);
                    texture->set_source_path(m_arguments.path);

                    // TODO Handle depth > 1 and mipmaps
                    ERHE_VERIFY(image_info.width * erhe::dataformat::get_format_size_bytes(image_info.format) == image_info.row_stride);
                    const int byte_count = image_info.row_stride * image_info.height;
                    ERHE_VERIFY(byte_count >= 1);

                    erhe::graphics::Ring_buffer_range buffer_range = m_arguments.image_transfer.acquire_range(byte_count);
                    std::span<std::byte> byte_span = buffer_range.get_span();
                    std::span<std::uint8_t> u8_span{
                        reinterpret_cast<std::uint8_t*>(byte_span.data()),
                        byte_span.size_bytes()
                    };

                    load_ok = loader.load(u8_span);
                    loader.close();

                    if (load_ok) {
                        buffer_range.bytes_written(byte_count);
                        buffer_range.close();
                        m_arguments.image_transfer.upload_to_texture(image_info, buffer_range, *texture.get(), generate_mipmap);
                        buffer_range.release();
                    } else {
                        buffer_range.cancel();
                    }

                    loader.close();
                }
            },
            buffer.data
        );
        if (!load_ok) {
            log_gltf->warn(
                "Image '{}' load failed: image index = {}, width = {}, height = {}",
                name, image_index, texture_create_info.width, texture_create_info.height
            );
        } else {
            log_gltf->info(
                "Loaded image '{}': image index = {}, width = {}, height = {}",
                name, image_index, texture_create_info.width, texture_create_info.height
            );
        }

        return texture;
    }
    [[nodiscard]] auto get_image(const std::size_t image_index, const bool linear)
    {
        if (!m_data_out.images[image_index]) {
            parse_image(image_index, linear);
        }
        return m_data_out.images[image_index];
    }
    void parse_image(const std::size_t image_index, const bool linear)
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
                    erhe_texture = load_image_buffer(buffer_view_source.bufferViewIndex, image_index, linear, image_name);
                },
                [&](const fastgltf::sources::URI& uri){
                    // Resolve on a copy: replace_filename() mutates in place,
                    // and m_arguments.path must keep naming the glTF file
                    // (source paths of nodes parsed later, resource names).
                    std::filesystem::path relative_path = uri.uri.fspath();
                    std::filesystem::path image_path    = m_arguments.path;
                    image_path.replace_filename(relative_path);
                    erhe_texture = load_image_file(image_path, linear, image_name);
                }
            },
            image.data
        );

        if (erhe_texture) {
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
        fastgltf::Filter gl_min_filter = sampler.minFilter.has_value() ? sampler.minFilter.value() : fastgltf::Filter::LinearMipMapLinear;
        fastgltf::Filter gl_mag_filter = sampler.magFilter.has_value() ? sampler.magFilter.value() : fastgltf::Filter::Linear;
        erhe::graphics::Filter min_filter{erhe::graphics::Filter::nearest};
        erhe::graphics::Filter mag_filter{erhe::graphics::Filter::nearest};
        erhe::graphics::Sampler_mipmap_mode mipmap_mode{erhe::graphics::Sampler_mipmap_mode::not_mipmapped};
        switch (gl_min_filter) {
            default:
            case fastgltf::Filter::Nearest:              min_filter = erhe::graphics::Filter::nearest; mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped; break;
            case fastgltf::Filter::Linear:               min_filter = erhe::graphics::Filter::linear;  mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped; break;
            case fastgltf::Filter::NearestMipMapNearest: min_filter = erhe::graphics::Filter::nearest; mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest; break;
            case fastgltf::Filter::LinearMipMapNearest:  min_filter = erhe::graphics::Filter::linear;  mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest; break;
            case fastgltf::Filter::NearestMipMapLinear:  min_filter = erhe::graphics::Filter::nearest; mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear; break;
            case fastgltf::Filter::LinearMipMapLinear:   min_filter = erhe::graphics::Filter::linear;  mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear; break;
        }
        switch (gl_mag_filter) {
            default:
            case fastgltf::Filter::Nearest: mag_filter = erhe::graphics::Filter::nearest; break;
            case fastgltf::Filter::Linear:  mag_filter = erhe::graphics::Filter::linear;  break;
        }
        auto from_gl = [](const fastgltf::Wrap wrap) -> erhe::graphics::Sampler_address_mode {
            switch (wrap) {
                default:
                case fastgltf::Wrap::Repeat:         return erhe::graphics::Sampler_address_mode::repeat;
                case fastgltf::Wrap::ClampToEdge:    return erhe::graphics::Sampler_address_mode::clamp_to_edge;
                case fastgltf::Wrap::MirroredRepeat: return erhe::graphics::Sampler_address_mode::mirrored_repeat;
            }
        };

        create_info.min_filter      = min_filter;
        create_info.mag_filter      = mag_filter;
        create_info.mipmap_mode     = mipmap_mode;
        create_info.address_mode[0] = from_gl(sampler.wrapS);
        create_info.address_mode[1] = from_gl(sampler.wrapT);
        create_info.address_mode[2] = from_gl(sampler.wrapT);
        create_info.max_anisotropy  = m_arguments.graphics_device.get_info().max_texture_max_anisotropy; // TODO
        create_info.debug_label     = erhe::utility::Debug_label{sampler_name};

        auto erhe_sampler = std::make_shared<erhe::graphics::Sampler>(m_arguments.graphics_device, create_info);
        // TODO erhe_sampler->set_source_path(m_path);
        m_data_out.samplers[sampler_index] = erhe_sampler;
        m_data_out.samplers.push_back(erhe_sampler);
    }
    [[nodiscard]] auto is_tangent_frame_needed(const std::size_t material_index) -> bool {
        const fastgltf::Material& material = m_asset->materials[material_index];
        if (material.normalTexture.has_value()) {
            return true;
        }
        if (material.anisotropy) {
            return true;
        }
        return false;
    }
    void parse_material(const std::size_t material_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Material& material = m_asset->materials[material_index];
        const std::string material_name = safe_resource_name(material.name, "material", material_index);
        log_gltf->trace("Primitive material: id = {}, name = {}", material_index, material_name);

        erhe::primitive::Material_create_info create_info{
            .name = material_name
        };

        auto apply_texture = [this](
            const fastgltf::TextureInfo&               gltf_texture_info,
            erhe::primitive::Material_texture_sampler& erhe_texture_sampler,
            const bool                                 linear
        )
        {
            const fastgltf::Texture& texture = m_asset->textures[gltf_texture_info.textureIndex];
            if (texture.imageIndex.has_value()) {
                erhe_texture_sampler.texture_reference = get_image(texture.imageIndex.value(), linear);
            }
            if (texture.samplerIndex.has_value()) {
                erhe_texture_sampler.sampler = m_data_out.samplers[texture.samplerIndex.value()];
            }
            erhe_texture_sampler.tex_coord = static_cast<uint8_t>(gltf_texture_info.texCoordIndex);
            if (gltf_texture_info.transform) {
                erhe_texture_sampler.rotation = gltf_texture_info.transform->rotation;
                erhe_texture_sampler.offset   = glm::vec2{gltf_texture_info.transform->uvOffset[0], gltf_texture_info.transform->uvOffset[1]};
                erhe_texture_sampler.scale    = glm::vec2{gltf_texture_info.transform->uvScale [0], gltf_texture_info.transform->uvScale [1]};
                if (gltf_texture_info.transform->texCoordIndex.has_value()) {
                    erhe_texture_sampler.tex_coord = static_cast<uint8_t>(gltf_texture_info.transform->texCoordIndex.value());
                }
            }
            // TODO texture transform
        };

        erhe::primitive::Material_data&             create_data     = create_info.data;
        erhe::primitive::Material_texture_samplers& create_samplers = create_data.texture_samplers;

        // KHR_materials_unlit: import the unlit flag into Material_data::bxdf_model
        // so the editor renders the material exactly as the source glTF intended.
        // The specular-glossiness fallback below also flips bxdf_model to unlit;
        // honoring KHR_materials_unlit first means we do not need that path's
        // implicit fall-through to set it.
        if (material.unlit) {
            create_data.bxdf_model = erhe::primitive::Bxdf_model::unlit;
        }

        // Standard glTF alphaMode -> Material_blending_mode. MASK also
        // pulls alphaCutoff. Erhe-specific modes (multiply / add /
        // subtract / screen_door) ride on extras (see post-parse loop)
        // and override this default.
        switch (material.alphaMode) {
            case fastgltf::AlphaMode::Opaque:
                create_data.blending_mode = erhe::primitive::Material_blending_mode::opaque;
                break;
            case fastgltf::AlphaMode::Mask:
                create_data.blending_mode = erhe::primitive::Material_blending_mode::alpha_test;
                create_data.alpha_cutoff  = material.alphaCutoff;
                break;
            case fastgltf::AlphaMode::Blend:
                create_data.blending_mode = erhe::primitive::Material_blending_mode::alpha_blend;
                break;
        }

        if (material.normalTexture.has_value()) {
            apply_texture(material.normalTexture.value(), create_data.texture_samplers.normal, true);
            create_data.normal_texture_scale = material.normalTexture.value().scale;
        }
        if (material.occlusionTexture.has_value()) {
            apply_texture(material.occlusionTexture.value(), create_samplers.occlusion, true);
            create_data.occlusion_texture_strength = material.occlusionTexture.value().strength;
        }
        create_data.emissive = material.emissiveStrength * glm::vec3{material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]};
        if (material.emissiveTexture.has_value()) {
            apply_texture(material.emissiveTexture.value(), create_samplers.emissive, false);
        } {
            const fastgltf::PBRData& pbr_data = material.pbrData;
            if (pbr_data.baseColorTexture.has_value()) {
                apply_texture(pbr_data.baseColorTexture.value(), create_samplers.base_color, false);
            }
            if (pbr_data.metallicRoughnessTexture.has_value()) {
                apply_texture(pbr_data.metallicRoughnessTexture.value(), create_samplers.metallic_roughness, true);
            }

            // NOTE: MaterialSpecularGlossiness is only supported in a hacky way to load Hintze Hall
            const std::unique_ptr<fastgltf::MaterialSpecularGlossiness>& specular_glossiness = material.specularGlossiness;
            if (specular_glossiness && specular_glossiness->diffuseTexture.has_value()) {
                create_data.bxdf_model = erhe::primitive::Bxdf_model::unlit;
                apply_texture(specular_glossiness->diffuseTexture.value(), create_samplers.base_color, false);
            }

            create_data.base_color = glm::vec3{
                pbr_data.baseColorFactor[0],
                pbr_data.baseColorFactor[1],
                pbr_data.baseColorFactor[2],
            };
            create_data.opacity     = pbr_data.baseColorFactor[3];
            create_data.metallic    = pbr_data.metallicFactor;
            create_data.roughness.x = std::max(pbr_data.roughnessFactor, 0.001f);
            create_data.roughness.y = std::max(pbr_data.roughnessFactor, 0.001f);
            log_gltf->trace(
                "Material PBR metallic roughness base color factor = {}, {}, {}, {}",
                pbr_data.baseColorFactor[0],
                pbr_data.baseColorFactor[1],
                pbr_data.baseColorFactor[2],
                pbr_data.baseColorFactor[3]
            );
            log_gltf->trace("Material PBR metallic roughness metallic factor = {}", pbr_data.metallicFactor);
            log_gltf->trace("Material PBR metallic roughness roughness factor = {}", pbr_data.roughnessFactor);
            auto new_material = std::make_shared<erhe::primitive::Material>(create_info);
            new_material->set_source_path(m_arguments.path);
            new_material->enable_flag_bits(erhe::Item_flags::show_in_ui);
            m_data_out.materials[material_index] = new_material;

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
        erhe_light->range            = light.range.has_value() ? light.range.value() : 1000.0f; // TODO KHR_lights_punctual says infinite..
        erhe_light->inner_spot_angle = light.innerConeAngle.has_value() ? light.innerConeAngle.value() : 0.0f;
        erhe_light->outer_spot_angle = light.outerConeAngle.has_value() ? light.outerConeAngle.value() : glm::pi<float>() / 4.0f;
        // TODO Sensible defaults for inner and outer cone angles
        erhe_light->layer_id = 0;
        erhe_light->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
    }

    class Primitive_entry
    {
    public:
        std::size_t                                     index_accessor;
        std::vector<std::size_t>                        attribute_accessors;
        bool                                            tangent_frame_needed;
        std::shared_ptr<erhe::primitive::Triangle_soup> triangle_soup;
        std::shared_ptr<erhe::primitive::Primitive>     primitive;
    };
    std::vector<Primitive_entry>   m_primitive_entries;
    ERHE_PROFILE_MUTEX(std::mutex, m_primitive_entries_mutex);

    void load_new_primitive_geometry(
        const fastgltf::Primitive& primitive,
        Primitive_entry&           primitive_entry,
        const bool                 tangent_frame_needed
    )
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
        bool generate_tangents   = tangent_frame_needed;
        bool inject_vertex_color = true;
        for (std::size_t i = 0, end = primitive.attributes.size(); i < end; ++i) {
            const fastgltf::Attribute&                     gltf_attribute        = primitive.attributes[i];
            const erhe::dataformat::Vertex_attribute_usage attribute_usage_type  = to_erhe(gltf_attribute.name);
            const std::size_t                              attribute_usage_index = get_attribute_index(gltf_attribute.name);
            const fastgltf::Accessor&                      accessor              = m_asset->accessors[gltf_attribute.accessorIndex];
            const erhe::dataformat::Format                 format                = to_erhe_attribute(accessor);
            if (attribute_usage_type == erhe::dataformat::Vertex_attribute_usage::tangent) {
                generate_tangents = false;
            }
            if (attribute_usage_type == erhe::dataformat::Vertex_attribute_usage::color) {
                inject_vertex_color = false;
            }
            vertex_count = std::min(accessor.count, vertex_count);
            triangle_soup.vertex_format.streams.front().emplace_back(
                format,
                attribute_usage_type,
                attribute_usage_index
            );
        }
        if (generate_tangents) {
            triangle_soup.vertex_format.streams.front().emplace_back(
                erhe::dataformat::Format::format_32_vec4_float,
                erhe::dataformat::Vertex_attribute_usage::tangent,
                0
            );
            //triangle_soup.vertex_format.streams.front().emplace_back(
            //    erhe::dataformat::Format::format_32_vec3_float,
            //    erhe::dataformat::Vertex_attribute_usage::bitangent,
            //    0
            //);
        }
        if (inject_vertex_color) {
            triangle_soup.vertex_format.streams.front().emplace_back(
                erhe::dataformat::Format::format_32_vec4_float,
                erhe::dataformat::Vertex_attribute_usage::color,
                0
            );
        }
        triangle_soup.vertex_format.streams.front().finalize_stride();
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
            if (accessor.bufferViewIndex.has_value()) {
                copyFromAccessorWithOutStride(
                    m_asset.get(),
                    accessor,
                    triangle_soup.vertex_data.data() + erhe_attribute.attribute->offset,
                    erhe_attribute.stream->stride
                );
            } else {
                log_gltf->warn("Attribute without accessor buffer view");
            }

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

        if (generate_tangents) {
            using namespace erhe::dataformat;
            const Attribute_stream position_attribute_  = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::position);
            const Attribute_stream normal_attribute_    = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::normal);
            const Attribute_stream texcoord_attribute_  = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::tex_coord);
            const Attribute_stream tangent_attribute_   = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::tangent);
            //const Attribute_stream bitangent_attribute_ = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::bitangent);

            if (
                (position_attribute_ .attribute != nullptr) &&
                (normal_attribute_   .attribute != nullptr) &&
                (texcoord_attribute_ .attribute != nullptr) &&
                (tangent_attribute_  .attribute != nullptr)
                //(bitangent_attribute_.attribute != nullptr)
            ) {
                log_gltf->info("generating tangents using mikktspace");
                // Generate tangents with mikktspace

                uint8_t* vertex_data = triangle_soup.vertex_data.data();
                class Tangent_generation_context
                {
                public:
                    std::span<uint32_t> indices;
                    std::size_t         vertex_count;
                    std::size_t         stride;
                    const std::uint8_t* position_data;
                    const std::uint8_t* normal_data;
                    const std::uint8_t* texcoord_data;
                    std::uint8_t*       tangent_data;
                    std::uint8_t*       bitangent_data;
                    Format              position_format;
                    Format              normal_format;
                    Format              texcoord_format;
                    Format              tangent_format;
                    Format              bitangent_format;
                };
                Tangent_generation_context context{
                    .indices          = std::span<uint32_t>{triangle_soup.index_data.data(), triangle_soup.index_data.size()},
                    .vertex_count     = vertex_count,
                    .stride           = position_attribute_.stream->stride,
                    .position_data    = vertex_data + position_attribute_ .attribute->offset,
                    .normal_data      = vertex_data + normal_attribute_   .attribute->offset,
                    .texcoord_data    = vertex_data + texcoord_attribute_ .attribute->offset,
                    .tangent_data     = vertex_data + tangent_attribute_  .attribute->offset,
                    .bitangent_data   = nullptr,
                    .position_format  = position_attribute_ .attribute->format,
                    .normal_format    = normal_attribute_   .attribute->format,
                    .texcoord_format  = texcoord_attribute_ .attribute->format,
                    .tangent_format   = tangent_attribute_  .attribute->format,
                    .bitangent_format = erhe::dataformat::Format::format_undefined
                };
                SMikkTSpaceInterface mikktspace{
                    .m_getNumFaces = [](const SMikkTSpaceContext* mikk_tspace_context) -> int {
                        Tangent_generation_context* context = static_cast<Tangent_generation_context*>(mikk_tspace_context->m_pUserData);
                        return static_cast<int>(context->indices.size() / 3);
                    },
                    .m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int32_t) -> int {
                        return 3;
                    },
                    .m_getPosition = [](const SMikkTSpaceContext* mikk_tspace_context, float posOut[], int32_t face, int32_t vert) {
                        Tangent_generation_context* context = static_cast<Tangent_generation_context*>(mikk_tspace_context->m_pUserData);
                        float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        const uint32_t v = context->indices[face * 3 + vert];
                        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(context->position_data + context->stride * v);
                        convert(src, context->position_format, &value[0], Format::format_32_vec4_float, 1.0f);
                        posOut[0] = value[0];
                        posOut[1] = value[1];
                        posOut[2] = value[2];
                    },
                    .m_getNormal = [](const SMikkTSpaceContext* mikk_tspace_context, float normOut[], int32_t face, int32_t vert) {
                        Tangent_generation_context* context = static_cast<Tangent_generation_context*>(mikk_tspace_context->m_pUserData);
                        float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        const uint32_t v = context->indices[face * 3 + vert];
                        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(context->normal_data + context->stride * v);
                        convert(src, context->normal_format, &value[0], Format::format_32_vec4_float, 1.0f);
                        normOut[0] = value[0];
                        normOut[1] = value[1];
                        normOut[2] = value[2];
                    },
                    .m_getTexCoord = [](const SMikkTSpaceContext* mikk_tspace_context, float texcOut[], int32_t face, int32_t vert) {
                        Tangent_generation_context* context = static_cast<Tangent_generation_context*>(mikk_tspace_context->m_pUserData);
                        float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        const uint32_t v = context->indices[face * 3 + vert];
                        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(context->texcoord_data + context->stride * v);
                        convert(src, context->texcoord_format, &value[0], Format::format_32_vec4_float, 1.0f);
                        texcOut[0] = value[0];
                        texcOut[1] = value[1];
                    },
                    .m_setTSpaceBasic = nullptr,
                    .m_setTSpace = [](
                        const SMikkTSpaceContext* mikk_tspace_context,
                        const float               tangent[],
                        const float               /*bitangent*/[],
                        const float               /*mag_s*/,
                        const float               /*mag_t*/,
                        const tbool               is_orientation_preserving,
                        const int                 face,
                        const int                 vert
                    ) {
                        Tangent_generation_context* context = static_cast<Tangent_generation_context*>(mikk_tspace_context->m_pUserData);
                        float tangent_src[4] = { tangent[0], tangent[1], tangent[2], is_orientation_preserving ? -1.0f : 1.0f };
                        const uint32_t v = context->indices[face * 3 + vert];
                        std::uint8_t* tangent_dst = reinterpret_cast<std::uint8_t*>(context->tangent_data + context->stride * v);
                        convert(tangent_src, Format::format_32_vec4_float, tangent_dst, context->tangent_format, 1.0f);
                        //float bitangent_src[4] = { -bitangent[0], -bitangent[1], -bitangent[2], 0.0f };
                        //std::uint8_t* bitangent_dst = reinterpret_cast<std::uint8_t*>(context->bitangent_data + context->stride * v);
                        //convert(bitangent_src, Format::format_32_vec3_float, bitangent_dst, context->bitangent_format, 1.0f);
                    }
                };
                const SMikkTSpaceContext mikk_tspace_context {
                    .m_pInterface = &mikktspace,
                    .m_pUserData  = &context
                };
                const tbool res = genTangSpaceDefault(&mikk_tspace_context);
                if (res == 0) {
                    log_gltf->warn("genTangSpaceDefault() failed");
                }
            } else {
                log_gltf->info("glTF does not have tangents, and not enough attributes to generate tangents&");
            }
        }

        if (inject_vertex_color) {
            using namespace erhe::dataformat;
            const Attribute_stream color_attribute_ = triangle_soup.vertex_format.find_attribute(Vertex_attribute_usage::color);
            ERHE_VERIFY(color_attribute_.attribute != nullptr);
            const float       opaque_white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            const std::size_t stride          = color_attribute_.stream->stride;
            const auto        color_format    = color_attribute_.attribute->format;
            uint8_t*          vertex_data     = triangle_soup.vertex_data.data();
            std::uint8_t*     color_data      = vertex_data + color_attribute_.attribute->offset;
            for (std::size_t v = 0; v < vertex_count; ++v) {
                std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(color_data + v * stride);
                convert(opaque_white, Format::format_32_vec4_float, dst, color_format, 1.0f);
            }
        }

        primitive_entry.primitive = std::make_shared<erhe::primitive::Primitive>(primitive_entry.triangle_soup);
    }
    auto get_primitive_geometry(
        const fastgltf::Primitive& primitive, 
        Primitive_entry&           primitive_entry,
        const bool                 tangent_frame_needed
    )
    {
        ERHE_PROFILE_FUNCTION();

        if (!primitive.indicesAccessor.has_value()) {
            return; // TODO likely draco compressed messed
        }
        primitive_entry.index_accessor = primitive.indicesAccessor.value();
        primitive_entry.attribute_accessors.clear();
        primitive_entry.tangent_frame_needed = tangent_frame_needed;
        for (std::size_t i = 0, end = primitive.attributes.size(); i < end; ++i) {
            const fastgltf::Attribute& attribute = primitive.attributes[i];
            primitive_entry.attribute_accessors.push_back(attribute.accessorIndex);
        }

        {
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_primitive_entries_mutex};

            for (const auto& old_entry : m_primitive_entries) {
                if (old_entry.index_accessor != primitive_entry.index_accessor) continue;
                if (old_entry.attribute_accessors != primitive_entry.attribute_accessors) continue;
                if (old_entry.tangent_frame_needed != primitive_entry.tangent_frame_needed) continue;
                // Found existing entry
                primitive_entry = old_entry;
                return;
            }
        }

        load_new_primitive_geometry(primitive, primitive_entry, tangent_frame_needed);

        {
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_primitive_entries_mutex};

            m_primitive_entries.push_back(primitive_entry);
        }
    }

    void parse_primitive(
        erhe::scene::Mesh&    erhe_mesh,
        const fastgltf::Mesh& mesh,
        const std::size_t     primitive_index
    )
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Primitive& primitive = mesh.primitives[primitive_index];
        std::string name = fmt::format("{}[{}]", mesh.name.c_str(), primitive_index);
        const bool tangent_frame_needed = primitive.materialIndex.has_value()
            ? is_tangent_frame_needed(primitive.materialIndex.value())
            : false;
        std::shared_ptr<erhe::primitive::Material> erhe_material = primitive.materialIndex.has_value()
            ? m_data_out.materials.at(primitive.materialIndex.value())
            : std::shared_ptr<erhe::primitive::Material>{};

        Primitive_entry primitive_entry;
        get_primitive_geometry(primitive, primitive_entry, tangent_frame_needed);
        erhe_mesh.add_primitive(primitive_entry.primitive, erhe_material);
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
    }
    void pre_parse_mesh(const std::size_t mesh_index)
    {
        const fastgltf::Mesh& mesh = m_asset->meshes[mesh_index];
        const std::string mesh_name = safe_resource_name(mesh.name, "mesh", mesh_index);
        auto erhe_mesh = std::make_shared<erhe::scene::Mesh>(mesh_name);
        m_data_out.meshes[mesh_index] = erhe_mesh;
    }
    void parse_mesh(const fastgltf::Mesh& mesh, erhe::scene::Mesh& erhe_mesh)
    {
        ERHE_PROFILE_FUNCTION();

        erhe_mesh.set_source_path(m_arguments.path);
        erhe_mesh.layer_id = m_arguments.mesh_layer_id;
        erhe_mesh.enable_flag_bits(
            Item_flags::content     |
            Item_flags::visible     |
            Item_flags::show_in_ui  |
            Item_flags::shadow_cast |
            Item_flags::id
        );
        for (std::size_t i = 0, end = mesh.primitives.size(); i < end; ++i) {
            parse_primitive(erhe_mesh, mesh, i);
        }
    }

    std::vector<std::size_t> m_nodes_with_skin;
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
        if (node.externalAssetIndex.has_value()) {
            // glTF 2.1: this node instantiates an external asset. Only the
            // association is recorded here; instantiating the referenced
            // asset under this (otherwise empty) carrier node is the
            // caller's job (see Gltf_data::node_external_assets).
            m_data_out.node_external_assets[node_index] = node.externalAssetIndex.value();
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
                m_nodes_with_skin.push_back(node_index);
            }
            erhe_node->attach(erhe_mesh);
        }
    }

    void parse_node_skin(const std::size_t node_index)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Node& node = m_asset->nodes[node_index];
        const std::size_t skin_index = node.skinIndex.value();
        if (!m_data_out.skins[skin_index]) {
            parse_skin(skin_index);
        }
        std::shared_ptr<erhe::scene::Node> erhe_node = m_data_out.nodes[node_index];
        std::shared_ptr<erhe::scene::Mesh> erhe_mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(erhe_node.get());
        erhe_mesh->skin = m_data_out.skins[skin_index];
    }

#if FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES
    // Resolves a glTF node index referenced by physics data to the parsed
    // erhe node. Returns nullptr (with a warning) for nodes that were not
    // parsed (e.g. nodes outside the first scene).
    [[nodiscard]] auto resolve_physics_node(const std::size_t node_index, const char* purpose) -> std::shared_ptr<erhe::scene::Node>
    {
        if ((node_index >= m_data_out.nodes.size()) || !m_data_out.nodes[node_index]) {
            log_gltf->warn("glTF physics: {} references node {} which is not in the parsed scene - skipping", purpose, node_index);
            return {};
        }
        return m_data_out.nodes[node_index];
    }

    [[nodiscard]] static auto to_physics_combine_mode(const fastgltf::CombineMode combine_mode) -> Physics_combine_mode
    {
        switch (combine_mode) {
            case fastgltf::CombineMode::Average:  return Physics_combine_mode::e_average;
            case fastgltf::CombineMode::Minimum:  return Physics_combine_mode::e_minimum;
            case fastgltf::CombineMode::Maximum:  return Physics_combine_mode::e_maximum;
            case fastgltf::CombineMode::Multiply: return Physics_combine_mode::e_multiply;
            default:                              return Physics_combine_mode::e_average;
        }
    }

    [[nodiscard]] auto to_physics_geometry(const fastgltf::Geometry& geometry, const char* purpose) -> Physics_node_geometry
    {
        Physics_node_geometry out{};
        if (geometry.shape.has_value()) {
            out.shape_index = geometry.shape.value();
        }
        if (geometry.mesh.has_value()) {
            const std::size_t mesh_index = geometry.mesh.value();
            if ((mesh_index >= m_data_out.meshes.size()) || !m_data_out.meshes[mesh_index]) {
                log_gltf->warn("glTF physics: {} references mesh {} which was not parsed - skipping", purpose, mesh_index);
            } else {
                out.mesh = m_data_out.meshes[mesh_index];
            }
        }
        if (geometry.node.has_value()) {
            out.node = resolve_physics_node(geometry.node.value(), purpose);
        }
        out.convex_hull = geometry.convexHull;
        return out;
    }

    // Maps KHR_implicit_shapes + KHR_physics_rigid_bodies data parsed by
    // fastgltf into the plain-data Gltf_physics_data carried in Gltf_data.
    // The editor performs all further mapping to erhe::physics types.
    void parse_physics()
    {
        ERHE_PROFILE_FUNCTION();

        Gltf_physics_data& physics = m_data_out.physics;

        physics.shapes.reserve(m_asset->shapes.size());
        for (const fastgltf::Shape& shape : m_asset->shapes) {
            Physics_shape out{};
            std::visit(
                fastgltf::visitor{
                    [&out](const fastgltf::SphereShape& sphere) {
                        out.type   = Physics_shape_type::e_sphere;
                        out.radius = static_cast<float>(sphere.radius);
                    },
                    [&out](const fastgltf::BoxShape& box) {
                        out.type = Physics_shape_type::e_box;
                        out.size = glm::vec3{box.size[0], box.size[1], box.size[2]};
                    },
                    [&out](const fastgltf::CapsuleShape& capsule) {
                        out.type          = Physics_shape_type::e_capsule;
                        out.height        = static_cast<float>(capsule.height);
                        out.radius_bottom = static_cast<float>(capsule.radiusBottom);
                        out.radius_top    = static_cast<float>(capsule.radiusTop);
                    },
                    [&out](const fastgltf::CylinderShape& cylinder) {
                        out.type          = Physics_shape_type::e_cylinder;
                        out.height        = static_cast<float>(cylinder.height);
                        out.radius_bottom = static_cast<float>(cylinder.radiusBottom);
                        out.radius_top    = static_cast<float>(cylinder.radiusTop);
                    }
                },
                shape
            );
            physics.shapes.push_back(out);
        }

        physics.materials.reserve(m_asset->physicsMaterials.size());
        for (const fastgltf::PhysicsMaterial& material : m_asset->physicsMaterials) {
            Physics_material_description out{};
            // fastgltf physics materials carry no name; the editor synthesizes one.
            out.static_friction     = static_cast<float>(material.staticFriction);
            out.dynamic_friction    = static_cast<float>(material.dynamicFriction);
            out.restitution         = static_cast<float>(material.restitution);
            out.friction_combine    = to_physics_combine_mode(material.frictionCombine);
            out.restitution_combine = to_physics_combine_mode(material.restitutionCombine);
            physics.materials.push_back(std::move(out));
        }

        physics.collision_filters.reserve(m_asset->collisionFilters.size());
        for (const fastgltf::CollisionFilter& filter : m_asset->collisionFilters) {
            Physics_collision_filter_description out{};
            for (const auto& system : filter.collisionSystems) {
                out.collision_systems.emplace_back(system.data(), system.size());
            }
            for (const auto& system : filter.collideWithSystems) {
                out.collide_with_systems.emplace_back(system.data(), system.size());
            }
            for (const auto& system : filter.notCollideWithSystems) {
                out.not_collide_with_systems.emplace_back(system.data(), system.size());
            }
            physics.collision_filters.push_back(std::move(out));
        }

        physics.joints.reserve(m_asset->physicsJoints.size());
        for (const fastgltf::PhysicsJoint& joint : m_asset->physicsJoints) {
            Physics_joint_description out{};
            out.limits.reserve(joint.limits.size());
            for (const fastgltf::JointLimit& limit : joint.limits) {
                Physics_joint_limit out_limit{};
                for (const uint8_t axis : limit.linearAxes) {
                    out_limit.linear_axes.push_back(static_cast<int>(axis));
                }
                for (const uint8_t axis : limit.angularAxes) {
                    out_limit.angular_axes.push_back(static_cast<int>(axis));
                }
                if (limit.min.has_value()) {
                    out_limit.min = static_cast<float>(limit.min.value());
                }
                if (limit.max.has_value()) {
                    out_limit.max = static_cast<float>(limit.max.value());
                }
                if (limit.stiffness.has_value()) {
                    out_limit.stiffness = static_cast<float>(limit.stiffness.value());
                }
                out_limit.damping = static_cast<float>(limit.damping);
                out.limits.push_back(std::move(out_limit));
            }
            out.drives.reserve(joint.drives.size());
            for (const fastgltf::JointDrive& drive : joint.drives) {
                Physics_joint_drive out_drive{};
                out_drive.type = (drive.type == fastgltf::DriveType::Angular)
                    ? Physics_drive_type::e_angular
                    : Physics_drive_type::e_linear;
                out_drive.mode = (drive.mode == fastgltf::DriveMode::Acceleration)
                    ? Physics_drive_mode::e_acceleration
                    : Physics_drive_mode::e_force;
                out_drive.axis            = static_cast<int>(drive.axis);
                out_drive.max_force       = static_cast<float>(drive.maxForce);
                out_drive.position_target = static_cast<float>(drive.positionTarget);
                out_drive.velocity_target = static_cast<float>(drive.velocityTarget);
                out_drive.stiffness       = static_cast<float>(drive.stiffness);
                out_drive.damping         = static_cast<float>(drive.damping);
                out.drives.push_back(out_drive);
            }
            physics.joints.push_back(std::move(out));
        }

        for (std::size_t node_index = 0, end = m_asset->nodes.size(); node_index < end; ++node_index) {
            const fastgltf::Node& node = m_asset->nodes[node_index];
            if (!node.physicsRigidBody) {
                continue;
            }
            std::shared_ptr<erhe::scene::Node> erhe_node = resolve_physics_node(node_index, "rigid body");
            if (!erhe_node) {
                continue;
            }
            const fastgltf::PhysicsRigidBody& rigid_body = *node.physicsRigidBody.get();
            Physics_node_description description{};
            description.node = erhe_node;

            if (rigid_body.motion.has_value()) {
                const fastgltf::Motion& motion = rigid_body.motion.value();
                Physics_node_motion out_motion{};
                out_motion.is_kinematic = motion.isKinematic;
                if (motion.mass.has_value()) {
                    out_motion.mass = static_cast<float>(motion.mass.value());
                }
                out_motion.center_of_mass = glm::vec3{motion.centerOfMass[0], motion.centerOfMass[1], motion.centerOfMass[2]};
                if (motion.inertialDiagonal.has_value()) {
                    const fastgltf::math::fvec3& d = motion.inertialDiagonal.value();
                    out_motion.inertia_diagonal = glm::vec3{d[0], d[1], d[2]};
                }
                if (motion.inertialOrientation.has_value()) {
                    const fastgltf::math::fvec4& q = motion.inertialOrientation.value();
                    out_motion.inertia_orientation = glm::quat{q[3], q[0], q[1], q[2]}; // glm has [w x y z], glTF has [x y z w]
                }
                out_motion.linear_velocity  = glm::vec3{motion.linearVelocity[0], motion.linearVelocity[1], motion.linearVelocity[2]};
                out_motion.angular_velocity = glm::vec3{motion.angularVelocity[0], motion.angularVelocity[1], motion.angularVelocity[2]};
                out_motion.gravity_factor   = static_cast<float>(motion.gravityFactor);
                description.motion = out_motion;
            }

            if (rigid_body.collider.has_value()) {
                const fastgltf::Collider& collider = rigid_body.collider.value();
                Physics_node_collider out_collider{};
                out_collider.geometry = to_physics_geometry(collider.geometry, "collider geometry");
                if (collider.physicsMaterial.has_value()) {
                    out_collider.material_index = collider.physicsMaterial.value();
                }
                if (collider.collisionFilter.has_value()) {
                    out_collider.filter_index = collider.collisionFilter.value();
                }
                description.collider = std::move(out_collider);
            }

            if (rigid_body.trigger.has_value()) {
                Physics_node_trigger out_trigger{};
                std::visit(
                    fastgltf::visitor{
                        [this, &out_trigger](const fastgltf::GeometryTrigger& geometry_trigger) {
                            out_trigger.geometry = to_physics_geometry(geometry_trigger.geometry, "trigger geometry");
                            if (geometry_trigger.collisionFilter.has_value()) {
                                out_trigger.filter_index = geometry_trigger.collisionFilter.value();
                            }
                        },
                        [this, &out_trigger](const fastgltf::NodeTrigger& node_trigger) {
                            for (const std::size_t trigger_node_index : node_trigger.nodes) {
                                std::shared_ptr<erhe::scene::Node> trigger_node = resolve_physics_node(trigger_node_index, "compound trigger");
                                if (trigger_node) {
                                    out_trigger.compound_nodes.push_back(std::move(trigger_node));
                                }
                            }
                        }
                    },
                    rigid_body.trigger.value()
                );
                description.trigger = std::move(out_trigger);
            }

            if (rigid_body.joint.has_value()) {
                const fastgltf::Joint& joint = rigid_body.joint.value();
                Physics_node_joint out_joint{};
                out_joint.connected_node   = resolve_physics_node(joint.connectedNode, "joint connected node");
                out_joint.joint_index      = joint.joint;
                out_joint.enable_collision = joint.enableCollision;
                description.joint = std::move(out_joint);
            }

            physics.node_physics.push_back(std::move(description));
        }

        if (!physics.node_physics.empty() || !physics.shapes.empty() || !physics.materials.empty() || !physics.collision_filters.empty() || !physics.joints.empty()) {
            log_gltf->info(
                "glTF physics: {} shapes, {} materials, {} collision filters, {} joints, {} rigid body nodes",
                physics.shapes.size(),
                physics.materials.size(),
                physics.collision_filters.size(),
                physics.joints.size(),
                physics.node_physics.size()
            );
        }
    }
#else
    void parse_physics()
    {
    }
#endif
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

    log_gltf->info("glTF loading {}", arguments.path.string());

    erhe::time::Timer timer{"parse_gltf"};
    timer.begin();

    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(arguments.path);
    if (data.error() != fastgltf::Error::None) {
        log_gltf->error("glTF load error: {}", fastgltf::getErrorMessage(data.error()));
        return {};
    }

    constexpr auto extensions =
        fastgltf::Extensions::EXT_mesh_gpu_instancing             |
        fastgltf::Extensions::KHR_mesh_quantization               |
        fastgltf::Extensions::EXT_meshopt_compression             |
        fastgltf::Extensions::KHR_lights_punctual                 |
        fastgltf::Extensions::EXT_texture_webp                    |
        fastgltf::Extensions::KHR_texture_transform               |
        fastgltf::Extensions::KHR_texture_basisu                  |
        fastgltf::Extensions::MSFT_texture_dds                    |
        fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness |
        fastgltf::Extensions::KHR_materials_specular              |
        fastgltf::Extensions::KHR_materials_ior                   |
        fastgltf::Extensions::KHR_materials_iridescence           |
        fastgltf::Extensions::KHR_materials_volume                |
        fastgltf::Extensions::KHR_materials_transmission          |
        fastgltf::Extensions::KHR_materials_clearcoat             |
        fastgltf::Extensions::KHR_materials_emissive_strength     |
        fastgltf::Extensions::KHR_materials_sheen                 |
        //fastgltf::Extensions::KHR_draco_mesh_compression          |
        fastgltf::Extensions::KHR_implicit_shapes                 |
        fastgltf::Extensions::KHR_physics_rigid_bodies            |
        fastgltf::Extensions::KHR_materials_unlit;
    // Collect erhe-specific extras during parsing. The parser callback
    // runs before parse_material() / parse_node(), so we accumulate into
    // per-index maps and replay each field after the standard glTF parse
    // completes.
    class Parse_extras_context
    {
    public:
        std::unordered_map<std::size_t, Material_extras> material_extras;
        std::unordered_map<std::size_t, uint64_t>        node_flags; // serialized erhe Item flags (c_serialized_item_flags)
    };
    Parse_extras_context parse_extras_context;

    fastgltf::Parser fastgltf_parser{extensions};
    fastgltf_parser.setUserPointer(&parse_extras_context);
    fastgltf_parser.setExtrasParseCallback(
        [](simdjson::dom::object* extras, std::size_t object_index, fastgltf::Category object_type, void* user_pointer) {
            if (extras == nullptr) {
                return;
            }
            Parse_extras_context* context = static_cast<Parse_extras_context*>(user_pointer);

            if (object_type == fastgltf::Category::Nodes) {
                simdjson::dom::element flags_element;
                if (extras->at_key("erhe_flags").get(flags_element) == simdjson::error_code::SUCCESS) {
                    simdjson::dom::array flags_array;
                    if (flags_element.get_array().get(flags_array) == simdjson::error_code::SUCCESS) {
                        uint64_t flag_bits = 0;
                        for (simdjson::dom::element flag_element : flags_array) {
                            std::string_view flag_name;
                            if (flag_element.get_string().get(flag_name) != simdjson::error_code::SUCCESS) {
                                continue;
                            }
                            for (const Serialized_item_flag& serialized_flag : c_serialized_item_flags) {
                                if (flag_name == serialized_flag.name) {
                                    flag_bits = flag_bits | serialized_flag.bit;
                                }
                            }
                        }
                        if (flag_bits != 0) {
                            context->node_flags[object_index] = flag_bits;
                        }
                    }
                }
                return;
            }

            if (object_type != fastgltf::Category::Materials) {
                return;
            }
            Material_extras& entry = context->material_extras[object_index];

            simdjson::dom::element roughness_y_element;
            if (extras->at_key("roughness_y").get(roughness_y_element) == simdjson::error_code::SUCCESS) {
                double value = 0.0;
                if (roughness_y_element.get_double().get(value) == simdjson::error_code::SUCCESS) {
                    entry.roughness_y = static_cast<float>(value);
                }
            }

            simdjson::dom::element bxdf_model_element;
            if (extras->at_key("bxdf_model").get(bxdf_model_element) == simdjson::error_code::SUCCESS) {
                std::string_view value;
                if (bxdf_model_element.get_string().get(value) == simdjson::error_code::SUCCESS) {
                    if (auto parsed = bxdf_model_from_string(value); parsed.has_value()) {
                        entry.bxdf_model = parsed;
                    }
                }
            }

            // erhe-specific blending modes (multiply/add/subtract/
            // screen_door) ride here; opaque/blend/mask are restored
            // from the standard alphaMode in parse_material().
            simdjson::dom::element blending_mode_element;
            if (extras->at_key("blending_mode").get(blending_mode_element) == simdjson::error_code::SUCCESS) {
                std::string_view value;
                if (blending_mode_element.get_string().get(value) == simdjson::error_code::SUCCESS) {
                    if (auto parsed = blending_mode_from_string(value); parsed.has_value()) {
                        entry.blending_mode = parsed;
                    }
                }
            }

            simdjson::dom::element circular_element;
            if (extras->at_key("use_circular_brushed_metal").get(circular_element) == simdjson::error_code::SUCCESS) {
                bool value = false;
                if (circular_element.get_bool().get(value) == simdjson::error_code::SUCCESS) {
                    entry.use_circular_brushed_metal = value;
                }
            }

            simdjson::dom::element circular_tex_coord_element;
            if (extras->at_key("circular_brushed_metal_tex_coord").get(circular_tex_coord_element) == simdjson::error_code::SUCCESS) {
                uint64_t value = 0;
                if (circular_tex_coord_element.get_uint64().get(value) == simdjson::error_code::SUCCESS) {
                    entry.circular_brushed_metal_tex_coord = static_cast<uint32_t>(value);
                }
            }

            simdjson::dom::element aniso_element;
            if (extras->at_key("use_aniso_control").get(aniso_element) == simdjson::error_code::SUCCESS) {
                bool value = false;
                if (aniso_element.get_bool().get(value) == simdjson::error_code::SUCCESS) {
                    entry.use_aniso_control = value;
                }
            }
        }
    );

    fastgltf::Expected<fastgltf::Asset> asset = fastgltf_parser.loadGltf(
        data.get(),
        gltf_base_directory(arguments.path),
        fastgltf::Options::LoadExternalBuffers // TODO Consider | fastgltf::Options::DecomposeNodeMatrices
    );
    if (auto error = asset.error(); error != fastgltf::Error::None) {
        log_gltf->error("glTF parse error: {}", fastgltf::getErrorMessage(error));
        return {};
    }


    Gltf_data result;
    Gltf_parser erhe_parser{std::move(asset), result, arguments};
    erhe_parser.parse_and_build();

    // Apply serialized erhe Item flags to parsed nodes (Gltf_data::nodes is
    // parallel to the glTF nodes array).
    for (const auto& [node_index, flag_bits] : parse_extras_context.node_flags) {
        if (node_index >= result.nodes.size()) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node>& node = result.nodes[node_index];
        if (node) {
            node->enable_flag_bits(flag_bits);
        }
    }

    // Apply extras data to parsed materials. bxdf_model in extras
    // overrides the KHR_materials_unlit-driven assignment from
    // parse_material when both are present (e.g. an anisotropic_brdf
    // material that also wrote bxdf_model into extras for clarity).
    for (const auto& [material_index, extras] : parse_extras_context.material_extras) {
        if (material_index >= result.materials.size()) {
            continue;
        }
        const std::shared_ptr<erhe::primitive::Material>& material = result.materials[material_index];
        if (!material) {
            continue;
        }
        if (extras.roughness_y.has_value()) {
            material->data.roughness.y = extras.roughness_y.value();
        }
        if (extras.bxdf_model.has_value()) {
            material->data.bxdf_model = extras.bxdf_model.value();
        }
        if (extras.blending_mode.has_value()) {
            material->data.blending_mode = extras.blending_mode.value();
        }
        if (extras.use_circular_brushed_metal.has_value()) {
            material->data.use_circular_brushed_metal = extras.use_circular_brushed_metal.value();
        }
        if (extras.circular_brushed_metal_tex_coord.has_value()) {
            material->data.circular_brushed_metal_tex_coord = extras.circular_brushed_metal_tex_coord.value();
        }
        if (extras.use_aniso_control.has_value()) {
            material->data.use_aniso_control = extras.use_aniso_control.value();
        }
    }

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

    Gltf_scan result;

    fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None) {
        const std::string error_message = fmt::format("glTF load error: {}", fastgltf::getErrorMessage(data.error()));
        log_gltf->error(error_message);
        result.errors.push_back(error_message);
        return result;
    }

    constexpr auto extensions =
        fastgltf::Extensions::EXT_mesh_gpu_instancing             |
        fastgltf::Extensions::KHR_mesh_quantization               |
        fastgltf::Extensions::EXT_meshopt_compression             |
        fastgltf::Extensions::KHR_lights_punctual                 |
        fastgltf::Extensions::EXT_texture_webp                    |
        fastgltf::Extensions::KHR_texture_transform               |
        fastgltf::Extensions::KHR_texture_basisu                  |
        fastgltf::Extensions::MSFT_texture_dds                    |
        fastgltf::Extensions::KHR_materials_pbrSpecularGlossiness |
        fastgltf::Extensions::KHR_materials_specular              |
        fastgltf::Extensions::KHR_materials_ior                   |
        fastgltf::Extensions::KHR_materials_iridescence           |
        fastgltf::Extensions::KHR_materials_volume                |
        fastgltf::Extensions::KHR_materials_transmission          |
        fastgltf::Extensions::KHR_materials_clearcoat             |
        fastgltf::Extensions::KHR_materials_emissive_strength     |
        fastgltf::Extensions::KHR_materials_sheen                 |
        //fastgltf::Extensions::KHR_draco_mesh_compression          | TODO
        fastgltf::Extensions::KHR_implicit_shapes                 |
        fastgltf::Extensions::KHR_physics_rigid_bodies            |
        fastgltf::Extensions::KHR_materials_unlit;
    fastgltf::Parser fastgltf_parser{extensions};
    fastgltf::Expected<fastgltf::Asset> asset_expected = fastgltf_parser.loadGltf(
        data.get(),
        gltf_base_directory(path),
        fastgltf::Options::None
    );
    if (auto error = asset_expected.error(); error != fastgltf::Error::None) {
        const std::string error_message = fmt::format("glTF parse error: {}", fastgltf::getErrorMessage(error));
        log_gltf->error(error_message);
        result.errors.push_back(error_message);
        return result;
    }

    fastgltf::Asset* asset = asset_expected.get_if();
    if (asset == nullptr) {
        const std::string error_message = "asset nullptr";
        log_gltf->error(error_message);
        result.errors.push_back(error_message);
        return result;
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

    for (const auto& extension : asset->extensionsUsed) {
        result.extensions_used.push_back(std::string{extension.data(), extension.size()});
    }
    for (const auto& extension : asset->extensionsRequired) {
        result.extensions_required.push_back(std::string{extension.data(), extension.size()});
    }

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

    for (std::size_t i = 0, end = asset->lights.size(); i < end; ++i) {
        switch (asset->lights[i].type) {
            case fastgltf::LightType::Directional: {
                result.directional_lights.push_back(resource_name(asset->lights[i].name, "directional_light", result.directional_lights.size()));
                break;
            }
            case fastgltf::LightType::Point: {
                result.point_lights.push_back(resource_name(asset->lights[i].name, "point_light", result.point_lights.size()));
                break;
            }
            case fastgltf::LightType::Spot: {
                result.spot_lights.push_back(resource_name(asset->lights[i].name, "spot_light", result.spot_lights.size()));
                break;
            }
        }
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

    result.files.resize(asset->files.size());
    for (std::size_t i = 0, end = asset->files.size(); i < end; ++i) {
        result.files[i] = resource_name(asset->files[i].name, "file", i);
    }

    result.external_assets.resize(asset->externalAssets.size());
    for (std::size_t i = 0, end = asset->externalAssets.size(); i < end; ++i) {
        result.external_assets[i] = resource_name(asset->externalAssets[i].name, "externalAsset", i);
    }

    // Combined scene-space AABB from POSITION accessor min/max (required by
    // the glTF spec) transformed through the node hierarchy. Reads only the
    // JSON - buffer data is never touched, so this stays cheap for large
    // files. See the Gltf_scan::bounding_box declaration for limitations.
    {
        const auto bounds_component = [](const fastgltf::AccessorBoundsArray& bounds, const std::size_t i) -> float {
            return (bounds.type() == fastgltf::AccessorBoundsArray::BoundsType::float64)
                ? static_cast<float>(bounds.data<double>()[i])
                : static_cast<float>(bounds.data<std::int64_t>()[i]);
        };
        erhe::math::Aabb aabb{};
        const auto include_scene = [&](const std::size_t scene_index) {
            fastgltf::iterateSceneNodes(
                *asset,
                scene_index,
                fastgltf::math::fmat4x4{},
                [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix) {
                    if (!node.meshIndex.has_value() || (node.meshIndex.value() >= asset->meshes.size())) {
                        return;
                    }
                    const fastgltf::Mesh& mesh = asset->meshes[node.meshIndex.value()];
                    for (const fastgltf::Primitive& primitive : mesh.primitives) {
                        const auto* position_attribute = primitive.findAttribute("POSITION");
                        if (position_attribute == primitive.attributes.cend()) {
                            continue;
                        }
                        const fastgltf::Accessor& accessor = asset->accessors[position_attribute->accessorIndex];
                        if (!accessor.min || !accessor.max || (accessor.min->size() < 3) || (accessor.max->size() < 3)) {
                            continue;
                        }
                        const erhe::math::Aabb local_aabb{
                            .min = glm::vec3{bounds_component(*accessor.min, 0), bounds_component(*accessor.min, 1), bounds_component(*accessor.min, 2)},
                            .max = glm::vec3{bounds_component(*accessor.max, 0), bounds_component(*accessor.max, 1), bounds_component(*accessor.max, 2)}
                        };
                        glm::mat4 world_from_node{1.0f};
                        static_assert(sizeof(world_from_node) == sizeof(matrix));
                        std::memcpy(&world_from_node, &matrix, sizeof(world_from_node)); // both column-major float 4x4
                        aabb.include(local_aabb.transformed_by(world_from_node));
                    }
                }
            );
        };
        if (!asset->scenes.empty()) {
            if (asset->defaultScene.has_value() && (asset->defaultScene.value() < asset->scenes.size())) {
                include_scene(asset->defaultScene.value());
            } else {
                for (std::size_t i = 0, end = asset->scenes.size(); i < end; ++i) {
                    include_scene(i);
                }
            }
        }
        if (aabb.is_valid()) {
            result.bounding_box = aabb;
        }
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
    explicit Gltf_exporter(const Gltf_export_arguments& arguments)
        : m_arguments{arguments}
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
        {
            fastgltf::Buffer gltf_index_buffer{
                .byteLength = index_data_size,
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{index_data_source.begin(), index_data_source.end()},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(gltf_index_buffer));
        }

        entry.index_buffer_view = m_gltf_asset.bufferViews.size();
        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = entry.index_buffer;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = index_data_size;
            buffer_view.target      = fastgltf::BufferTarget::ElementArrayBuffer;
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }

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
        add_index_data_source(entry, vertex_count, index_count, std::span<const std::byte>(index_data, index_data_size));
    }
    void add_vertex_data_source(
        Export_entry&                          entry,
        std::size_t                            vertex_count,
        const erhe::dataformat::Vertex_format& vertex_format,
        std::span<const std::byte>             vertex_data_source
    )
    {
        entry.vertex_buffer = m_gltf_asset.buffers.size();
        {
            fastgltf::Buffer gltf_vertex_buffer{
                .byteLength = vertex_data_source.size_bytes(),
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{vertex_data_source.begin(), vertex_data_source.end()},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(gltf_vertex_buffer));
        }

        entry.vertex_buffer_view = m_gltf_asset.bufferViews.size();

        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = entry.vertex_buffer;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = vertex_data_source.size_bytes();
            buffer_view.byteStride  = vertex_format.streams.front().stride,
            buffer_view.target      = fastgltf::BufferTarget::ArrayBuffer;
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }

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

        erhe::buffer::Cpu_buffer vertex_buffer{"", vertex_count * vertex_stride};
        erhe::buffer::Cpu_buffer index_buffer {"", index_count * index_stride};

        // Declared after the buffers: buffer_mesh holds Buffer_allocation RAII
        // handles that free into the buffers' allocators on destruction, so it
        // must be destroyed before vertex_buffer / index_buffer.
        erhe::primitive::Buffer_mesh buffer_mesh;

        erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink{{&vertex_buffer}};
        erhe::primitive::Cpu_index_buffer_sink index_buffer_sink{index_buffer};
        const erhe::primitive::Build_info build_info{
            .primitive_types = {
                .fill_triangles = true,
            },
            .buffer_info = {
                .normal_style       = erhe::primitive::Normal_style::corner_normals,
                .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
                .vertex_format      = vertex_format,
                .vertex_buffer_sink = vertex_buffer_sink,
                .index_buffer_sink  = index_buffer_sink
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
        add_index_data_source(entry, vertex_count, index_count, index_buffer.get_span());
        add_vertex_data_source(entry, vertex_count, vertex_format, vertex_buffer.get_span());

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

        const erhe::primitive::Material_data& data = material->data;
        const size_t gltf_material_index = m_gltf_asset.materials.size();
        {
            fastgltf::Material gltf_material{
                .pbrData = {
                    .baseColorFactor = {
                        data.base_color.r,
                        data.base_color.g,
                        data.base_color.b,
                        data.opacity
                    },
                    .metallicFactor           = data.metallic,
                    .roughnessFactor          = data.roughness.x,
                    .baseColorTexture         = {},
                    .metallicRoughnessTexture = {}
                },
                .normalTexture    = {},
                .occlusionTexture = {},
                .emissiveTexture  = {},
                .emissiveFactor = {
                    data.emissive.r,
                    data.emissive.g,
                    data.emissive.b
                },
                .anisotropy                               = {},
                .clearcoat                                = {},
                .diffuseTransmission                      = {},
                .iridescence                              = {},
                .sheen                                    = {},
                .specular                                 = {},
                .specularGlossiness                       = {},
                .transmission                             = {},
                .volume                                   = {},
                .packedNormalMetallicRoughnessTexture     = {},
                .packedOcclusionRoughnessMetallicTextures = {},
                .name                                     = FASTGLTF_STD_PMR_NS::string{material->get_name()}
            };
            // Preserve the unlit BxDF via the standard KHR_materials_unlit
            // extension; the import path keys off material.unlit in the
            // same way. The other bxdf_model values + the use_* flags are
            // erhe-specific and round-trip via extras (see
            // setExtrasWriteCallback below).
            gltf_material.unlit = (data.bxdf_model == erhe::primitive::Bxdf_model::unlit);

            // Material_blending_mode -> alphaMode (+ alphaCutoff for
            // MASK). erhe-specific modes (multiply / add / subtract /
            // screen_door) fall back to BLEND so a consumer that does
            // not understand the extras still sees a sensible result;
            // the exact value is restored on re-import via extras.
            switch (data.blending_mode) {
                case erhe::primitive::Material_blending_mode::opaque:
                    gltf_material.alphaMode = fastgltf::AlphaMode::Opaque;
                    break;
                case erhe::primitive::Material_blending_mode::alpha_blend:
                    gltf_material.alphaMode = fastgltf::AlphaMode::Blend;
                    break;
                case erhe::primitive::Material_blending_mode::alpha_test:
                    gltf_material.alphaMode   = fastgltf::AlphaMode::Mask;
                    gltf_material.alphaCutoff = data.alpha_cutoff;
                    break;
                case erhe::primitive::Material_blending_mode::multiply:
                case erhe::primitive::Material_blending_mode::add:
                case erhe::primitive::Material_blending_mode::subtract:
                case erhe::primitive::Material_blending_mode::screen_door:
                    gltf_material.alphaMode = fastgltf::AlphaMode::Blend;
                    break;
            }
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
        for (const erhe::scene::Mesh_primitive& erhe_mesh_primitive : erhe_mesh->get_primitives()) {
            const erhe::primitive::Primitive& erhe_primitive = *erhe_mesh_primitive.primitive.get();
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
            if (erhe_mesh_primitive.material) {
                gltf_primitive.materialIndex = process_material(erhe_mesh_primitive.material.get());
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

    // Emits the deduplicated glTF 2.1 "files" + "externalAssets" entries for
    // an external-asset reference and returns the externalAssets index.
    auto get_external_asset_index(const Gltf_export_external_asset& external_asset) -> std::size_t
    {
        const auto existing = m_uri_to_external_asset_index.find(external_asset.uri);
        if (existing != m_uri_to_external_asset_index.end()) {
            return existing->second;
        }

        fastgltf::File file{};
        file.data = fastgltf::sources::URI{
            .fileByteOffset = 0,
            .uri            = fastgltf::URI{external_asset.uri},
            .mimeType       = fastgltf::MimeType::None
        };
        file.mimeType = external_asset.mime_type;
        const std::size_t file_index = m_gltf_asset.files.size();
        m_gltf_asset.files.emplace_back(std::move(file));

        fastgltf::ExternalAsset gltf_external_asset{};
        gltf_external_asset.fileIndex = file_index;
        gltf_external_asset.name      = external_asset.name;
        const std::size_t external_asset_index = m_gltf_asset.externalAssets.size();
        m_gltf_asset.externalAssets.emplace_back(std::move(gltf_external_asset));

        m_uri_to_external_asset_index.emplace(external_asset.uri, external_asset_index);
        return external_asset_index;
    }

    // Serialized erhe Item flags (c_serialized_item_flags) ride in node
    // "extras" (written by the extras callback in export_gltf()); record
    // them per emitted glTF node index.
    void record_serialized_node_flags(const erhe::scene::Node& erhe_node, const std::size_t gltf_node_index)
    {
        const uint64_t flag_bits = erhe_node.get_flag_bits() & serialized_item_flags_mask();
        if (flag_bits != 0) {
            m_gltf_node_index_to_flags.emplace(gltf_node_index, flag_bits);
        }
    }

    auto process_light(const erhe::scene::Light* erhe_light) -> std::size_t
    {
        const auto it = m_exported_lights.find(erhe_light);
        if (it != m_exported_lights.end()) {
            return it->second;
        }

        fastgltf::Light gltf_light{};
        // Qualified: Gltf_exporter's from_erhe(Trs_transform) member hides
        // the namespace-scope overload set.
        gltf_light.type      = erhe::gltf::from_erhe(erhe_light->type);
        gltf_light.color     = fastgltf::math::nvec3{erhe_light->color.x, erhe_light->color.y, erhe_light->color.z};
        gltf_light.intensity = erhe_light->intensity;
        // KHR_lights_punctual: range is not allowed on directional lights;
        // erhe uses range 0 to mean infinite.
        if ((erhe_light->type != erhe::scene::Light_type::directional) && (erhe_light->range > 0.0f)) {
            gltf_light.range = erhe_light->range;
        }
        if (erhe_light->type == erhe::scene::Light_type::spot) {
            gltf_light.innerConeAngle = erhe_light->inner_spot_angle;
            gltf_light.outerConeAngle = erhe_light->outer_spot_angle;
        }
        gltf_light.name = erhe_light->get_name();

        const std::size_t gltf_light_index = m_gltf_asset.lights.size();
        m_gltf_asset.lights.emplace_back(std::move(gltf_light));
        m_exported_lights.emplace(erhe_light, gltf_light_index);
        return gltf_light_index;
    }

    auto process_node(const erhe::scene::Node& erhe_node) -> std::size_t
    {
        fastgltf::Node gltf_node{};

        gltf_node.name = erhe_node.get_name();
        gltf_node.transform = from_erhe(erhe_node.parent_from_node_transform());

        // glTF 2.1: a prefab-instance node is written as an externalAsset
        // reference. Children and attachments are not exported - the
        // instantiated content comes from the referenced file.
        const auto external_asset_it = m_arguments.external_assets.find(&erhe_node);
        if (external_asset_it != m_arguments.external_assets.end()) {
            gltf_node.externalAssetIndex = get_external_asset_index(external_asset_it->second);
            size_t gltf_external_node_index = m_gltf_asset.nodes.size();
            m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
            m_erhe_node_to_gltf_node_index.insert({&erhe_node, gltf_external_node_index});
            record_serialized_node_flags(erhe_node, gltf_external_node_index);
            return gltf_external_node_index;
        }

        const std::shared_ptr<erhe::scene::Mesh> erhe_mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(&erhe_node);
        if (erhe_mesh) {
            gltf_node.meshIndex = process_mesh(erhe_mesh.get());
        }

        const std::shared_ptr<erhe::scene::Camera> erhe_camera = erhe::scene::get_attachment<erhe::scene::Camera>(&erhe_node);
        if (erhe_camera) {
            gltf_node.cameraIndex = process_camera(erhe_camera.get());
        }

        const std::shared_ptr<erhe::scene::Light> erhe_light = erhe::scene::get_attachment<erhe::scene::Light>(&erhe_node);
        if (erhe_light) {
            gltf_node.lightIndex = process_light(erhe_light.get());
        }

        // Direct children only: for_each_child_const() walks ALL descendants
        // (it delegates to the recursive for_each_const()), and process_node
        // recurses itself - using it here exported every nested node once
        // per ancestor level.
        for (const std::shared_ptr<erhe::Hierarchy>& child : erhe_node.get_children()) {
            const erhe::scene::Node* erhe_child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
            if (erhe_child_node != nullptr) {
                std::size_t gltf_child_node_index = process_node(*erhe_child_node);
                gltf_node.children.push_back(gltf_child_node_index);
            }
        }

        size_t gltf_node_index = m_gltf_asset.nodes.size();
        m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
        m_erhe_node_to_gltf_node_index.insert({&erhe_node, gltf_node_index});
        record_serialized_node_flags(erhe_node, gltf_node_index);
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

#if FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES
    [[nodiscard]] static auto from_physics_combine_mode(const Physics_combine_mode mode) -> fastgltf::CombineMode
    {
        switch (mode) {
            case Physics_combine_mode::e_average:  return fastgltf::CombineMode::Average;
            case Physics_combine_mode::e_minimum:  return fastgltf::CombineMode::Minimum;
            case Physics_combine_mode::e_maximum:  return fastgltf::CombineMode::Maximum;
            case Physics_combine_mode::e_multiply: return fastgltf::CombineMode::Multiply;
            default:                               return fastgltf::CombineMode::Average;
        }
    }

    [[nodiscard]] auto find_gltf_node_index(const std::shared_ptr<erhe::scene::Node>& node) const -> std::optional<std::size_t>
    {
        if (!node) {
            return std::nullopt;
        }
        const auto it = m_erhe_node_to_gltf_node_index.find(node.get());
        if (it == m_erhe_node_to_gltf_node_index.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Maps a carrier geometry to fastgltf: implicit shape index, mesh index
    // (current spec; the mesh is exported on demand) or node index (older
    // spec revision). Returns nullopt (with a warning) when nothing usable
    // can be referenced.
    [[nodiscard]] auto to_gltf_geometry(const Physics_node_geometry& geometry, const std::string& owner_name) -> std::optional<fastgltf::Geometry>
    {
        fastgltf::Geometry out{};
        if (geometry.shape_index.has_value()) {
            out.shape = geometry.shape_index.value();
        } else if (geometry.mesh) {
            out.mesh = process_mesh(geometry.mesh.get());
        } else if (geometry.node) {
            const std::optional<std::size_t> node_index = find_gltf_node_index(geometry.node);
            if (!node_index.has_value()) {
                log_gltf->warn("glTF physics export: '{}' collider geometry references a node outside the exported subtree - skipping", owner_name);
                return std::nullopt;
            }
            out.node = node_index.value();
        } else {
            log_gltf->warn("glTF physics export: '{}' collider geometry has neither shape nor mesh nor node - skipping", owner_name);
            return std::nullopt;
        }
        out.convexHull = geometry.convex_hull;
        return out;
    }

    void process_physics()
    {
        const Gltf_physics_data& physics = *m_arguments.physics_data;

        for (const Physics_shape& shape : physics.shapes) {
            switch (shape.type) {
                case Physics_shape_type::e_sphere: {
                    fastgltf::SphereShape sphere{};
                    sphere.radius = shape.radius;
                    m_gltf_asset.shapes.emplace_back(sphere);
                    break;
                }
                case Physics_shape_type::e_box: {
                    fastgltf::BoxShape box{};
                    box.size = fastgltf::math::fvec3{shape.size.x, shape.size.y, shape.size.z};
                    m_gltf_asset.shapes.emplace_back(box);
                    break;
                }
                case Physics_shape_type::e_capsule: {
                    fastgltf::CapsuleShape capsule{};
                    capsule.height       = shape.height;
                    capsule.radiusBottom = shape.radius_bottom;
                    capsule.radiusTop    = shape.radius_top;
                    m_gltf_asset.shapes.emplace_back(capsule);
                    break;
                }
                case Physics_shape_type::e_cylinder:
                default: {
                    fastgltf::CylinderShape cylinder{};
                    cylinder.height       = shape.height;
                    cylinder.radiusBottom = shape.radius_bottom;
                    cylinder.radiusTop    = shape.radius_top;
                    m_gltf_asset.shapes.emplace_back(cylinder);
                    break;
                }
            }
        }

        for (const Physics_material_description& material : physics.materials) {
            fastgltf::PhysicsMaterial out{};
            out.staticFriction     = material.static_friction;
            out.dynamicFriction    = material.dynamic_friction;
            out.restitution        = material.restitution;
            out.frictionCombine    = from_physics_combine_mode(material.friction_combine);
            out.restitutionCombine = from_physics_combine_mode(material.restitution_combine);
            m_gltf_asset.physicsMaterials.push_back(out);
        }

        for (const Physics_collision_filter_description& filter : physics.collision_filters) {
            fastgltf::CollisionFilter out{};
            for (const std::string& system : filter.collision_systems) {
                out.collisionSystems.emplace_back(system.data(), system.size());
            }
            for (const std::string& system : filter.collide_with_systems) {
                out.collideWithSystems.emplace_back(system.data(), system.size());
            }
            for (const std::string& system : filter.not_collide_with_systems) {
                out.notCollideWithSystems.emplace_back(system.data(), system.size());
            }
            m_gltf_asset.collisionFilters.emplace_back(std::move(out));
        }

        for (const Physics_joint_description& joint : physics.joints) {
            fastgltf::PhysicsJoint out{};
            for (const Physics_joint_limit& limit : joint.limits) {
                fastgltf::JointLimit out_limit{};
                for (const int axis : limit.linear_axes) {
                    out_limit.linearAxes.emplace_back(static_cast<uint8_t>(axis));
                }
                for (const int axis : limit.angular_axes) {
                    out_limit.angularAxes.emplace_back(static_cast<uint8_t>(axis));
                }
                if (limit.min.has_value()) {
                    out_limit.min = limit.min.value();
                }
                if (limit.max.has_value()) {
                    out_limit.max = limit.max.value();
                }
                if (limit.stiffness.has_value()) {
                    out_limit.stiffness = limit.stiffness.value();
                }
                out_limit.damping = limit.damping;
                out.limits.emplace_back(std::move(out_limit));
            }
            for (const Physics_joint_drive& drive : joint.drives) {
                fastgltf::JointDrive out_drive{};
                out_drive.type = (drive.type == Physics_drive_type::e_angular)
                    ? fastgltf::DriveType::Angular
                    : fastgltf::DriveType::Linear;
                out_drive.mode = (drive.mode == Physics_drive_mode::e_acceleration)
                    ? fastgltf::DriveMode::Acceleration
                    : fastgltf::DriveMode::Force;
                out_drive.axis           = static_cast<uint8_t>(drive.axis);
                out_drive.maxForce       = drive.max_force;
                out_drive.positionTarget = drive.position_target;
                out_drive.velocityTarget = drive.velocity_target;
                out_drive.stiffness      = drive.stiffness;
                out_drive.damping        = drive.damping;
                out.drives.emplace_back(out_drive);
            }
            m_gltf_asset.physicsJoints.emplace_back(std::move(out));
        }

        // Synthesized collider child nodes first, so that compound (node
        // list) triggers can reference them below.
        std::unordered_map<const erhe::scene::Node*, std::vector<std::size_t>> trigger_children_by_parent;
        for (const Physics_synthesized_collider& collider : physics.synthesized_colliders) {
            const std::optional<std::size_t> parent_index = find_gltf_node_index(collider.parent);
            if (!parent_index.has_value()) {
                log_gltf->warn("glTF physics export: synthesized collider '{}' parent is outside the exported subtree - skipping", collider.name);
                continue;
            }
            const std::optional<fastgltf::Geometry> geometry = to_gltf_geometry(collider.geometry, collider.name);
            if (!geometry.has_value()) {
                continue;
            }
            fastgltf::Node gltf_node{};
            gltf_node.name = collider.name;
            gltf_node.transform = fastgltf::TRS{
                .translation = fastgltf::math::fvec3{collider.translation.x, collider.translation.y, collider.translation.z},
                .rotation    = fastgltf::math::fquat{collider.rotation.x, collider.rotation.y, collider.rotation.z, collider.rotation.w},
                .scale       = fastgltf::math::fvec3{collider.scale.x, collider.scale.y, collider.scale.z}
            };
            gltf_node.physicsRigidBody = std::make_unique<fastgltf::PhysicsRigidBody>();
            if (collider.is_trigger) {
                fastgltf::GeometryTrigger trigger{};
                trigger.geometry = geometry.value();
                if (collider.filter_index.has_value()) {
                    trigger.collisionFilter = collider.filter_index.value();
                }
                gltf_node.physicsRigidBody->trigger.emplace().emplace<fastgltf::GeometryTrigger>(std::move(trigger));
            } else {
                fastgltf::Collider& out_collider = gltf_node.physicsRigidBody->collider.emplace();
                out_collider.geometry = geometry.value();
                if (collider.material_index.has_value()) {
                    out_collider.physicsMaterial = collider.material_index.value();
                }
                if (collider.filter_index.has_value()) {
                    out_collider.collisionFilter = collider.filter_index.value();
                }
            }
            const std::size_t node_index = m_gltf_asset.nodes.size();
            m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
            m_gltf_asset.nodes[parent_index.value()].children.push_back(node_index);
            if (collider.is_trigger) {
                trigger_children_by_parent[collider.parent.get()].push_back(node_index);
            }
        }

        for (const Physics_node_description& description : physics.node_physics) {
            const std::optional<std::size_t> node_index = find_gltf_node_index(description.node);
            if (!node_index.has_value()) {
                log_gltf->warn(
                    "glTF physics export: physics node '{}' is outside the exported subtree - skipping",
                    description.node ? description.node->get_name() : std::string{"<null>"}
                );
                continue;
            }
            const std::string& node_name = description.node->get_name();
            fastgltf::Node& gltf_node = m_gltf_asset.nodes[node_index.value()];
            gltf_node.physicsRigidBody = std::make_unique<fastgltf::PhysicsRigidBody>();
            fastgltf::PhysicsRigidBody& rigid_body = *gltf_node.physicsRigidBody;

            if (description.motion.has_value()) {
                const Physics_node_motion& in_motion = description.motion.value();
                fastgltf::Motion& motion = rigid_body.motion.emplace();
                motion.isKinematic = in_motion.is_kinematic;
                if (in_motion.mass.has_value()) {
                    motion.mass = in_motion.mass.value();
                }
                motion.centerOfMass = fastgltf::math::fvec3{in_motion.center_of_mass.x, in_motion.center_of_mass.y, in_motion.center_of_mass.z};
                if (in_motion.inertia_diagonal.has_value()) {
                    const glm::vec3 d = in_motion.inertia_diagonal.value();
                    motion.inertialDiagonal = fastgltf::math::fvec3{d.x, d.y, d.z};
                }
                if (in_motion.inertia_orientation.has_value()) {
                    const glm::quat q = in_motion.inertia_orientation.value();
                    motion.inertialOrientation = fastgltf::math::fvec4{q.x, q.y, q.z, q.w};
                }
                motion.linearVelocity  = fastgltf::math::fvec3{in_motion.linear_velocity.x, in_motion.linear_velocity.y, in_motion.linear_velocity.z};
                motion.angularVelocity = fastgltf::math::fvec3{in_motion.angular_velocity.x, in_motion.angular_velocity.y, in_motion.angular_velocity.z};
                motion.gravityFactor   = in_motion.gravity_factor;
            }

            if (description.collider.has_value()) {
                const Physics_node_collider& in_collider = description.collider.value();
                const std::optional<fastgltf::Geometry> geometry = to_gltf_geometry(in_collider.geometry, node_name);
                if (geometry.has_value()) {
                    fastgltf::Collider& collider = rigid_body.collider.emplace();
                    collider.geometry = geometry.value();
                    if (in_collider.material_index.has_value()) {
                        collider.physicsMaterial = in_collider.material_index.value();
                    }
                    if (in_collider.filter_index.has_value()) {
                        collider.collisionFilter = in_collider.filter_index.value();
                    }
                }
            }

            if (description.trigger.has_value()) {
                const Physics_node_trigger& in_trigger = description.trigger.value();
                if (in_trigger.geometry.has_value()) {
                    const std::optional<fastgltf::Geometry> geometry = to_gltf_geometry(in_trigger.geometry.value(), node_name);
                    if (geometry.has_value()) {
                        fastgltf::GeometryTrigger trigger{};
                        trigger.geometry = geometry.value();
                        if (in_trigger.filter_index.has_value()) {
                            trigger.collisionFilter = in_trigger.filter_index.value();
                        }
                        rigid_body.trigger.emplace().emplace<fastgltf::GeometryTrigger>(std::move(trigger));
                    }
                } else {
                    // Compound trigger: a node list over this node's
                    // synthesized trigger children.
                    const auto it = trigger_children_by_parent.find(description.node.get());
                    if ((it != trigger_children_by_parent.end()) && !it->second.empty()) {
                        fastgltf::NodeTrigger node_trigger{};
                        for (const std::size_t trigger_node_index : it->second) {
                            node_trigger.nodes.emplace_back(trigger_node_index);
                        }
                        rigid_body.trigger.emplace().emplace<fastgltf::NodeTrigger>(std::move(node_trigger));
                    } else {
                        log_gltf->warn("glTF physics export: trigger '{}' has no geometry and no synthesized members - skipping trigger", node_name);
                    }
                }
            }

            if (description.joint.has_value()) {
                const Physics_node_joint& in_joint = description.joint.value();
                const std::optional<std::size_t> connected_index = find_gltf_node_index(in_joint.connected_node);
                if (!connected_index.has_value()) {
                    log_gltf->warn("glTF physics export: joint on '{}' connects to a node outside the exported subtree - skipping joint", node_name);
                } else {
                    fastgltf::Joint& joint = rigid_body.joint.emplace();
                    joint.connectedNode   = connected_index.value();
                    joint.joint           = in_joint.joint_index;
                    joint.enableCollision = in_joint.enable_collision;
                }
            }
        }

        if (!m_gltf_asset.shapes.empty()) {
            m_gltf_asset.extensionsUsed.emplace_back("KHR_implicit_shapes");
        }
        const bool any_physics =
            !physics.node_physics.empty()          ||
            !physics.synthesized_colliders.empty() ||
            !m_gltf_asset.physicsMaterials.empty() ||
            !m_gltf_asset.collisionFilters.empty() ||
            !m_gltf_asset.physicsJoints.empty();
        if (any_physics) {
            m_gltf_asset.extensionsUsed.emplace_back("KHR_physics_rigid_bodies");
        }
    }
#endif

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
    const Gltf_export_arguments& m_arguments;
    std::map<std::string, std::size_t> m_uri_to_external_asset_index;
    fastgltf::Asset          m_gltf_asset;

    std::unordered_map<const erhe::scene::Node*, std::size_t>  m_erhe_node_to_gltf_node_index;
    std::unordered_map<const erhe::scene::Light*, std::size_t> m_exported_lights;
    std::unordered_map<std::size_t, uint64_t>                  m_gltf_node_index_to_flags; // serialized erhe Item flags per glTF node index
};

auto Gltf_exporter::export_gltf() -> std::string
{
    m_gltf_asset.assetInfo = fastgltf::AssetInfo{
        .gltfVersion = "2.0",
        .copyright   = "",
        .generator   = "erhe"
    };
    m_gltf_asset.defaultScene = std::size_t{0};

    {
        fastgltf::Scene scene{};
        // Direct children only (see the matching comment in process_node);
        // the recursive for_each_child_const() made every descendant a
        // scene root and re-exported nested subtrees.
        for (const std::shared_ptr<erhe::Hierarchy>& child : m_arguments.root_node.get_children()) {
            const erhe::scene::Node* erhe_node = dynamic_cast<const erhe::scene::Node*>(child.get());
            if (erhe_node != nullptr) {
                size_t node_index = process_node(*erhe_node);
                scene.nodeIndices.push_back(node_index);
            }
        }
        m_gltf_asset.scenes.push_back(std::move(scene));
    }

#if FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES
    // Before combine_buffers(): mesh-keyed collider geometry may export
    // additional meshes (process_mesh), which adds buffers.
    if (m_arguments.physics_data != nullptr) {
        process_physics();
    }
#endif

    // glTF 2.1 features in use: loaders must understand files /
    // externalAssets, so declare both the version and the minimum version.
    // Plain exports keep writing glTF 2.0.
    if (!m_gltf_asset.externalAssets.empty()) {
        m_gltf_asset.assetInfo->gltfVersion = "2.1";
        m_gltf_asset.assetInfo->minVersion  = "2.1";
    }

    // KHR_lights_punctual: exported when process_light() emitted any light;
    // the fastgltf exporter writes the lights array but leaves declaring the
    // extension to the application.
    if (!m_gltf_asset.lights.empty()) {
        m_gltf_asset.extensionsUsed.emplace_back("KHR_lights_punctual");
    }

    combine_buffers();

    // Extras write context: gltf material index -> erhe Material* (erhe
    // material extras) and gltf node index -> serialized erhe Item flags.
    class Export_extras_context
    {
    public:
        std::unordered_map<std::size_t, const erhe::primitive::Material*> index_to_material;
        const std::unordered_map<std::size_t, uint64_t>*                  node_index_to_flags{nullptr};
    };
    Export_extras_context export_extras_context;
    for (const auto& [material_ptr, gltf_index] : m_exported_materials) {
        export_extras_context.index_to_material[gltf_index] = material_ptr;
    }
    export_extras_context.node_index_to_flags = &m_gltf_node_index_to_flags;

    fastgltf::Exporter exporter{};
    exporter.setUserPointer(&export_extras_context);
    exporter.setExtrasWriteCallback(
        [](std::size_t object_index, fastgltf::Category object_type, void* user_pointer) -> std::optional<std::string> {
            const Export_extras_context* context = static_cast<const Export_extras_context*>(user_pointer);

            if (object_type == fastgltf::Category::Nodes) {
                const auto node_it = context->node_index_to_flags->find(object_index);
                if (node_it == context->node_index_to_flags->end()) {
                    return std::nullopt;
                }
                std::string out{"{\"erhe_flags\": ["};
                const char* sep = "";
                for (const Serialized_item_flag& serialized_flag : c_serialized_item_flags) {
                    if ((node_it->second & serialized_flag.bit) != 0) {
                        out += sep; out += "\""; out += serialized_flag.name; out += "\"";
                        sep = ", ";
                    }
                }
                out += "]}";
                return out;
            }

            if (object_type != fastgltf::Category::Materials) {
                return std::nullopt;
            }
            const auto it = context->index_to_material.find(object_index);
            if (it == context->index_to_material.end()) {
                return std::nullopt;
            }
            const erhe::primitive::Material*      material = it->second;
            const erhe::primitive::Material_data& data     = material->data;

            // Skip writing the bxdf_model extra when it's the
            // round-trip default (isotropic_brdf) or when it's unlit
            // (KHR_materials_unlit carries that case at the standard
            // extension level).
            const bool emit_bxdf_model =
                (data.bxdf_model != erhe::primitive::Bxdf_model::isotropic_brdf) &&
                (data.bxdf_model != erhe::primitive::Bxdf_model::unlit);
            const bool emit_roughness_y =
                (data.roughness.x != data.roughness.y);
            // alphaMode covers opaque / alpha_blend / alpha_test. Other
            // modes need extras so they survive a round-trip.
            const bool emit_blending_mode =
                (data.blending_mode != erhe::primitive::Material_blending_mode::opaque) &&
                (data.blending_mode != erhe::primitive::Material_blending_mode::alpha_blend) &&
                (data.blending_mode != erhe::primitive::Material_blending_mode::alpha_test);
            // 1 is the round-trip default (set by Material_data).
            const bool emit_circular_brushed_metal_tex_coord =
                (data.circular_brushed_metal_tex_coord != 1u);

            if (!emit_roughness_y &&
                !emit_bxdf_model &&
                !emit_blending_mode &&
                !data.use_circular_brushed_metal &&
                !emit_circular_brushed_metal_tex_coord &&
                !data.use_aniso_control)
            {
                return std::nullopt;
            }

            std::string out{"{"};
            const char* sep = "";
            if (emit_roughness_y) {
                out += sep; out += "\"roughness_y\": "; out += std::to_string(data.roughness.y);
                sep = ", ";
            }
            if (emit_bxdf_model) {
                out += sep; out += "\"bxdf_model\": \""; out += bxdf_model_to_c_str(data.bxdf_model); out += "\"";
                sep = ", ";
            }
            if (emit_blending_mode) {
                out += sep; out += "\"blending_mode\": \""; out += blending_mode_to_c_str(data.blending_mode); out += "\"";
                sep = ", ";
            }
            if (data.use_circular_brushed_metal) {
                out += sep; out += "\"use_circular_brushed_metal\": true";
                sep = ", ";
            }
            if (emit_circular_brushed_metal_tex_coord) {
                out += sep; out += "\"circular_brushed_metal_tex_coord\": "; out += std::to_string(data.circular_brushed_metal_tex_coord);
                sep = ", ";
            }
            if (data.use_aniso_control) {
                out += sep; out += "\"use_aniso_control\": true";
                sep = ", ";
            }
            out += "}";
            return out;
        }
    );

    if (m_arguments.binary)
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

[[nodiscard]] auto export_gltf(const Gltf_export_arguments& arguments) -> std::string
{
    Gltf_exporter exporter{arguments};
    return exporter.export_gltf();
}

[[nodiscard]] auto export_gltf(const erhe::scene::Node& root_node, bool binary, const Gltf_physics_data* physics_data) -> std::string
{
    return export_gltf(
        Gltf_export_arguments{
            .root_node    = root_node,
            .binary       = binary,
            .physics_data = physics_data
        }
    );
}

} // namespace erhe::gltf

