// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "gltf_fastgltf.hpp"
#include "gltf_item_flags.hpp"
#include "gltf_log.hpp"
#include "image_transfer.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
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
#include <random>
#include <span>
#include <sstream>
#include <string_view>
#include <string>
#include <unordered_set>
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

// Sniff the media type of an encoded image stream from its magic bytes
// (Gltf_image_source retention; doc/gltf-scene-roundtrip-plan.md phase 0).
// Returns an empty string when unrecognized.
[[nodiscard]] auto sniff_image_mime_type(const std::vector<std::byte>& bytes) -> std::string
{
    const auto starts_with = [&bytes](const std::initializer_list<unsigned char> magic, const std::size_t offset = 0) -> bool {
        if (bytes.size() < (offset + magic.size())) {
            return false;
        }
        std::size_t i = offset;
        for (const unsigned char c : magic) {
            if (static_cast<unsigned char>(bytes[i]) != c) {
                return false;
            }
            ++i;
        }
        return true;
    };
    if (starts_with({0x89u, 'P', 'N', 'G'}))                                  return "image/png";
    if (starts_with({0xFFu, 0xD8u, 0xFFu}))                                   return "image/jpeg";
    if (starts_with({0xABu, 'K', 'T', 'X'}))                                  return "image/ktx2";
    if (starts_with({'R', 'I', 'F', 'F'}) && starts_with({'W', 'E', 'B', 'P'}, 8)) return "image/webp";
    if (starts_with({'D', 'D', 'S', ' '}))                                    return "image/vnd-ms.dds";
    return {};
}

// Gltf_image_source::mime_type string -> fastgltf::MimeType (export side).
[[nodiscard]] auto mime_type_from_string(const std::string& mime_type) -> fastgltf::MimeType
{
    if (mime_type == "image/png")        return fastgltf::MimeType::PNG;
    if (mime_type == "image/jpeg")       return fastgltf::MimeType::JPEG;
    if (mime_type == "image/ktx2")       return fastgltf::MimeType::KTX2;
    if (mime_type == "image/webp")       return fastgltf::MimeType::WEBP;
    if (mime_type == "image/vnd-ms.dds") return fastgltf::MimeType::DDS;
    return fastgltf::MimeType::None;
}

// erhe Item flags that round-trip through glTF node "extras" as
// {"erhe_flags": ["<name>", ...]}. Neither glTF 2.0 core nor the glTF 2.1
// proposals (KhronosGroup/glTF#2585 and related) define per-node
// editor/authoring flags such as "exclude from external-asset
// instantiation" (see doc/gltf_2_1_item_flags_comment.md), so
// erhe-specific bits ride in extras by name; unknown names are ignored on
// import so the list can grow without breaking older builds.
// LEGACY READ PATH: new files carry flags in the ERHE_node extension
// (doc/gltf-scene-roundtrip-plan.md phase 3); the extras form is still
// parsed for files written before the migration.
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

// The persistent (authored) Item flag helpers live in gltf_item_flags.{hpp,cpp}
// (shared with the editor-domain extension builders, e.g. ERHE_layout).

// erhe::scene::Projection::Type <-> ERHE_camera projection_type string.
[[nodiscard]] auto projection_type_name(const erhe::scene::Projection::Type type) -> const char*
{
    switch (type) {
        case erhe::scene::Projection::Type::other:                 return "other";
        case erhe::scene::Projection::Type::perspective_horizontal:return "perspective_horizontal";
        case erhe::scene::Projection::Type::perspective_vertical:  return "perspective_vertical";
        case erhe::scene::Projection::Type::perspective:           return "perspective";
        case erhe::scene::Projection::Type::perspective_xr:        return "perspective_xr";
        case erhe::scene::Projection::Type::orthogonal_horizontal: return "orthogonal_horizontal";
        case erhe::scene::Projection::Type::orthogonal_vertical:   return "orthogonal_vertical";
        case erhe::scene::Projection::Type::orthogonal:            return "orthogonal";
        case erhe::scene::Projection::Type::orthogonal_rectangle:  return "orthogonal_rectangle";
        case erhe::scene::Projection::Type::generic_frustum:       return "generic_frustum";
        default:                                                   return "perspective_vertical";
    }
}

[[nodiscard]] auto projection_type_from_name(const std::string_view name) -> erhe::scene::Projection::Type
{
    if (name == "other")                  return erhe::scene::Projection::Type::other;
    if (name == "perspective_horizontal") return erhe::scene::Projection::Type::perspective_horizontal;
    if (name == "perspective_vertical")   return erhe::scene::Projection::Type::perspective_vertical;
    if (name == "perspective")            return erhe::scene::Projection::Type::perspective;
    if (name == "perspective_xr")         return erhe::scene::Projection::Type::perspective_xr;
    if (name == "orthogonal_horizontal")  return erhe::scene::Projection::Type::orthogonal_horizontal;
    if (name == "orthogonal_vertical")    return erhe::scene::Projection::Type::orthogonal_vertical;
    if (name == "orthogonal")             return erhe::scene::Projection::Type::orthogonal;
    if (name == "orthogonal_rectangle")   return erhe::scene::Projection::Type::orthogonal_rectangle;
    if (name == "generic_frustum")        return erhe::scene::Projection::Type::generic_frustum;
    return erhe::scene::Projection::Type::perspective_vertical;
}

// Applies a parsed flag-name array exactly: every persistent flag is
// enabled when listed and disabled when not (unknown names ignored).
void apply_persistent_flags(erhe::Item_base& item, const simdjson::dom::array& flags_array)
{
    uint64_t listed_bits = 0;
    for (const simdjson::dom::element flag_element : flags_array) {
        std::string_view flag_name;
        if (flag_element.get_string().get(flag_name) != simdjson::SUCCESS) {
            continue;
        }
        listed_bits |= persistent_item_flag_from_name(flag_name);
    }
    apply_persistent_item_flags(item, listed_bits);
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

    // glTF 2.1 unique ID carry (KhronosGroup/glTF#2597): imported items keep
    // the source file's uid so identity survives the round trip - export
    // preserves a carried uid verbatim instead of generating a new one.
    template <typename T>
    static void copy_uid(const T& gltf_object, erhe::Item_base& item)
    {
        if (!gltf_object.uid.empty()) {
            item.set_gltf_uid(std::string_view{gltf_object.uid});
        }
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
        m_data_out.image_sources.resize(m_asset->images.size());
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
        m_mesh_uid_claimed.resize(mesh_count);
        for (std::size_t i = 0; i < mesh_count; ++i) {
            pre_parse_mesh(i);
        }

        ::tf::Taskflow taskflow;
        for (std::size_t i = 0; i < mesh_count; ++i) {
            const fastgltf::Mesh& mesh = m_asset->meshes[i];
            erhe::scene::Mesh& erhe_mesh = *m_data_out.meshes[i].get();
            taskflow.emplace(
                [this, i, &mesh, &erhe_mesh]()
                {
                    parse_mesh(i, mesh, erhe_mesh);
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
            out_file.uid       = std::string{file.uid};
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
            out_external_asset.uid        = std::string{external_asset.uid};
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
        copy_uid(animation, *erhe_animation);
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
        std::shared_ptr<Gltf_image_source>       image_source;
        std::visit(
            fastgltf::visitor {
                [](auto& arg) {
                    static_cast<void>(arg);
                    ERHE_FATAL("TODO Unsupported image source");
                },
                [&](const fastgltf::sources::BufferView& buffer_view_source){
                    erhe_texture = load_image_buffer(buffer_view_source.bufferViewIndex, image_index, linear, image_name);
                    // Retain the encoded source stream for byte-exact
                    // re-embedding on export (phase 0).
                    if (erhe_texture) {
                        const fastgltf::BufferView& buffer_view = m_asset->bufferViews[buffer_view_source.bufferViewIndex];
                        const fastgltf::Buffer&     buffer      = m_asset->buffers.at(buffer_view.bufferIndex);
                        if (const fastgltf::sources::Array* array_source = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
                            const std::byte* start = reinterpret_cast<const std::byte*>(array_source->bytes.data()) + buffer_view.byteOffset;
                            image_source = std::make_shared<Gltf_image_source>();
                            image_source->encoded_bytes.assign(start, start + buffer_view.byteLength);
                        }
                    }
                },
                [&](const fastgltf::sources::URI& uri){
                    // Resolve on a copy: replace_filename() mutates in place,
                    // and m_arguments.path must keep naming the glTF file
                    // (source paths of nodes parsed later, resource names).
                    std::filesystem::path relative_path = uri.uri.fspath();
                    std::filesystem::path image_path    = m_arguments.path;
                    image_path.replace_filename(relative_path);
                    erhe_texture = load_image_file(image_path, linear, image_name);
                    if (erhe_texture) {
                        const std::optional<std::string> file_content = erhe::file::read("gltf image source retention", image_path);
                        if (file_content.has_value() && !file_content->empty()) {
                            const std::byte* start = reinterpret_cast<const std::byte*>(file_content->data());
                            image_source = std::make_shared<Gltf_image_source>();
                            image_source->encoded_bytes.assign(start, start + file_content->size());
                        }
                    }
                }
            },
            image.data
        );

        if (image_source) {
            image_source->mime_type = sniff_image_mime_type(image_source->encoded_bytes);
        }
        if (erhe_texture) {
            copy_uid(image, *erhe_texture);
        }
        m_data_out.images[image_index]        = erhe_texture;
        m_data_out.image_sources[image_index] = image_source;
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
            copy_uid(material, *new_material);
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
        copy_uid(camera, *erhe_camera);
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
                    projection->z_near          = perspective.znear;
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

    // ERHE_geometry extension JSON per (mesh index, primitive index),
    // captured by the generic extensions parse callback and handed over
    // before parse_and_build(). Primitives listed here are
    // geometry-normative: the full erhe::geometry::Geometry is rebuilt
    // instead of the Triangle_soup path.
    std::map<std::pair<std::size_t, std::size_t>, std::string> m_primitive_geometry_extensions;
public:
    void set_primitive_geometry_extensions(std::map<std::pair<std::size_t, std::size_t>, std::string>&& extensions)
    {
        m_primitive_geometry_extensions = std::move(extensions);
    }
private:

    // Reads a uint32 accessor referenced by an ERHE_geometry field.
    [[nodiscard]] auto read_extension_u32_accessor(
        const simdjson::dom::object& extension_object,
        const char*                  key,
        std::vector<uint32_t>&       out_values
    ) -> bool
    {
        std::uint64_t accessor_index{0};
        if (extension_object.at_key(key).get_uint64().get(accessor_index) != simdjson::SUCCESS) {
            return false;
        }
        if (accessor_index >= m_asset->accessors.size()) {
            return false;
        }
        const fastgltf::Accessor& accessor = m_asset->accessors[accessor_index];
        out_values.resize(accessor.count);
        fastgltf::iterateAccessorWithIndex<uint32_t>(
            m_asset.get(), accessor,
            [&](const uint32_t value, const std::size_t index) {
                out_values[index] = value;
            }
        );
        return true;
    }

    // Rebuilds a full Geometry from a primitive carrying ERHE_geometry
    // (doc/gltf-scene-roundtrip-plan.md phase 2): POSITION accessor +
    // ring accessors + raw attribute-store buffer views. Returns null on
    // any inconsistency; the caller falls back to the Triangle_soup path.
    [[nodiscard]] auto parse_erhe_geometry_primitive(
        const fastgltf::Primitive& primitive,
        const std::string&         extension_json,
        const std::string&         name
    ) -> std::shared_ptr<erhe::primitive::Primitive>
    {
        simdjson::dom::parser json_parser;
        simdjson::dom::element root;
        if (json_parser.parse(simdjson::padded_string{extension_json}).get(root) != simdjson::SUCCESS) {
            log_gltf->warn("ERHE_geometry: primitive '{}' extension JSON does not parse", name);
            return {};
        }
        simdjson::dom::object extension_object;
        if (root.get_object().get(extension_object) != simdjson::SUCCESS) {
            log_gltf->warn("ERHE_geometry: primitive '{}' extension is not an object", name);
            return {};
        }

        erhe::geometry::Geometry_flat_data flat;
        const auto read_count = [&extension_object](const char* key, std::size_t& out_value) -> bool {
            std::uint64_t value{0};
            if (extension_object.at_key(key).get_uint64().get(value) != simdjson::SUCCESS) {
                return false;
            }
            out_value = static_cast<std::size_t>(value);
            return true;
        };
        if (
            !read_count("vertex_count", flat.vertex_count) ||
            !read_count("facet_count",  flat.facet_count)  ||
            !read_count("corner_count", flat.corner_count) ||
            !read_count("edge_count",   flat.edge_count)
        ) {
            log_gltf->warn("ERHE_geometry: primitive '{}' is missing element counts", name);
            return {};
        }

        const auto* position_attribute = primitive.findAttribute("POSITION");
        if (position_attribute == primitive.attributes.cend()) {
            log_gltf->warn("ERHE_geometry: primitive '{}' has no POSITION attribute", name);
            return {};
        }
        const fastgltf::Accessor& position_accessor = m_asset->accessors[position_attribute->accessorIndex];
        if (position_accessor.count != flat.vertex_count) {
            log_gltf->warn("ERHE_geometry: primitive '{}' POSITION count does not match vertex_count", name);
            return {};
        }
        flat.positions.resize(flat.vertex_count * 3);
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            m_asset.get(), position_accessor,
            [&](const fastgltf::math::fvec3 value, const std::size_t index) {
                flat.positions[(index * 3) + 0] = value.x();
                flat.positions[(index * 3) + 1] = value.y();
                flat.positions[(index * 3) + 2] = value.z();
            }
        );

        if (
            !read_extension_u32_accessor(extension_object, "facet_vertex_counts",  flat.facet_vertex_counts) ||
            !read_extension_u32_accessor(extension_object, "facet_vertex_indices", flat.facet_vertex_indices)
        ) {
            log_gltf->warn("ERHE_geometry: primitive '{}' is missing ring accessors", name);
            return {};
        }
        if (flat.edge_count > 0) {
            if (!read_extension_u32_accessor(extension_object, "edge_vertices", flat.edge_vertex_indices)) {
                log_gltf->warn("ERHE_geometry: primitive '{}' is missing the edge_vertices accessor", name);
                return {};
            }
        }

        simdjson::dom::array attributes_array;
        if (extension_object.at_key("attributes").get_array().get(attributes_array) == simdjson::SUCCESS) {
            for (simdjson::dom::element attribute_element : attributes_array) {
                simdjson::dom::object attribute_object;
                if (attribute_element.get_object().get(attribute_object) != simdjson::SUCCESS) {
                    continue;
                }
                erhe::geometry::Geometry_attribute_record record;
                std::string_view string_value;
                std::uint64_t    number_value{0};
                if (attribute_object.at_key("name").get_string().get(string_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.name = std::string{string_value};
                if (attribute_object.at_key("element").get_string().get(string_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.element = std::string{string_value};
                if (attribute_object.at_key("element_type").get_string().get(string_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.element_type = std::string{string_value};
                if (attribute_object.at_key("element_size").get_uint64().get(number_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.element_size = static_cast<std::size_t>(number_value);
                if (attribute_object.at_key("dimension").get_uint64().get(number_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.dimension = static_cast<std::size_t>(number_value);
                if (attribute_object.at_key("item_count").get_uint64().get(number_value) != simdjson::SUCCESS) {
                    continue;
                }
                record.item_count = static_cast<std::size_t>(number_value);
                if (attribute_object.at_key("buffer_view").get_uint64().get(number_value) != simdjson::SUCCESS) {
                    continue;
                }
                if (number_value >= m_asset->bufferViews.size()) {
                    log_gltf->warn("ERHE_geometry: primitive '{}' attribute '{}' has an out-of-range buffer view", name, record.name);
                    return {};
                }
                const fastgltf::BufferView& buffer_view = m_asset->bufferViews[number_value];
                const fastgltf::Buffer&     buffer      = m_asset->buffers.at(buffer_view.bufferIndex);
                const auto* array_source = std::get_if<fastgltf::sources::Array>(&buffer.data);
                if (array_source == nullptr) {
                    log_gltf->warn("ERHE_geometry: primitive '{}' attribute '{}' buffer is not loaded", name, record.name);
                    return {};
                }
                const std::byte* start = reinterpret_cast<const std::byte*>(array_source->bytes.data()) + buffer_view.byteOffset;
                record.bytes.assign(start, start + buffer_view.byteLength);
                flat.attributes.push_back(std::move(record));
            }
        }

        auto geometry = std::make_shared<erhe::geometry::Geometry>(name);
        if (!erhe::geometry::geometry_from_flat_data(flat, *geometry)) {
            log_gltf->warn("ERHE_geometry: primitive '{}' flat data is inconsistent - falling back to triangles", name);
            return {};
        }
        // Deliberately NO process() here: the dump restored every derived
        // attribute (edges, centroids, smooth normals, texcoords) byte-exact
        // and facet adjacency was reconnected by geometry_from_flat_data.
        // Recomputing would overwrite the dump and break the bit-exact
        // round-trip; processing runs only for genuinely missing data
        // (finalize_imported_meshes gates on missing edges).
        return std::make_shared<erhe::primitive::Primitive>(geometry);
    }

    void parse_primitive(
        erhe::scene::Mesh&    erhe_mesh,
        const std::size_t     mesh_index,
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

        const auto geometry_extension_it = m_primitive_geometry_extensions.find({mesh_index, primitive_index});
        if (geometry_extension_it != m_primitive_geometry_extensions.end()) {
            std::shared_ptr<erhe::primitive::Primitive> geometry_primitive =
                parse_erhe_geometry_primitive(primitive, geometry_extension_it->second, name);
            if (geometry_primitive) {
                erhe_mesh.add_primitive(geometry_primitive, erhe_material);
                return;
            }
        }

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
        copy_uid(skin, *erhe_skin);
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
        copy_uid(mesh, *erhe_mesh);
        m_data_out.meshes[mesh_index] = erhe_mesh;
    }
    void parse_mesh(const std::size_t mesh_index, const fastgltf::Mesh& mesh, erhe::scene::Mesh& erhe_mesh)
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
            parse_primitive(erhe_mesh, mesh_index, mesh, i);
        }
    }

    std::vector<std::size_t> m_nodes_with_skin;
    // Per glTF mesh: has a node clone already claimed the mesh's uid
    // (see the mesh instantiation in parse_node)?
    std::vector<bool> m_mesh_uid_claimed;
    void parse_node(const std::size_t node_index, const std::shared_ptr<erhe::scene::Node>& parent)
    {
        ERHE_PROFILE_FUNCTION();

        const fastgltf::Node& node = m_asset->nodes[node_index];

        const std::string node_name = safe_resource_name(node.name, "node", node_index);
        log_gltf->trace("Node: node index = {}, name = {}", node_index, node.name);
        auto erhe_node = std::make_shared<erhe::scene::Node>(node_name);
        erhe_node->set_source_path(m_arguments.path);
        copy_uid(node, *erhe_node);
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
            // The clone starts uid-less (a clone is a new object); the FIRST
            // instantiation of a glTF mesh inherits the file identity so it
            // round-trips. Later instantiations of the same mesh are
            // additional objects (a glTF mesh shared by N nodes re-exports
            // as N meshes) and get fresh uids at export.
            if (!template_mesh.get_gltf_uid().empty() && !m_mesh_uid_claimed[mesh_index]) {
                erhe_mesh->set_gltf_uid(template_mesh.get_gltf_uid());
                m_mesh_uid_claimed[mesh_index] = true;
            }
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
    // completes. The same context also captures ERHE_* extension payloads
    // through the generic passthrough (both callbacks share userPointer).
    class Parse_extras_context
    {
    public:
        std::unordered_map<std::size_t, Material_extras> material_extras;
        std::unordered_map<std::size_t, uint64_t>        node_flags; // serialized erhe Item flags (c_serialized_item_flags)

        // Captured ERHE_* extension payloads, keyed by the future object
        // index (the callback fires before each object is appended).
        Gltf_raw_extensions                                      asset_extensions;
        std::vector<std::pair<std::size_t, Gltf_raw_extensions>> scene_extensions;
        std::vector<std::pair<std::size_t, Gltf_raw_extensions>> node_extensions;
        std::vector<std::pair<std::size_t, Gltf_raw_extensions>> camera_extensions;
        std::vector<std::pair<std::size_t, Gltf_raw_extensions>> material_extensions;
        std::vector<std::pair<std::size_t, Gltf_raw_extensions>> mesh_extensions;
        std::vector<std::pair<std::pair<std::size_t, std::size_t>, Gltf_raw_extensions>> mesh_primitive_extensions;
    };
    Parse_extras_context parse_extras_context;

    fastgltf::Parser fastgltf_parser{extensions};
    fastgltf_parser.setUserPointer(&parse_extras_context);
    fastgltf_parser.setExtensionsParseCallback(
        [](simdjson::dom::object* extensions_object, std::size_t object_index, std::size_t sub_object_index, fastgltf::Category object_type, void* user_pointer) {
            if (extensions_object == nullptr) {
                return;
            }
            Parse_extras_context* context = static_cast<Parse_extras_context*>(user_pointer);
            Gltf_raw_extensions captured;
            for (const simdjson::dom::key_value_pair& field : *extensions_object) {
                const std::string_view key = field.key;
                if (!key.starts_with("ERHE_")) {
                    continue; // typed extensions keep their typed paths
                }
                std::ostringstream json_stream;
                json_stream << field.value; // simdjson dom streaming emits minified JSON
                captured.entries.emplace_back(std::string{key}, json_stream.str());
            }
            if (captured.entries.empty()) {
                return;
            }
            switch (object_type) {
                case fastgltf::Category::Asset:     context->asset_extensions = std::move(captured); break;
                case fastgltf::Category::Scenes:    context->scene_extensions   .emplace_back(object_index, std::move(captured)); break;
                case fastgltf::Category::Nodes:     context->node_extensions    .emplace_back(object_index, std::move(captured)); break;
                case fastgltf::Category::Cameras:   context->camera_extensions  .emplace_back(object_index, std::move(captured)); break;
                case fastgltf::Category::Materials: context->material_extensions.emplace_back(object_index, std::move(captured)); break;
                case fastgltf::Category::Meshes: {
                    if (sub_object_index == fastgltf::invalidSubObjectIndex) {
                        context->mesh_extensions.emplace_back(object_index, std::move(captured));
                    } else {
                        context->mesh_primitive_extensions.emplace_back(std::make_pair(object_index, sub_object_index), std::move(captured));
                    }
                    break;
                }
                default: {
                    break;
                }
            }
        }
    );
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

    // Hand captured ERHE_geometry payloads to the parser: primitives
    // carrying them are geometry-normative and rebuild a full
    // erhe::geometry::Geometry instead of a Triangle_soup.
    {
        std::map<std::pair<std::size_t, std::size_t>, std::string> geometry_extensions;
        for (const auto& [key, captured] : parse_extras_context.mesh_primitive_extensions) {
            for (const auto& [extension_name, extension_value] : captured.entries) {
                if (extension_name == "ERHE_geometry") {
                    geometry_extensions[key] = extension_value;
                }
            }
        }
        erhe_parser.set_primitive_geometry_extensions(std::move(geometry_extensions));
    }

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

    // Transfer captured ERHE_* extension payloads into Gltf_data (the
    // per-object vectors parallel the object vectors).
    {
        result.asset_extensions = std::move(parse_extras_context.asset_extensions);
        for (auto& [scene_index, captured] : parse_extras_context.scene_extensions) {
            if (scene_index == 0) { // only one scene is parsed
                result.scene_extensions = std::move(captured);
            }
        }
        result.node_extensions.resize(result.nodes.size());
        for (auto& [index, captured] : parse_extras_context.node_extensions) {
            if (index < result.node_extensions.size()) {
                result.node_extensions[index] = std::move(captured);
            }
        }
        result.camera_extensions.resize(result.cameras.size());
        for (auto& [index, captured] : parse_extras_context.camera_extensions) {
            if (index < result.camera_extensions.size()) {
                result.camera_extensions[index] = std::move(captured);
            }
        }
        result.material_extensions.resize(result.materials.size());
        for (auto& [index, captured] : parse_extras_context.material_extensions) {
            if (index < result.material_extensions.size()) {
                result.material_extensions[index] = std::move(captured);
            }
        }
        result.mesh_extensions.resize(result.meshes.size());
        for (auto& [index, captured] : parse_extras_context.mesh_extensions) {
            if (index < result.mesh_extensions.size()) {
                result.mesh_extensions[index] = std::move(captured);
            }
        }
        result.mesh_primitive_extensions.resize(result.meshes.size());
        for (std::size_t m = 0; m < result.meshes.size(); ++m) {
            const std::size_t primitive_count = result.meshes[m] ? result.meshes[m]->get_primitives().size() : 0;
            result.mesh_primitive_extensions[m].resize(primitive_count);
        }
        for (auto& [key, captured] : parse_extras_context.mesh_primitive_extensions) {
            const auto [mesh_index, primitive_index] = key;
            if ((mesh_index < result.mesh_primitive_extensions.size()) &&
                (primitive_index < result.mesh_primitive_extensions[mesh_index].size()))
            {
                result.mesh_primitive_extensions[mesh_index][primitive_index] = std::move(captured);
            }
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

    // Apply library-domain ERHE_* extensions to the parsed objects:
    // ERHE_node / ERHE_light on nodes, ERHE_camera on cameras and
    // ERHE_material on materials (doc/gltf-scene-roundtrip-plan.md
    // phase 3). Runs after the legacy extras replay so the extensions win
    // when both are present. Editor-domain extensions stay raw in
    // Gltf_data for the editor layer.
    {
        simdjson::dom::parser extension_parser;
        const auto get_extension_object = [&extension_parser](const std::string& json, simdjson::dom::object& out_object) -> bool {
            simdjson::dom::element root;
            if (extension_parser.parse(simdjson::padded_string{json}).get(root) != simdjson::SUCCESS) {
                return false;
            }
            return root.get_object().get(out_object) == simdjson::SUCCESS;
        };
        const auto read_float = [](const simdjson::dom::object& object, const char* key, float& out_value) -> bool {
            double value{0.0};
            if (object.at_key(key).get_double().get(value) != simdjson::SUCCESS) {
                return false;
            }
            out_value = static_cast<float>(value);
            return true;
        };

        for (std::size_t i = 0; i < result.node_extensions.size(); ++i) {
            const std::shared_ptr<erhe::scene::Node>& node = result.nodes[i];
            if (!node) {
                continue;
            }
            for (const auto& [extension_name, extension_json] : result.node_extensions[i].entries) {
                simdjson::dom::object extension_object;
                if (!get_extension_object(extension_json, extension_object)) {
                    continue;
                }
                if (extension_name == "ERHE_node") {
                    simdjson::dom::array flags_array;
                    if (extension_object.at_key("flags").get_array().get(flags_array) == simdjson::SUCCESS) {
                        apply_persistent_flags(*node, flags_array);
                    }
                    if (extension_object.at_key("mesh_flags").get_array().get(flags_array) == simdjson::SUCCESS) {
                        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
                        if (mesh) {
                            apply_persistent_flags(*mesh, flags_array);
                        }
                    }
                } else if (extension_name == "ERHE_light") {
                    const std::shared_ptr<erhe::scene::Light> light = erhe::scene::get_attachment<erhe::scene::Light>(node.get());
                    if (light) {
                        bool bool_value{false};
                        if (extension_object.at_key("cast_shadow").get_bool().get(bool_value) == simdjson::SUCCESS) {
                            light->cast_shadow = bool_value;
                        }
                        if ((extension_object.at_key("infinite_range").get_bool().get(bool_value) == simdjson::SUCCESS) && bool_value) {
                            light->range = 0.0f;
                        }
                        simdjson::dom::array flags_array;
                        if (extension_object.at_key("flags").get_array().get(flags_array) == simdjson::SUCCESS) {
                            apply_persistent_flags(*light, flags_array);
                        }
                    }
                }
            }
        }

        for (std::size_t i = 0; i < result.camera_extensions.size(); ++i) {
            const std::shared_ptr<erhe::scene::Camera>& camera = result.cameras[i];
            if (!camera) {
                continue;
            }
            for (const auto& [extension_name, extension_json] : result.camera_extensions[i].entries) {
                if (extension_name != "ERHE_camera") {
                    continue;
                }
                simdjson::dom::object extension_object;
                if (!get_extension_object(extension_json, extension_object)) {
                    continue;
                }
                erhe::scene::Projection* projection = camera->projection();
                if (projection != nullptr) {
                    std::string_view type_name;
                    if (extension_object.at_key("projection_type").get_string().get(type_name) == simdjson::SUCCESS) {
                        projection->projection_type = projection_type_from_name(type_name);
                    }
                    static_cast<void>(read_float(extension_object, "z_near",         projection->z_near));
                    static_cast<void>(read_float(extension_object, "z_far",          projection->z_far));
                    static_cast<void>(read_float(extension_object, "fov_x",          projection->fov_x));
                    static_cast<void>(read_float(extension_object, "fov_y",          projection->fov_y));
                    static_cast<void>(read_float(extension_object, "fov_left",       projection->fov_left));
                    static_cast<void>(read_float(extension_object, "fov_right",      projection->fov_right));
                    static_cast<void>(read_float(extension_object, "fov_up",         projection->fov_up));
                    static_cast<void>(read_float(extension_object, "fov_down",       projection->fov_down));
                    static_cast<void>(read_float(extension_object, "ortho_left",     projection->ortho_left));
                    static_cast<void>(read_float(extension_object, "ortho_width",    projection->ortho_width));
                    static_cast<void>(read_float(extension_object, "ortho_bottom",   projection->ortho_bottom));
                    static_cast<void>(read_float(extension_object, "ortho_height",   projection->ortho_height));
                    static_cast<void>(read_float(extension_object, "frustum_left",   projection->frustum_left));
                    static_cast<void>(read_float(extension_object, "frustum_right",  projection->frustum_right));
                    static_cast<void>(read_float(extension_object, "frustum_bottom", projection->frustum_bottom));
                    static_cast<void>(read_float(extension_object, "frustum_top",    projection->frustum_top));
                }
                float float_value{0.0f};
                if (read_float(extension_object, "exposure", float_value)) {
                    camera->set_exposure(float_value);
                }
                if (read_float(extension_object, "shadow_range", float_value)) {
                    camera->set_shadow_range(float_value);
                }
                simdjson::dom::array flags_array;
                if (extension_object.at_key("flags").get_array().get(flags_array) == simdjson::SUCCESS) {
                    apply_persistent_flags(*camera, flags_array);
                }
            }
        }

        for (std::size_t i = 0; i < result.material_extensions.size(); ++i) {
            const std::shared_ptr<erhe::primitive::Material>& material = result.materials[i];
            if (!material) {
                continue;
            }
            for (const auto& [extension_name, extension_json] : result.material_extensions[i].entries) {
                if (extension_name != "ERHE_material") {
                    continue;
                }
                simdjson::dom::object extension_object;
                if (!get_extension_object(extension_json, extension_object)) {
                    continue;
                }
                float float_value{0.0f};
                if (read_float(extension_object, "roughness_y", float_value)) {
                    material->data.roughness.y = float_value;
                }
                std::string_view string_value;
                if (extension_object.at_key("bxdf_model").get_string().get(string_value) == simdjson::SUCCESS) {
                    if (const auto parsed = bxdf_model_from_string(string_value); parsed.has_value()) {
                        material->data.bxdf_model = parsed.value();
                    }
                }
                if (extension_object.at_key("blending_mode").get_string().get(string_value) == simdjson::SUCCESS) {
                    if (const auto parsed = blending_mode_from_string(string_value); parsed.has_value()) {
                        material->data.blending_mode = parsed.value();
                    }
                }
                bool bool_value{false};
                if (extension_object.at_key("use_circular_brushed_metal").get_bool().get(bool_value) == simdjson::SUCCESS) {
                    material->data.use_circular_brushed_metal = bool_value;
                }
                std::uint64_t uint_value{0};
                if (extension_object.at_key("circular_brushed_metal_tex_coord").get_uint64().get(uint_value) == simdjson::SUCCESS) {
                    material->data.circular_brushed_metal_tex_coord = static_cast<uint32_t>(uint_value);
                }
                if (extension_object.at_key("use_aniso_control").get_bool().get(bool_value) == simdjson::SUCCESS) {
                    material->data.use_aniso_control = bool_value;
                }
            }
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
    result.image_uids.resize(asset->images.size());
    for (std::size_t i = 0, end = asset->images.size(); i < end; ++i) {
        result.images[i]     = resource_name(asset->images[i].name, "image", i);
        result.image_uids[i] = std::string{asset->images[i].uid};
    }

    result.samplers.resize(asset->samplers.size());
    result.sampler_uids.resize(asset->samplers.size());
    for (std::size_t i = 0, end = asset->samplers.size(); i < end; ++i) {
        result.samplers[i]     = resource_name(asset->samplers[i].name, "sampler", i);
        result.sampler_uids[i] = std::string{asset->samplers[i].uid};
    }

    result.materials.resize(asset->materials.size());
    result.material_uids.resize(asset->materials.size());
    for (std::size_t i = 0, end = asset->materials.size(); i < end; ++i) {
        result.materials[i]     = resource_name(asset->materials[i].name, "material", i);
        result.material_uids[i] = std::string{asset->materials[i].uid};
    }

    result.cameras.resize(asset->cameras.size());
    result.camera_uids.resize(asset->cameras.size());
    for (std::size_t i = 0, end = asset->cameras.size(); i < end; ++i) {
        result.cameras[i]     = resource_name(asset->cameras[i].name, "camera", i);
        result.camera_uids[i] = std::string{asset->cameras[i].uid};
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
    result.mesh_uids.resize(asset->meshes.size());
    for (std::size_t i = 0, end = asset->meshes.size(); i < end; ++i) {
        result.meshes[i]    = resource_name(asset->meshes[i].name, "mesh", i);
        result.mesh_uids[i] = std::string{asset->meshes[i].uid};
    }

    result.nodes.resize(asset->nodes.size());
    result.node_uids.resize(asset->nodes.size());
    for (std::size_t i = 0, end = asset->nodes.size(); i < end; ++i) {
        result.nodes[i]     = resource_name(asset->nodes[i].name, "node", i);
        result.node_uids[i] = std::string{asset->nodes[i].uid};
    }

    result.skins.resize(asset->skins.size());
    result.skin_uids.resize(asset->skins.size());
    for (std::size_t i = 0, end = asset->skins.size(); i < end; ++i) {
        result.skins[i]     = resource_name(asset->skins[i].name, "skin", i);
        result.skin_uids[i] = std::string{asset->skins[i].uid};
    }

    result.animations.resize(asset->animations.size());
    result.animation_uids.resize(asset->animations.size());
    for (std::size_t i = 0, end = asset->animations.size(); i < end; ++i) {
        result.animations[i]     = resource_name(asset->animations[i].name, "animation", i);
        result.animation_uids[i] = std::string{asset->animations[i].uid};
    }

    result.scenes.resize(asset->scenes.size());
    result.scene_uids.resize(asset->scenes.size());
    for (std::size_t i = 0, end = asset->scenes.size(); i < end; ++i) {
        result.scenes[i]     = resource_name(asset->scenes[i].name, "scene", i);
        result.scene_uids[i] = std::string{asset->scenes[i].uid};
    }

    result.files.resize(asset->files.size());
    result.file_uids.resize(asset->files.size());
    for (std::size_t i = 0, end = asset->files.size(); i < end; ++i) {
        result.files[i]     = resource_name(asset->files[i].name, "file", i);
        result.file_uids[i] = std::string{asset->files[i].uid};
    }

    result.external_assets.resize(asset->externalAssets.size());
    result.external_asset_uids.resize(asset->externalAssets.size());
    for (std::size_t i = 0, end = asset->externalAssets.size(); i < end; ++i) {
        result.external_assets[i]     = resource_name(asset->externalAssets[i].name, "externalAsset", i);
        result.external_asset_uids[i] = std::string{asset->externalAssets[i].uid};
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
        // ERHE_geometry extension members for geometry-normative primitives
        // (empty for triangle-soup primitives).
        std::string                      erhe_geometry_extension;
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

    // Appends one tightly packed uint32 scalar accessor (own buffer + view).
    [[nodiscard]] auto add_u32_accessor(const std::span<const uint32_t> values, const std::string_view name) -> std::size_t
    {
        const std::size_t byte_length  = values.size_bytes();
        const std::size_t buffer_index = m_gltf_asset.buffers.size();
        {
            const std::byte* bytes = reinterpret_cast<const std::byte*>(values.data());
            fastgltf::Buffer buffer{
                .byteLength = byte_length,
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{bytes, bytes + byte_length},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(buffer));
        }
        const std::size_t buffer_view_index = m_gltf_asset.bufferViews.size();
        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = buffer_index;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = byte_length;
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }
        fastgltf::Accessor accessor{
            .byteOffset      = 0,
            .count           = values.size(),
            .type            = fastgltf::AccessorType::Scalar,
            .componentType   = fastgltf::ComponentType::UnsignedInt,
            .normalized      = false,
            .max             = {},
            .min             = {},
            .bufferViewIndex = buffer_view_index,
            .sparse          = {},
            .name            = FASTGLTF_STD_PMR_NS::string{name}
        };
        const std::size_t accessor_index = m_gltf_asset.accessors.size();
        m_gltf_asset.accessors.emplace_back(std::move(accessor));
        return accessor_index;
    }

    // Appends a raw buffer view (no accessor) holding a geogram attribute
    // store dump for the ERHE_geometry extension.
    [[nodiscard]] auto add_raw_buffer_view(const std::span<const std::byte> bytes) -> std::size_t
    {
        const std::size_t buffer_index = m_gltf_asset.buffers.size();
        {
            fastgltf::Buffer buffer{
                .byteLength = bytes.size(),
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{bytes.begin(), bytes.end()},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(buffer));
        }
        const std::size_t buffer_view_index = m_gltf_asset.bufferViews.size();
        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = buffer_index;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = bytes.size();
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }
        return buffer_view_index;
    }

    [[nodiscard]] static auto json_escape(const std::string& in) -> std::string
    {
        std::string out;
        out.reserve(in.size());
        for (const char c : in) {
            if ((c == '"') || (c == '\\')) {
                out += '\\';
            }
            out += c;
        }
        return out;
    }

    // Geometry-normative primitive export (doc/gltf-scene-roundtrip-plan.md
    // phase 2): POSITION over geogram vertices (glTF vertex i == geogram
    // vertex i), facet-fan TRIANGLES for stock viewers, and the
    // ERHE_geometry extension carrying polygon rings (accessors) plus the
    // raw geogram attribute dump (buffer views) for a bit-exact reload.
    // Vertex-element attributes that map to standard glTF semantics are
    // dual-listed as core attributes over the same bytes.
    std::unordered_map<erhe::geometry::Geometry*, Export_entry> m_erhe_geometry_entries;
    [[nodiscard]] auto process_geometry(erhe::geometry::Geometry* geometry) -> Export_entry
    {
        ERHE_VERIFY(geometry != nullptr);

        const auto fi = m_erhe_geometry_entries.find(geometry);
        if (fi != m_erhe_geometry_entries.end()) {
            return fi->second;
        }

        const erhe::geometry::Geometry_flat_data flat = erhe::geometry::geometry_to_flat_data(*geometry);
        ERHE_VERIFY(flat.vertex_count > 0);

        Export_entry entry{};
        {
            const std::byte* index_bytes = reinterpret_cast<const std::byte*>(flat.triangle_indices.data());
            add_index_data_source(
                entry,
                flat.vertex_count,
                flat.triangle_indices.size(),
                std::span<const std::byte>{index_bytes, flat.triangle_indices.size() * sizeof(uint32_t)}
            );
        }
        const std::size_t position_accessor = add_float_accessor(
            std::span<const float>{flat.positions.data(), flat.positions.size()},
            fastgltf::AccessorType::Vec3,
            flat.vertex_count,
            true, // POSITION requires min/max
            "POSITION"
        );
        entry.attributes.emplace_back(FASTGLTF_STD_PMR_NS::string{"POSITION"}, position_accessor);

        const std::size_t facet_vertex_counts_accessor  = add_u32_accessor(flat.facet_vertex_counts,  "facet_vertex_counts");
        const std::size_t facet_vertex_indices_accessor = add_u32_accessor(flat.facet_vertex_indices, "facet_vertex_indices");
        std::optional<std::size_t> edge_vertices_accessor{};
        if (!flat.edge_vertex_indices.empty()) {
            edge_vertices_accessor = add_u32_accessor(flat.edge_vertex_indices, "edge_vertices");
        }

        std::string json = fmt::format(
            "\"ERHE_geometry\":{{\"vertex_count\":{},\"facet_count\":{},\"corner_count\":{},\"edge_count\":{}"
            ",\"facet_vertex_counts\":{},\"facet_vertex_indices\":{}",
            flat.vertex_count, flat.facet_count, flat.corner_count, flat.edge_count,
            facet_vertex_counts_accessor, facet_vertex_indices_accessor
        );
        if (edge_vertices_accessor.has_value()) {
            json += fmt::format(",\"edge_vertices\":{}", edge_vertices_accessor.value());
        }
        json += ",\"attributes\":[";

        // The flat dump contains every ALLOCATED geogram attribute; the
        // parallel "present_<name>" bool records tell which elements hold a
        // value (unset elements are zero bytes). Dual-listing may only use
        // fully-present vertex attributes: glTF requires e.g. unit-length
        // NORMAL for every element of the accessor.
        const auto vertex_attribute_fully_present = [&flat](const std::string& attribute_name) -> bool {
            const std::string present_name = "present_" + attribute_name;
            for (const erhe::geometry::Geometry_attribute_record& present_record : flat.attributes) {
                if ((present_record.element == "vertex") && (present_record.name == present_name)) {
                    if (present_record.bytes.empty()) {
                        return false;
                    }
                    for (const std::byte value : present_record.bytes) {
                        if (value == std::byte{0}) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            return false;
        };

        bool dual_listed_normal = false;
        const char* separator = "";
        for (const erhe::geometry::Geometry_attribute_record& record : flat.attributes) {
            const std::byte* record_bytes = record.bytes.data();
            const std::size_t buffer_view_index = add_raw_buffer_view(
                std::span<const std::byte>{record_bytes, record.bytes.size()}
            );
            json += fmt::format(
                "{}{{\"name\":\"{}\",\"element\":\"{}\",\"element_type\":\"{}\",\"element_size\":{},\"dimension\":{},\"item_count\":{},\"buffer_view\":{}}}",
                separator,
                json_escape(record.name), json_escape(record.element), json_escape(record.element_type),
                record.element_size, record.dimension, record.item_count, buffer_view_index
            );
            separator = ",";

            // Dual-list smooth vertex normals as core NORMAL (same bytes,
            // one extra accessor) so stock viewers get smooth shading;
            // prefer authored "normal" over computed "normal_smooth".
            const bool is_vertex_vec3f =
                (record.element == "vertex") &&
                (record.element_type == "vec3f") &&
                (record.item_count == flat.vertex_count) &&
                ((record.element_size * record.dimension) == (3 * sizeof(float)));
            if (
                is_vertex_vec3f &&
                ((record.name == "normal") || (!dual_listed_normal && (record.name == "normal_smooth"))) &&
                vertex_attribute_fully_present(record.name)
            ) {
                fastgltf::Accessor normal_accessor{
                    .byteOffset      = 0,
                    .count           = flat.vertex_count,
                    .type            = fastgltf::AccessorType::Vec3,
                    .componentType   = fastgltf::ComponentType::Float,
                    .normalized      = false,
                    .max             = {},
                    .min             = {},
                    .bufferViewIndex = buffer_view_index,
                    .sparse          = {},
                    .name            = "NORMAL"
                };
                const std::size_t normal_accessor_index = m_gltf_asset.accessors.size();
                m_gltf_asset.accessors.emplace_back(std::move(normal_accessor));
                if (record.name == "normal") {
                    // Authored normals win: replace a previously dual-listed
                    // normal_smooth entry.
                    for (auto& attribute : entry.attributes) {
                        if (std::string_view{attribute.name} == "NORMAL") {
                            attribute.accessorIndex = normal_accessor_index;
                            dual_listed_normal = true;
                            break;
                        }
                    }
                }
                if (!dual_listed_normal) {
                    entry.attributes.emplace_back(FASTGLTF_STD_PMR_NS::string{"NORMAL"}, normal_accessor_index);
                    dual_listed_normal = true;
                }
            }
        }
        json += "]}";
        entry.erhe_geometry_extension = std::move(json);

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
            // erhe-specific and round-trip via the ERHE_material extension
            // (see record_material_extensions below).
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

            // Texture references (phase 0): each slot embeds its retained
            // source image stream; slots whose texture has no exportable
            // source (e.g. graph-texture bakes) stay empty.
            {
                const erhe::primitive::Material_texture_samplers& slots = data.texture_samplers;
                fastgltf::TextureInfo base_color_texture{};
                if (fill_texture_info(slots.base_color, base_color_texture)) {
                    gltf_material.pbrData.baseColorTexture = std::move(base_color_texture);
                }
                fastgltf::TextureInfo metallic_roughness_texture{};
                if (fill_texture_info(slots.metallic_roughness, metallic_roughness_texture)) {
                    gltf_material.pbrData.metallicRoughnessTexture = std::move(metallic_roughness_texture);
                }
                fastgltf::NormalTextureInfo normal_texture{};
                if (fill_texture_info(slots.normal, normal_texture)) {
                    normal_texture.scale = data.normal_texture_scale;
                    gltf_material.normalTexture = std::move(normal_texture);
                }
                fastgltf::OcclusionTextureInfo occlusion_texture{};
                if (fill_texture_info(slots.occlusion, occlusion_texture)) {
                    occlusion_texture.strength = data.occlusion_texture_strength;
                    gltf_material.occlusionTexture = std::move(occlusion_texture);
                }
                fastgltf::TextureInfo emissive_texture{};
                if (fill_texture_info(slots.emissive, emissive_texture)) {
                    gltf_material.emissiveTexture = std::move(emissive_texture);
                }
            }
            m_gltf_asset.materials.push_back(std::move(gltf_material));
        }
        record_material_extensions(*material, gltf_material_index);
        m_exported_materials.insert({material, gltf_material_index});
        return gltf_material_index;
    }

    // ERHE_material extension per emitted glTF material
    // (doc/gltf-scene-roundtrip-plan.md phase 3): the erhe-specific
    // Material_data fields that have no standard glTF representation.
    // Migrates the legacy material extras writer (same conditional field
    // set); the extras remain parsed for older files.
    std::unordered_map<std::size_t, std::string> m_internal_material_extensions;
    void record_material_extensions(const erhe::primitive::Material& material, const std::size_t gltf_material_index)
    {
        const erhe::primitive::Material_data& data = material.data;

        // Skip fields at their round-trip defaults; unlit rides on
        // KHR_materials_unlit and OPAQUE/BLEND/MASK on alphaMode.
        const bool emit_bxdf_model =
            (data.bxdf_model != erhe::primitive::Bxdf_model::isotropic_brdf) &&
            (data.bxdf_model != erhe::primitive::Bxdf_model::unlit);
        const bool emit_roughness_y = (data.roughness.x != data.roughness.y);
        const bool emit_blending_mode =
            (data.blending_mode != erhe::primitive::Material_blending_mode::opaque) &&
            (data.blending_mode != erhe::primitive::Material_blending_mode::alpha_blend) &&
            (data.blending_mode != erhe::primitive::Material_blending_mode::alpha_test);
        const bool emit_circular_brushed_metal_tex_coord = (data.circular_brushed_metal_tex_coord != 1u);

        if (!emit_roughness_y &&
            !emit_bxdf_model &&
            !emit_blending_mode &&
            !data.use_circular_brushed_metal &&
            !emit_circular_brushed_metal_tex_coord &&
            !data.use_aniso_control)
        {
            return;
        }

        std::string fields;
        const char* separator = "";
        if (emit_roughness_y) {
            fields += fmt::format("{}\"roughness_y\":{}", separator, data.roughness.y);
            separator = ",";
        }
        if (emit_bxdf_model) {
            fields += fmt::format("{}\"bxdf_model\":\"{}\"", separator, bxdf_model_to_c_str(data.bxdf_model));
            separator = ",";
        }
        if (emit_blending_mode) {
            fields += fmt::format("{}\"blending_mode\":\"{}\"", separator, blending_mode_to_c_str(data.blending_mode));
            separator = ",";
        }
        if (data.use_circular_brushed_metal) {
            fields += fmt::format("{}\"use_circular_brushed_metal\":true", separator);
            separator = ",";
        }
        if (emit_circular_brushed_metal_tex_coord) {
            fields += fmt::format("{}\"circular_brushed_metal_tex_coord\":{}", separator, data.circular_brushed_metal_tex_coord);
            separator = ",";
        }
        if (data.use_aniso_control) {
            fields += fmt::format("{}\"use_aniso_control\":true", separator);
            separator = ",";
        }
        m_internal_material_extensions.emplace(gltf_material_index, fmt::format("\"ERHE_material\":{{{}}}", fields));
    }

    std::unordered_map<const erhe::scene::Mesh*, std::size_t> m_erhe_mesh_to_gltf_mesh_index;
    // erhe primitive index -> exported glTF primitive index (nullopt when
    // the primitive was skipped); used to place per-primitive extension
    // payloads.
    std::unordered_map<const erhe::scene::Mesh*, std::vector<std::optional<std::size_t>>> m_erhe_mesh_primitive_index_map;
    [[nodiscard]] auto process_mesh(const erhe::scene::Mesh* erhe_mesh) -> std::size_t
    {
        ERHE_VERIFY(erhe_mesh != nullptr);

        // Check if mesh has already been exported
        const auto fi = m_erhe_mesh_to_gltf_mesh_index.find(erhe_mesh);
        if (fi != m_erhe_mesh_to_gltf_mesh_index.end()) {
            return fi->second;
        }

        fastgltf::Mesh gltf_mesh{};
        gltf_mesh.name = FASTGLTF_STD_PMR_NS::string{erhe_mesh->get_name()};
        const std::vector<erhe::scene::Mesh_primitive>& erhe_primitives = erhe_mesh->get_primitives();
        std::vector<std::optional<std::size_t>>& primitive_index_map = m_erhe_mesh_primitive_index_map[erhe_mesh];
        primitive_index_map.resize(erhe_primitives.size());
        std::vector<std::pair<std::size_t, std::string>> pending_geometry_extensions; // (gltf primitive index, extension members)
        for (std::size_t primitive_index = 0; primitive_index < erhe_primitives.size(); ++primitive_index) {
            const erhe::scene::Mesh_primitive& erhe_mesh_primitive = erhe_primitives[primitive_index];
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
            // The triangle soup, when present, is the primitive's source of
            // truth: soups exist only on imported render primitives (a
            // primitive restored from ERHE_geometry parses to geometry
            // alone), and a geometry alongside a soup is a derived artifact
            // (make_geometry), re-derivable after reload. Exporting the soup
            // keeps the full imported vertex attributes (TEXCOORD_n,
            // JOINTS_n / WEIGHTS_n, COLOR_n) that the welded geometry only
            // holds per-corner. Authored geometry (no soup) exports
            // geometry-normative with the ERHE_geometry extension.
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
            if (!export_entry.erhe_geometry_extension.empty()) {
                pending_geometry_extensions.emplace_back(gltf_mesh.primitives.size(), export_entry.erhe_geometry_extension);
            }
            primitive_index_map[primitive_index] = gltf_mesh.primitives.size();
            gltf_mesh.primitives.emplace_back(std::move(gltf_primitive));
        }
        std::size_t gltf_mesh_index = m_gltf_asset.meshes.size();
        m_gltf_asset.meshes.emplace_back(std::move(gltf_mesh));
        m_erhe_mesh_to_gltf_mesh_index.insert({erhe_mesh, gltf_mesh_index});
        for (auto& [gltf_primitive_index, extension_members] : pending_geometry_extensions) {
            m_geometry_primitive_extensions[{gltf_mesh_index, gltf_primitive_index}] = std::move(extension_members);
        }
        return gltf_mesh_index;
    }

    // ERHE_geometry extension members per exported (mesh, primitive), merged
    // into the extensions write callback context in export_gltf().
    std::map<std::pair<std::size_t, std::size_t>, std::string> m_geometry_primitive_extensions;

    // Extra unreferenced meshes (doc/gltf-scene-roundtrip-plan.md phase 3):
    // one glTF mesh with one geometry-normative primitive each, exported
    // through the ERHE_geometry path. Used for brush geometry; the caller's
    // asset_extensions_builder resolves the resulting mesh indices via
    // Gltf_export_index_lookup::extra_mesh_indices.
    std::vector<std::optional<std::size_t>> m_extra_mesh_indices;
    void process_extra_meshes()
    {
        m_extra_mesh_indices.resize(m_arguments.extra_meshes.size());
        for (std::size_t i = 0, end = m_arguments.extra_meshes.size(); i < end; ++i) {
            const Gltf_export_extra_mesh& extra_mesh = m_arguments.extra_meshes[i];
            if (!extra_mesh.geometry) {
                log_gltf->warn("glTF export: extra mesh '{}' has no geometry - skipped", extra_mesh.name);
                continue;
            }
            Export_entry export_entry = process_geometry(extra_mesh.geometry.get());
            fastgltf::Mesh gltf_mesh{};
            gltf_mesh.name = extra_mesh.name;
            fastgltf::Primitive gltf_primitive;
            for (const fastgltf::Attribute& attribute : export_entry.attributes) {
                gltf_primitive.attributes.emplace_back(attribute.name, attribute.accessorIndex);
            }
            gltf_primitive.indicesAccessor = export_entry.index_buffer_accessor;
            if (extra_mesh.material) {
                gltf_primitive.materialIndex = process_material(extra_mesh.material.get());
            }
            gltf_mesh.primitives.emplace_back(std::move(gltf_primitive));
            const std::size_t gltf_mesh_index = m_gltf_asset.meshes.size();
            m_gltf_asset.meshes.emplace_back(std::move(gltf_mesh));
            if (!export_entry.erhe_geometry_extension.empty()) {
                m_geometry_primitive_extensions[{gltf_mesh_index, 0}] = export_entry.erhe_geometry_extension;
            }
            m_extra_mesh_indices[i] = gltf_mesh_index;
        }
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

        // Core glTF cameras are only an interchange approximation of erhe's
        // Projection (yfov/aspect + xmag/ymag cannot express asymmetric
        // frusta or the per-type fov fields); the ERHE_camera extension
        // (doc/gltf-scene-roundtrip-plan.md phase 3) carries full fidelity.
        // glTF requires perspective znear > 0.
        fastgltf::Camera gltf_camera{};
        const float z_near_raw = std::min(erhe_projection->z_far, erhe_projection->z_near);
        const float z_far_raw  = std::max(erhe_projection->z_far, erhe_projection->z_near);
        const float z_near     = std::max(z_near_raw, 0.0001f);
        const auto make_perspective = [&](const float yfov, const std::optional<float> aspect_ratio) {
            fastgltf::Camera::Perspective perspective{
                .aspectRatio = {},
                .yfov  = std::max(yfov, 0.0001f),
                .zfar  = {},
                .znear = z_near
            };
            if (aspect_ratio.has_value() && (aspect_ratio.value() > 0.0f)) {
                perspective.aspectRatio = aspect_ratio.value();
            }
            if (z_far_raw > z_near) {
                perspective.zfar = z_far_raw;
            }
            gltf_camera.camera = perspective;
        };
        switch (erhe_projection->projection_type) {
            case erhe::scene::Projection::Type::perspective_horizontal: {
                // Core glTF has no xfov; best effort without an aspect ratio.
                make_perspective(erhe_projection->fov_x, std::nullopt);
                break;
            }
            case erhe::scene::Projection::Type::perspective_vertical: {
                make_perspective(erhe_projection->fov_y, std::nullopt);
                break;
            }
            case erhe::scene::Projection::Type::perspective: {
                make_perspective(
                    erhe_projection->fov_y,
                    (erhe_projection->fov_y != 0.0f) ? std::optional<float>{erhe_projection->fov_x / erhe_projection->fov_y} : std::nullopt
                );
                break;
            }
            case erhe::scene::Projection::Type::perspective_xr: {
                const float yfov = erhe_projection->fov_up - erhe_projection->fov_down;
                make_perspective(
                    yfov,
                    (yfov != 0.0f) ? std::optional<float>{(erhe_projection->fov_right - erhe_projection->fov_left) / yfov} : std::nullopt
                );
                break;
            }
            case erhe::scene::Projection::Type::orthogonal_horizontal:
            case erhe::scene::Projection::Type::orthogonal_vertical:
            case erhe::scene::Projection::Type::orthogonal:
            case erhe::scene::Projection::Type::orthogonal_rectangle: {
                gltf_camera.camera = fastgltf::Camera::Orthographic{
                    .xmag  = erhe_projection->ortho_width,
                    .ymag  = erhe_projection->ortho_height,
                    .zfar  = z_far_raw,
                    .znear = z_near_raw
                };
                break;
            }
            case erhe::scene::Projection::Type::other:
            case erhe::scene::Projection::Type::generic_frustum:
            default: {
                // No core-glTF equivalent. Write a best-effort perspective
                // approximation instead of aborting the export.
                log_gltf->warn(
                    "glTF export: camera '{}' projection type '{}' has no core glTF equivalent - writing a perspective approximation",
                    erhe_camera->get_name(),
                    erhe::scene::Projection::c_type_strings[static_cast<unsigned int>(erhe_projection->projection_type)]
                );
                make_perspective((erhe_projection->fov_y > 0.0f) ? erhe_projection->fov_y : (glm::pi<float>() / 4.0f), std::nullopt);
                break;
            }
        }
        gltf_camera.name = erhe_camera->get_name();
        std::size_t gltf_camera_index = m_gltf_asset.cameras.size();
        m_gltf_asset.cameras.emplace_back(std::move(gltf_camera));
        m_erhe_camera_to_gltf_camera_index.insert({erhe_camera, gltf_camera_index});
        record_camera_extensions(*erhe_camera, *erhe_projection, gltf_camera_index);
        return gltf_camera_index;
    }

    // ERHE_camera extension per emitted glTF camera
    // (doc/gltf-scene-roundtrip-plan.md phase 3): the FULL
    // erhe::scene::Projection (core glTF cameras carry only yfov/aspect +
    // xmag/ymag and cannot express erhe's projection types or asymmetric
    // frusta), plus exposure, shadow_range and the camera Item flags.
    // Floats format with fmt "{}" (shortest round-trip, value-exact).
    std::unordered_map<std::size_t, std::string> m_internal_camera_extensions;
    void record_camera_extensions(
        const erhe::scene::Camera&     erhe_camera,
        const erhe::scene::Projection& projection,
        const std::size_t              gltf_camera_index
    )
    {
        std::string members = fmt::format(
            "\"ERHE_camera\":{{\"projection_type\":\"{}\""
            ",\"z_near\":{},\"z_far\":{}"
            ",\"fov_x\":{},\"fov_y\":{},\"fov_left\":{},\"fov_right\":{},\"fov_up\":{},\"fov_down\":{}"
            ",\"ortho_left\":{},\"ortho_width\":{},\"ortho_bottom\":{},\"ortho_height\":{}"
            ",\"frustum_left\":{},\"frustum_right\":{},\"frustum_bottom\":{},\"frustum_top\":{}"
            ",\"exposure\":{},\"shadow_range\":{},\"flags\":{}}}",
            projection_type_name(projection.projection_type),
            projection.z_near, projection.z_far,
            projection.fov_x, projection.fov_y,
            projection.fov_left, projection.fov_right, projection.fov_up, projection.fov_down,
            projection.ortho_left, projection.ortho_width, projection.ortho_bottom, projection.ortho_height,
            projection.frustum_left, projection.frustum_right, projection.frustum_bottom, projection.frustum_top,
            erhe_camera.get_exposure(), erhe_camera.get_shadow_range(),
            persistent_item_flags_to_json(erhe_camera.get_flag_bits())
        );
        m_internal_camera_extensions.emplace(gltf_camera_index, std::move(members));
    }

    // Appends one tightly packed float accessor (own buffer + buffer view;
    // combine_buffers() later folds it into buffer 0). with_bounds computes
    // per-component min/max, required by the spec for animation input
    // accessors.
    [[nodiscard]] auto add_float_accessor(
        const std::span<const float> values,
        const fastgltf::AccessorType accessor_type,
        const std::size_t            element_count,
        const bool                   with_bounds,
        const std::string_view       name
    ) -> std::size_t
    {
        const std::size_t byte_length  = values.size_bytes();
        const std::size_t buffer_index = m_gltf_asset.buffers.size();
        {
            const std::byte* bytes = reinterpret_cast<const std::byte*>(values.data());
            fastgltf::Buffer buffer{
                .byteLength = byte_length,
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{bytes, bytes + byte_length},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(buffer));
        }
        const std::size_t buffer_view_index = m_gltf_asset.bufferViews.size();
        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = buffer_index;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = byte_length;
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }

        fastgltf::Accessor accessor{
            .byteOffset      = 0,
            .count           = element_count,
            .type            = accessor_type,
            .componentType   = fastgltf::ComponentType::Float,
            .normalized      = false,
            .max             = {},
            .min             = {},
            .bufferViewIndex = buffer_view_index,
            .sparse          = {},
            .name            = FASTGLTF_STD_PMR_NS::string{name}
        };
        if (with_bounds && (element_count > 0)) {
            const std::size_t component_count = values.size() / element_count;
            fastgltf::AccessorBoundsArray min_value{component_count, fastgltf::AccessorBoundsArray::BoundsType::float64};
            fastgltf::AccessorBoundsArray max_value{component_count, fastgltf::AccessorBoundsArray::BoundsType::float64};
            for (std::size_t c = 0; c < component_count; ++c) {
                min_value.set<double>(c, static_cast<double>(std::numeric_limits<float>::max()));
                max_value.set<double>(c, static_cast<double>(std::numeric_limits<float>::lowest()));
            }
            for (std::size_t i = 0; i < element_count; ++i) {
                for (std::size_t c = 0; c < component_count; ++c) {
                    const double v = static_cast<double>(values[(i * component_count) + c]);
                    min_value.set<double>(c, std::min(min_value.get<double>(c), v));
                    max_value.set<double>(c, std::max(max_value.get<double>(c), v));
                }
            }
            accessor.min = std::move(min_value);
            accessor.max = std::move(max_value);
        }
        const std::size_t accessor_index = m_gltf_asset.accessors.size();
        m_gltf_asset.accessors.emplace_back(std::move(accessor));
        return accessor_index;
    }

    // --- Images / samplers / textures (doc/gltf-scene-roundtrip-plan.md phase 0) ---

    // Deduplicated image export: the retained encoded source stream
    // (Gltf_image_source) is embedded verbatim as a buffer view, so the
    // round-trip is byte-exact. nullopt = the texture has no exportable
    // source (empty provider, GPU-only graph-texture bake, unrecognized
    // stream) - slots referencing it are skipped.
    std::unordered_map<const erhe::graphics::Texture*, std::optional<std::size_t>> m_exported_images;
    [[nodiscard]] auto process_image(const erhe::graphics::Texture* texture) -> std::optional<std::size_t>
    {
        ERHE_VERIFY(texture != nullptr);
        const auto fi = m_exported_images.find(texture);
        if (fi != m_exported_images.end()) {
            return fi->second;
        }

        const std::shared_ptr<const Gltf_image_source> image_source = m_arguments.image_source_provider
            ? m_arguments.image_source_provider(texture)
            : std::shared_ptr<const Gltf_image_source>{};
        if (!image_source || image_source->encoded_bytes.empty()) {
            log_gltf->warn("glTF export: texture '{}' has no retained source image bytes - texture slot skipped", texture->get_name());
            m_exported_images.insert({texture, std::nullopt});
            return std::nullopt;
        }
        const fastgltf::MimeType mime_type = mime_type_from_string(image_source->mime_type);
        if (mime_type == fastgltf::MimeType::None) {
            // mimeType is required when an image uses a buffer view; without
            // it the file would be invalid.
            log_gltf->warn("glTF export: texture '{}' source stream has unrecognized media type - texture slot skipped", texture->get_name());
            m_exported_images.insert({texture, std::nullopt});
            return std::nullopt;
        }

        const std::size_t buffer_index = m_gltf_asset.buffers.size();
        {
            fastgltf::Buffer image_buffer{
                .byteLength = image_source->encoded_bytes.size(),
                .data = fastgltf::sources::Vector{
                    .bytes    = std::vector<std::byte>{image_source->encoded_bytes.begin(), image_source->encoded_bytes.end()},
                    .mimeType = fastgltf::MimeType::GltfBuffer
                },
                .name = {}
            };
            m_gltf_asset.buffers.emplace_back(std::move(image_buffer));
        }
        const std::size_t buffer_view_index = m_gltf_asset.bufferViews.size();
        {
            fastgltf::BufferView buffer_view{};
            buffer_view.bufferIndex = buffer_index;
            buffer_view.byteOffset  = 0;
            buffer_view.byteLength  = image_source->encoded_bytes.size();
            m_gltf_asset.bufferViews.emplace_back(std::move(buffer_view));
        }

        fastgltf::Image gltf_image{};
        gltf_image.data = fastgltf::sources::BufferView{
            .bufferViewIndex = buffer_view_index,
            .mimeType        = mime_type
        };
        gltf_image.name = FASTGLTF_STD_PMR_NS::string{texture->get_name()};
        const std::size_t image_index = m_gltf_asset.images.size();
        m_gltf_asset.images.emplace_back(std::move(gltf_image));
        m_exported_images.insert({texture, image_index});
        return image_index;
    }

    std::unordered_map<const erhe::graphics::Sampler*, std::size_t> m_exported_samplers;
    [[nodiscard]] auto process_sampler(const erhe::graphics::Sampler* sampler) -> std::size_t
    {
        ERHE_VERIFY(sampler != nullptr);
        const auto fi = m_exported_samplers.find(sampler);
        if (fi != m_exported_samplers.end()) {
            return fi->second;
        }
        const erhe::graphics::Sampler_create_info& create_info = sampler->get_create_info();

        fastgltf::Sampler gltf_sampler{};
        const bool min_linear = (create_info.min_filter == erhe::graphics::Filter::linear);
        switch (create_info.mipmap_mode) {
            case erhe::graphics::Sampler_mipmap_mode::not_mipmapped: gltf_sampler.minFilter = min_linear ? fastgltf::Filter::Linear             : fastgltf::Filter::Nearest;             break;
            case erhe::graphics::Sampler_mipmap_mode::nearest:       gltf_sampler.minFilter = min_linear ? fastgltf::Filter::LinearMipMapNearest : fastgltf::Filter::NearestMipMapNearest; break;
            case erhe::graphics::Sampler_mipmap_mode::linear:        gltf_sampler.minFilter = min_linear ? fastgltf::Filter::LinearMipMapLinear  : fastgltf::Filter::NearestMipMapLinear;  break;
            default:                                                 gltf_sampler.minFilter = fastgltf::Filter::Linear; break;
        }
        gltf_sampler.magFilter = (create_info.mag_filter == erhe::graphics::Filter::linear)
            ? fastgltf::Filter::Linear
            : fastgltf::Filter::Nearest;
        const auto to_gltf_wrap = [](const erhe::graphics::Sampler_address_mode mode) -> fastgltf::Wrap {
            switch (mode) {
                case erhe::graphics::Sampler_address_mode::repeat:          return fastgltf::Wrap::Repeat;
                case erhe::graphics::Sampler_address_mode::clamp_to_edge:   return fastgltf::Wrap::ClampToEdge;
                case erhe::graphics::Sampler_address_mode::mirrored_repeat: return fastgltf::Wrap::MirroredRepeat;
                default:                                                    return fastgltf::Wrap::Repeat;
            }
        };
        gltf_sampler.wrapS = to_gltf_wrap(create_info.address_mode[0]);
        gltf_sampler.wrapT = to_gltf_wrap(create_info.address_mode[1]);

        const std::size_t sampler_index = m_gltf_asset.samplers.size();
        m_gltf_asset.samplers.emplace_back(std::move(gltf_sampler));
        m_exported_samplers.insert({sampler, sampler_index});
        return sampler_index;
    }

    std::map<std::pair<std::size_t, std::optional<std::size_t>>, std::size_t> m_exported_textures;
    [[nodiscard]] auto process_texture(const erhe::graphics::Texture* texture, const erhe::graphics::Sampler* sampler) -> std::optional<std::size_t>
    {
        const std::optional<std::size_t> image_index = process_image(texture);
        if (!image_index.has_value()) {
            return std::nullopt;
        }
        std::optional<std::size_t> sampler_index{};
        if (sampler != nullptr) {
            sampler_index = process_sampler(sampler);
        }
        const std::pair<std::size_t, std::optional<std::size_t>> key{image_index.value(), sampler_index};
        const auto fi = m_exported_textures.find(key);
        if (fi != m_exported_textures.end()) {
            return fi->second;
        }
        fastgltf::Texture gltf_texture{};
        gltf_texture.imageIndex = image_index.value();
        if (sampler_index.has_value()) {
            gltf_texture.samplerIndex = sampler_index.value();
        }
        const std::size_t texture_index = m_gltf_asset.textures.size();
        m_gltf_asset.textures.emplace_back(std::move(gltf_texture));
        m_exported_textures.insert({key, texture_index});
        return texture_index;
    }

    // Fills a fastgltf TextureInfo / NormalTextureInfo / OcclusionTextureInfo
    // from an erhe material texture slot; false = slot empty or texture has
    // no exportable source. Writes KHR_texture_transform when the slot's UV
    // transform is non-default.
    bool m_uses_texture_transform{false};
    template <typename Texture_info>
    [[nodiscard]] auto fill_texture_info(const erhe::primitive::Material_texture_sampler& slot, Texture_info& out) -> bool
    {
        if (!slot.texture_reference) {
            return false;
        }
        const erhe::graphics::Texture* texture = slot.texture_reference->get_referenced_texture();
        if (texture == nullptr) {
            return false;
        }
        const std::optional<std::size_t> texture_index = process_texture(texture, slot.sampler.get());
        if (!texture_index.has_value()) {
            return false;
        }
        out.textureIndex  = texture_index.value();
        out.texCoordIndex = slot.tex_coord;
        const bool has_transform =
            (slot.rotation != 0.0f) ||
            (slot.offset != glm::vec2{0.0f, 0.0f}) ||
            (slot.scale != glm::vec2{1.0f, 1.0f});
        if (has_transform) {
            out.transform = std::make_unique<fastgltf::TextureTransform>();
            out.transform->rotation = slot.rotation;
            out.transform->uvOffset = fastgltf::math::nvec2{slot.offset.x, slot.offset.y};
            out.transform->uvScale  = fastgltf::math::nvec2{slot.scale.x, slot.scale.y};
            m_uses_texture_transform = true;
        }
        return true;
    }

    // --- Animations (doc/gltf-scene-roundtrip-plan.md phase 0) ---
    // Animation_sampler storage is already glTF-native (timestamps + flat
    // output floats, CUBICSPLINE interleaved [in_tangent, value, out_tangent],
    // quaternions in glTF [x,y,z,w] order), so export is a near-passthrough.

    [[nodiscard]] static auto from_erhe(const erhe::scene::Animation_path path) -> fastgltf::AnimationPath
    {
        switch (path) {
            case erhe::scene::Animation_path::TRANSLATION: return fastgltf::AnimationPath::Translation;
            case erhe::scene::Animation_path::ROTATION:    return fastgltf::AnimationPath::Rotation;
            case erhe::scene::Animation_path::SCALE:       return fastgltf::AnimationPath::Scale;
            case erhe::scene::Animation_path::WEIGHTS:     return fastgltf::AnimationPath::Weights;
            default:                                       return fastgltf::AnimationPath::Translation;
        }
    }

    [[nodiscard]] static auto from_erhe(const erhe::scene::Animation_interpolation_mode mode) -> fastgltf::AnimationInterpolation
    {
        switch (mode) {
            case erhe::scene::Animation_interpolation_mode::STEP:        return fastgltf::AnimationInterpolation::Step;
            case erhe::scene::Animation_interpolation_mode::LINEAR:      return fastgltf::AnimationInterpolation::Linear;
            case erhe::scene::Animation_interpolation_mode::CUBICSPLINE: return fastgltf::AnimationInterpolation::CubicSpline;
            default:                                                     return fastgltf::AnimationInterpolation::Linear;
        }
    }

    void process_animations()
    {
        for (const std::shared_ptr<erhe::scene::Animation>& animation : m_arguments.animations) {
            if (animation) {
                process_animation(*animation);
            }
        }
    }

    void process_animation(const erhe::scene::Animation& animation)
    {
        fastgltf::Animation gltf_animation{};
        gltf_animation.name = FASTGLTF_STD_PMR_NS::string{animation.get_name()};

        // erhe sampler index -> emitted glTF sampler index, filled lazily:
        // the output accessor type comes from the channel path, and samplers
        // referenced only by skipped channels must not be emitted.
        std::vector<std::optional<std::size_t>> sampler_map(animation.samplers.size());

        for (const erhe::scene::Animation_channel& channel : animation.channels) {
            if (channel.path == erhe::scene::Animation_path::WEIGHTS) {
                // erhe has no morph-target support end-to-end; there is no
                // data behind a weights channel to export.
                log_gltf->warn("glTF export: animation '{}' weights channel skipped (morph targets not supported)", animation.get_name());
                continue;
            }
            const std::size_t component_count = erhe::scene::get_component_count(channel.path);
            if ((component_count == 0) || (channel.sampler_index >= animation.samplers.size())) {
                continue;
            }
            const std::optional<std::size_t> target_node_index = find_gltf_node_index(channel.target);
            if (!target_node_index.has_value()) {
                log_gltf->warn(
                    "glTF export: animation '{}' channel targets node '{}' outside the exported subtree - skipping channel",
                    animation.get_name(),
                    channel.target ? channel.target->get_name() : std::string{"<null>"}
                );
                continue;
            }

            if (!sampler_map[channel.sampler_index].has_value()) {
                const erhe::scene::Animation_sampler& sampler = animation.samplers[channel.sampler_index];
                const bool        cubic       = (sampler.interpolation_mode == erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
                const std::size_t value_count = sampler.timestamps.size() * component_count * (cubic ? 3 : 1);
                if (sampler.timestamps.empty() || (sampler.data.size() < value_count)) {
                    log_gltf->warn(
                        "glTF export: animation '{}' sampler {} has inconsistent keyframe data - skipping channel",
                        animation.get_name(), channel.sampler_index
                    );
                    continue;
                }
                const std::size_t input_accessor = add_float_accessor(
                    std::span<const float>{sampler.timestamps.data(), sampler.timestamps.size()},
                    fastgltf::AccessorType::Scalar,
                    sampler.timestamps.size(),
                    true, // animation input accessors require min/max
                    "animation_input"
                );
                const std::size_t output_accessor = add_float_accessor(
                    std::span<const float>{sampler.data.data(), value_count},
                    (component_count == 4) ? fastgltf::AccessorType::Vec4 : fastgltf::AccessorType::Vec3,
                    sampler.timestamps.size() * (cubic ? 3 : 1),
                    false,
                    "animation_output"
                );
                fastgltf::AnimationSampler gltf_sampler{};
                gltf_sampler.inputAccessor  = input_accessor;
                gltf_sampler.outputAccessor = output_accessor;
                gltf_sampler.interpolation  = from_erhe(sampler.interpolation_mode);
                sampler_map[channel.sampler_index] = gltf_animation.samplers.size();
                gltf_animation.samplers.emplace_back(std::move(gltf_sampler));
            }

            fastgltf::AnimationChannel gltf_channel{};
            gltf_channel.samplerIndex = sampler_map[channel.sampler_index].value();
            gltf_channel.nodeIndex    = target_node_index.value();
            gltf_channel.path         = from_erhe(channel.path);
            gltf_animation.channels.emplace_back(std::move(gltf_channel));
        }

        if (gltf_animation.channels.empty()) {
            log_gltf->warn("glTF export: animation '{}' has no exportable channels - skipped", animation.get_name());
            return;
        }
        m_exported_animations.insert({&animation, m_gltf_asset.animations.size()});
        m_gltf_asset.animations.emplace_back(std::move(gltf_animation));
    }

    std::unordered_map<const erhe::scene::Animation*, std::size_t> m_exported_animations;

    // --- Skins (doc/gltf-scene-roundtrip-plan.md phase 0) ---
    // Recorded during the node pass (a skin's joint nodes may come later in
    // traversal order), emitted after it when every node index is known.
    // Skin_data::skeleton is never populated on import, so the optional glTF
    // skeleton field is omitted.

    std::vector<std::pair<std::size_t, const erhe::scene::Skin*>>       m_pending_node_skins; // gltf node index -> skin
    std::unordered_map<const erhe::scene::Skin*, std::optional<std::size_t>> m_exported_skins;

    void process_skins()
    {
        for (const auto& [node_index, skin] : m_pending_node_skins) {
            const std::optional<std::size_t> skin_index = process_skin(skin);
            if (skin_index.has_value()) {
                m_gltf_asset.nodes[node_index].skinIndex = skin_index.value();
            }
        }
    }

    [[nodiscard]] auto process_skin(const erhe::scene::Skin* skin) -> std::optional<std::size_t>
    {
        ERHE_VERIFY(skin != nullptr);
        const auto fi = m_exported_skins.find(skin);
        if (fi != m_exported_skins.end()) {
            return fi->second;
        }
        const erhe::scene::Skin_data& skin_data = skin->skin_data;

        fastgltf::Skin gltf_skin{};
        gltf_skin.name = FASTGLTF_STD_PMR_NS::string{skin->get_name()};
        for (const std::shared_ptr<erhe::scene::Node>& joint : skin_data.joints) {
            const std::optional<std::size_t> joint_index = find_gltf_node_index(joint);
            if (!joint_index.has_value()) {
                log_gltf->warn(
                    "glTF export: skin '{}' joint '{}' is outside the exported subtree - skipping skin",
                    skin->get_name(),
                    joint ? joint->get_name() : std::string{"<null>"}
                );
                m_exported_skins.insert({skin, std::nullopt});
                return std::nullopt;
            }
            gltf_skin.joints.emplace_back(joint_index.value());
        }
        if (gltf_skin.joints.empty()) {
            log_gltf->warn("glTF export: skin '{}' has no joints - skipping skin", skin->get_name());
            m_exported_skins.insert({skin, std::nullopt});
            return std::nullopt;
        }
        if (!skin_data.inverse_bind_matrices.empty()) {
            if (skin_data.inverse_bind_matrices.size() != skin_data.joints.size()) {
                log_gltf->warn(
                    "glTF export: skin '{}' has {} inverse bind matrices for {} joints - omitting inverseBindMatrices",
                    skin->get_name(), skin_data.inverse_bind_matrices.size(), skin_data.joints.size()
                );
            } else {
                const float* matrix_floats = reinterpret_cast<const float*>(skin_data.inverse_bind_matrices.data());
                gltf_skin.inverseBindMatrices = add_float_accessor(
                    std::span<const float>{matrix_floats, skin_data.inverse_bind_matrices.size() * 16},
                    fastgltf::AccessorType::Mat4,
                    skin_data.inverse_bind_matrices.size(),
                    false,
                    "inverse_bind_matrices"
                );
            }
        }
        const std::size_t skin_index = m_gltf_asset.skins.size();
        m_gltf_asset.skins.emplace_back(std::move(gltf_skin));
        m_exported_skins.insert({skin, skin_index});
        return skin_index;
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

    // ERHE_node (+ ERHE_light for light-carrying nodes) extension members
    // per emitted glTF node (doc/gltf-scene-roundtrip-plan.md phase 3):
    // persistent Item flags by name, the mesh attachment's Item flags (core
    // meshes have no erhe payload of their own, and erhe Mesh attachments
    // are per node), and the light attachment's erhe-only state (per-light
    // hooks inside KHR_lights_punctual would need extra fork surface; erhe
    // lights are 1:1 with their node). Replaces the legacy erhe_flags node
    // extras writer; the extras are still parsed for older files.
    std::unordered_map<std::size_t, std::string> m_internal_node_extensions;
    void record_node_extensions(
        const erhe::scene::Node&                   erhe_node,
        const std::size_t                          gltf_node_index,
        const std::shared_ptr<erhe::scene::Mesh>&  erhe_mesh,
        const std::shared_ptr<erhe::scene::Light>& erhe_light
    )
    {
        std::string members = fmt::format(
            "\"ERHE_node\":{{\"flags\":{}",
            persistent_item_flags_to_json(erhe_node.get_flag_bits())
        );
        if (erhe_mesh) {
            members += fmt::format(",\"mesh_flags\":{}", persistent_item_flags_to_json(erhe_mesh->get_flag_bits()));
        }
        members += "}";
        if (erhe_light) {
            members += fmt::format(
                ",\"ERHE_light\":{{\"cast_shadow\":{},\"infinite_range\":{},\"flags\":{}}}",
                erhe_light->cast_shadow ? "true" : "false",
                (erhe_light->range <= 0.0f) ? "true" : "false",
                persistent_item_flags_to_json(erhe_light->get_flag_bits())
            );
        }
        m_internal_node_extensions.emplace(gltf_node_index, std::move(members));
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

    // Append the gltf node indices for erhe_node's child nodes to
    // out_node_indices. Children marked Item_flags::import_root are
    // implicit containers created when a glTF file was opened/imported --
    // not file content -- so their children are emitted in their place,
    // with the container's transform composed in; import re-creates the
    // container. Without this every open/save cycle would nest the content
    // in one more wrapper node.
    template <typename Index_vector>
    void process_child_nodes(const erhe::scene::Node& erhe_node, const erhe::scene::Trs_transform& pre_transform, Index_vector& out_node_indices)
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : erhe_node.get_children()) {
            const erhe::scene::Node* erhe_child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
            if (erhe_child_node == nullptr) {
                continue;
            }
            if ((erhe_child_node->get_flag_bits() & erhe::Item_flags::import_root) != 0) {
                process_child_nodes(*erhe_child_node, pre_transform * erhe_child_node->parent_from_node_transform(), out_node_indices);
                continue;
            }
            out_node_indices.push_back(process_node(*erhe_child_node, pre_transform));
        }
    }

    auto process_node(const erhe::scene::Node& erhe_node, const erhe::scene::Trs_transform& pre_transform = {}) -> std::size_t
    {
        fastgltf::Node gltf_node{};

        gltf_node.name = erhe_node.get_name();
        gltf_node.transform = from_erhe(pre_transform * erhe_node.parent_from_node_transform());

        // glTF 2.1: a prefab-instance node is written as an externalAsset
        // reference. Children and attachments are not exported - the
        // instantiated content comes from the referenced file.
        const auto external_asset_it = m_arguments.external_assets.find(&erhe_node);
        if (external_asset_it != m_arguments.external_assets.end()) {
            gltf_node.externalAssetIndex = get_external_asset_index(external_asset_it->second);
            size_t gltf_external_node_index = m_gltf_asset.nodes.size();
            m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
            m_erhe_node_to_gltf_node_index.insert({&erhe_node, gltf_external_node_index});
            record_node_extensions(erhe_node, gltf_external_node_index, {}, {});
            return gltf_external_node_index;
        }

        // Exclusion hook (doc/gltf-scene-roundtrip-plan.md phase 3): an
        // excluded mesh is a baked artifact rebuilt on load (graph-mesh
        // controlled) - the node exports without it (no glTF mesh, no
        // mesh_flags, no skin).
        std::shared_ptr<erhe::scene::Mesh> erhe_mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(&erhe_node);
        if (erhe_mesh && m_arguments.excluded_meshes.contains(erhe_mesh.get())) {
            erhe_mesh.reset();
        }
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
        // per ancestor level. import_root children are unwrapped.
        process_child_nodes(erhe_node, erhe::scene::Trs_transform{}, gltf_node.children);

        size_t gltf_node_index = m_gltf_asset.nodes.size();
        m_gltf_asset.nodes.emplace_back(std::move(gltf_node));
        m_erhe_node_to_gltf_node_index.insert({&erhe_node, gltf_node_index});
        record_node_extensions(erhe_node, gltf_node_index, erhe_mesh, erhe_light);
        if (erhe_mesh && erhe_mesh->skin) {
            // node.skinIndex is resolved by process_skins() after the node
            // pass - joint nodes may come later in traversal order.
            m_pending_node_skins.emplace_back(gltf_node_index, erhe_mesh->skin.get());
        }
        return gltf_node_index;
    }

    static auto get_gltf_component_size(fastgltf::ComponentType componentType) -> std::size_t
    {
        switch (componentType) {
            case fastgltf::ComponentType::Byte         : return 1;
            case fastgltf::ComponentType::UnsignedByte : return 1;
            case fastgltf::ComponentType::Short        : return 2;
            case fastgltf::ComponentType::UnsignedShort: return 2;
            case fastgltf::ComponentType::Int          : return 4;
            case fastgltf::ComponentType::UnsignedInt  : return 4;
            case fastgltf::ComponentType::Float        : return 4;
            case fastgltf::ComponentType::Double       : return 8;
            default:                                     ERHE_FATAL("Bad component type"); return 0;
        }
    }

    static auto get_gltf_component_count(fastgltf::AccessorType accessorType) -> std::size_t
    {
        switch (accessorType) {
            case fastgltf::AccessorType::Scalar: return 1;
            case fastgltf::AccessorType::Vec2  : return 2;
            case fastgltf::AccessorType::Vec3  : return 3;
            case fastgltf::AccessorType::Vec4  : return 4;
            case fastgltf::AccessorType::Mat2  : return 4;
            case fastgltf::AccessorType::Mat3  : return 9;
            case fastgltf::AccessorType::Mat4  : return 16;
            default:                             ERHE_FATAL("Bad accessor type"); return 0;
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
            // Tightly packed when the view declares no stride (index,
            // animation and inverse-bind-matrix accessors).
            const std::size_t element_size = get_gltf_component_count(accessor.type) * get_gltf_component_size(accessor.componentType);
            const std::size_t stride = buffer_view.byteStride.has_value()
                ? buffer_view.byteStride.value()
                : element_size;
            std::size_t start = buffer_view.byteOffset + accessor.byteOffset;
            std::size_t end = start + ((accessor.count > 0) ? ((accessor.count - 1) * stride + element_size) : 0);
            ERHE_VERIFY(start < buffer.byteLength);
            ERHE_VERIFY(end <= buffer.byteLength);
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

    // glTF 2.1 unique IDs (KhronosGroup/glTF#2597): stamp a uid on every
    // exported top-level object that is backed by an erhe item. An item's
    // existing uid is preserved verbatim; an absent one is generated exactly
    // once and stored back on the item, so re-saving a file never changes
    // uids (external references depend on that stability). uids and names
    // form a single identifier namespace within the file - a carried uid
    // that collides with another object's uid or name (possible e.g. after
    // importing the same source file twice) is regenerated with a warning,
    // because the writer's ValidateAsset pass rejects such files outright.
    // Objects without a backing item (accessors, buffers, buffer views,
    // samplers, synthesized texture entries, extra meshes, the scene) are
    // not stamped: a generated uid could not be stored anywhere and would
    // churn on every save.
    void stamp_uids()
    {
        // Every object name in every top-level array: a uid must not share
        // a value with any of them. A uid may equal its OWN object's name,
        // hence per-name counts rather than a set.
        std::unordered_map<std::string, std::size_t> name_counts;
        const auto count_names = [&name_counts](const auto& gltf_objects) {
            for (const auto& gltf_object : gltf_objects) {
                if (!gltf_object.name.empty()) {
                    ++name_counts[std::string{gltf_object.name}];
                }
            }
        };
        count_names(m_gltf_asset.accessors);
        count_names(m_gltf_asset.animations);
        count_names(m_gltf_asset.buffers);
        count_names(m_gltf_asset.bufferViews);
        count_names(m_gltf_asset.cameras);
        count_names(m_gltf_asset.externalAssets);
        count_names(m_gltf_asset.files);
        count_names(m_gltf_asset.images);
        count_names(m_gltf_asset.materials);
        count_names(m_gltf_asset.meshes);
        count_names(m_gltf_asset.nodes);
        count_names(m_gltf_asset.samplers);
        count_names(m_gltf_asset.scenes);
        count_names(m_gltf_asset.skins);
        count_names(m_gltf_asset.textures);

        // Item-backed objects in deterministic (category, index) order. The
        // uid/name pointers stay valid: no top-level array grows between
        // here and serialization.
        class Uid_target
        {
        public:
            std::size_t                  index;
            FASTGLTF_STD_PMR_NS::string* gltf_uid;
            FASTGLTF_STD_PMR_NS::string* gltf_name;
            const erhe::Item_base*       item;
        };
        std::vector<Uid_target> targets;
        const auto add_targets = [&targets](auto& gltf_objects, const auto& item_to_index) {
            std::vector<Uid_target> category_targets;
            for (const auto& [item, index_entry] : item_to_index) {
                const std::optional<std::size_t> index{index_entry}; // map values are size_t or optional<size_t>
                if (!index.has_value()) {
                    continue;
                }
                auto& gltf_object = gltf_objects[index.value()];
                category_targets.push_back(Uid_target{index.value(), &gltf_object.uid, &gltf_object.name, item});
            }
            std::sort(
                category_targets.begin(),
                category_targets.end(),
                [](const Uid_target& lhs, const Uid_target& rhs) {
                    return lhs.index < rhs.index;
                }
            );
            targets.insert(targets.end(), category_targets.begin(), category_targets.end());
        };
        add_targets(m_gltf_asset.nodes,      m_erhe_node_to_gltf_node_index);
        add_targets(m_gltf_asset.meshes,     m_erhe_mesh_to_gltf_mesh_index);
        add_targets(m_gltf_asset.cameras,    m_erhe_camera_to_gltf_camera_index);
        add_targets(m_gltf_asset.materials,  m_exported_materials);
        add_targets(m_gltf_asset.images,     m_exported_images);
        add_targets(m_gltf_asset.skins,      m_exported_skins);
        add_targets(m_gltf_asset.animations, m_exported_animations);

        // Pass 1: carried uids, preserved verbatim when they satisfy the
        // uniqueness rules; violators are left empty for pass 2.
        std::unordered_set<std::string> used_uids;
        const auto collides_with_name = [&name_counts](const std::string& uid, const FASTGLTF_STD_PMR_NS::string& own_name) -> bool {
            const auto it = name_counts.find(uid);
            if (it == name_counts.end()) {
                return false;
            }
            const std::size_t own_match = (std::string_view{own_name} == std::string_view{uid}) ? 1 : 0;
            return it->second > own_match;
        };
        for (const Uid_target& target : targets) {
            const std::string& carried = target.item->get_gltf_uid();
            if (carried.empty()) {
                continue;
            }
            if (used_uids.contains(carried)) {
                log_gltf->warn("glTF export: uid '{}' of '{}' collides with another object's uid - regenerating", carried, target.item->get_name());
                continue;
            }
            if (collides_with_name(carried, *target.gltf_name)) {
                log_gltf->warn("glTF export: uid '{}' of '{}' collides with another object's name - regenerating", carried, target.item->get_name());
                continue;
            }
            *target.gltf_uid = FASTGLTF_STD_PMR_NS::string{carried};
            used_uids.insert(carried);
        }

        // Pass 2: generate for items without a (valid) uid. Alphanumeric
        // (the explainer's recommended charset), 16 chars (~95 bits);
        // regenerated on the astronomically unlikely collision with any
        // existing identifier.
        std::mt19937_64 rng{std::random_device{}()};
        constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::uniform_int_distribution<std::size_t> pick_char{0, sizeof(alphabet) - 2};
        for (const Uid_target& target : targets) {
            if (!target.gltf_uid->empty()) {
                continue;
            }
            std::string uid;
            do {
                uid.clear();
                for (int i = 0; i < 16; ++i) {
                    uid.push_back(alphabet[pick_char(rng)]);
                }
            } while (used_uids.contains(uid) || name_counts.contains(uid));
            *target.gltf_uid = FASTGLTF_STD_PMR_NS::string{uid};
            used_uids.insert(uid);
            // Persisting the generated uid on the item is required so the
            // next save reuses it. Exported items arrive through const
            // pointers, but they are never created const, and uid
            // assignment is the export's one sanctioned item mutation.
            const_cast<erhe::Item_base*>(target.item)->set_gltf_uid(uid);
            log_gltf->trace("glTF export: assigned uid '{}' to '{}'", uid, target.item->get_name());
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
    const Gltf_export_arguments& m_arguments;
    std::map<std::string, std::size_t> m_uri_to_external_asset_index;
    fastgltf::Asset          m_gltf_asset;

    std::unordered_map<const erhe::scene::Node*, std::size_t>  m_erhe_node_to_gltf_node_index;
    std::unordered_map<const erhe::scene::Light*, std::size_t> m_exported_lights;

    // Adds an extension name to extensionsUsed once.
    void declare_extension_used(const std::string_view extension_name)
    {
        for (const auto& existing : m_gltf_asset.extensionsUsed) {
            if (std::string_view{existing} == extension_name) {
                return;
            }
        }
        m_gltf_asset.extensionsUsed.emplace_back(std::string{extension_name}.c_str());
    }
};

auto Gltf_exporter::export_gltf() -> std::string
{
    m_gltf_asset.assetInfo = fastgltf::AssetInfo{
        .gltfVersion = "2.0",
        .minVersion  = "",
        .copyright   = "",
        .generator   = "erhe"
    };
    m_gltf_asset.defaultScene = std::size_t{0};

    {
        fastgltf::Scene scene{};
        // Direct children only (see the matching comment in process_node);
        // the recursive for_each_child_const() made every descendant a
        // scene root and re-exported nested subtrees. import_root children
        // (the implicit wrapper import creates) are unwrapped.
        process_child_nodes(m_arguments.root_node, erhe::scene::Trs_transform{}, scene.nodeIndices);
        m_gltf_asset.scenes.push_back(std::move(scene));
    }

#if FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES
    // Before combine_buffers(): mesh-keyed collider geometry may export
    // additional meshes (process_mesh), which adds buffers.
    if (m_arguments.physics_data != nullptr) {
        process_physics();
    }
#endif

    // After the node and physics passes so the extra meshes land last in
    // the meshes array (deterministic indices for ERHE_brushes).
    process_extra_meshes();

    // After the node pass (indices resolved), before combine_buffers()
    // (both add buffers).
    process_skins();
    process_animations();

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
    if (m_uses_texture_transform) {
        m_gltf_asset.extensionsUsed.emplace_back("KHR_texture_transform");
    }

    combine_buffers();

    // After every pass that adds top-level objects or names (nodes, physics,
    // extra meshes, skins, animations, buffer combining): the uid pass needs
    // the complete identifier namespace of the final file.
    stamp_uids();

    // Extensions write context: the ERHE_* extension members (raw JSON,
    // keyed by glTF index) served to the generic extensions write callback.
    class Export_extras_context
    {
    public:
        std::string                                                asset_extensions;
        std::string                                                scene_extensions;
        std::unordered_map<std::size_t, std::string>               node_extensions;
        std::unordered_map<std::size_t, std::string>               camera_extensions;
        std::unordered_map<std::size_t, std::string>               material_extensions;
        std::unordered_map<std::size_t, std::string>               mesh_extensions;
        std::map<std::pair<std::size_t, std::size_t>, std::string> mesh_primitive_extensions;
    };
    Export_extras_context export_extras_context;

    // Resolve extension payloads from erhe objects to glTF indices; payloads
    // whose object did not end up in the export are skipped with a warning.
    {
        const Gltf_export_extension_payloads& payloads = m_arguments.extension_payloads;
        export_extras_context.asset_extensions = payloads.asset;
        export_extras_context.scene_extensions = payloads.scene;
        for (const auto& [node, payload] : payloads.nodes) {
            const auto it = m_erhe_node_to_gltf_node_index.find(node);
            if (it != m_erhe_node_to_gltf_node_index.end()) {
                export_extras_context.node_extensions[it->second] = payload;
            } else {
                log_gltf->warn("glTF export: extension payload for node '{}' outside the exported asset - skipped", node ? node->get_name() : std::string{"<null>"});
            }
        }
        for (const auto& [camera, payload] : payloads.cameras) {
            const auto it = m_erhe_camera_to_gltf_camera_index.find(camera);
            if (it != m_erhe_camera_to_gltf_camera_index.end()) {
                export_extras_context.camera_extensions[it->second] = payload;
            } else {
                log_gltf->warn("glTF export: extension payload for camera '{}' outside the exported asset - skipped", camera ? camera->get_name() : std::string{"<null>"});
            }
        }
        for (const auto& [material, payload] : payloads.materials) {
            const auto it = m_exported_materials.find(material);
            if (it != m_exported_materials.end()) {
                export_extras_context.material_extensions[it->second] = payload;
            } else {
                log_gltf->warn("glTF export: extension payload for material '{}' outside the exported asset - skipped", material ? material->get_name() : std::string{"<null>"});
            }
        }
        for (const auto& [mesh, payload] : payloads.meshes) {
            const auto it = m_erhe_mesh_to_gltf_mesh_index.find(mesh);
            if (it != m_erhe_mesh_to_gltf_mesh_index.end()) {
                export_extras_context.mesh_extensions[it->second] = payload;
            } else {
                log_gltf->warn("glTF export: extension payload for mesh '{}' outside the exported asset - skipped", mesh ? mesh->get_name() : std::string{"<null>"});
            }
        }
        for (const auto& [key, payload] : payloads.mesh_primitives) {
            const auto& [mesh, erhe_primitive_index] = key;
            const auto mesh_it = m_erhe_mesh_to_gltf_mesh_index.find(mesh);
            const auto map_it  = m_erhe_mesh_primitive_index_map.find(mesh);
            if ((mesh_it == m_erhe_mesh_to_gltf_mesh_index.end()) ||
                (map_it == m_erhe_mesh_primitive_index_map.end()) ||
                (erhe_primitive_index >= map_it->second.size()) ||
                !map_it->second[erhe_primitive_index].has_value())
            {
                log_gltf->warn(
                    "glTF export: extension payload for mesh '{}' primitive {} outside the exported asset - skipped",
                    mesh ? mesh->get_name() : std::string{"<null>"}, erhe_primitive_index
                );
                continue;
            }
            export_extras_context.mesh_primitive_extensions[{mesh_it->second, map_it->second[erhe_primitive_index].value()}] = payload;
        }
        // Exporter-generated ERHE_* members (ERHE_geometry on primitives,
        // ERHE_node / ERHE_light on nodes, ERHE_camera on cameras,
        // ERHE_material on materials) merge with any caller payload on the
        // same object.
        const auto merge_extension_members = [](std::string& slot, const std::string& extension_members) {
            if (slot.empty()) {
                slot = extension_members;
            } else {
                slot += ",";
                slot += extension_members;
            }
        };
        for (const auto& [key, extension_members] : m_geometry_primitive_extensions) {
            merge_extension_members(export_extras_context.mesh_primitive_extensions[key], extension_members);
        }
        for (const auto& [index, extension_members] : m_internal_node_extensions) {
            merge_extension_members(export_extras_context.node_extensions[index], extension_members);
        }
        for (const auto& [index, extension_members] : m_internal_camera_extensions) {
            merge_extension_members(export_extras_context.camera_extensions[index], extension_members);
        }
        for (const auto& [index, extension_members] : m_internal_material_extensions) {
            merge_extension_members(export_extras_context.material_extensions[index], extension_members);
        }
        // Asset-root extension payloads built against the now-known glTF
        // indices (doc/gltf-scene-roundtrip-plan.md phase 3: ERHE_brushes,
        // ERHE_node_graphs, ERHE_collections).
        if (m_arguments.asset_extensions_builder) {
            Gltf_export_index_lookup index_lookup{};
            index_lookup.node_indices       = m_erhe_node_to_gltf_node_index;
            index_lookup.material_indices   = m_exported_materials;
            index_lookup.mesh_indices       = m_erhe_mesh_to_gltf_mesh_index;
            index_lookup.extra_mesh_indices = m_extra_mesh_indices;
            const std::vector<std::pair<std::string, std::string>> asset_entries = m_arguments.asset_extensions_builder(index_lookup);
            for (const auto& [extension_name, extension_json] : asset_entries) {
                merge_extension_members(
                    export_extras_context.asset_extensions,
                    fmt::format("\"{}\":{}", extension_name, extension_json)
                );
                declare_extension_used(extension_name);
            }
        }
        if (!m_geometry_primitive_extensions.empty()) {
            declare_extension_used("ERHE_geometry");
        }
        if (!m_internal_node_extensions.empty()) {
            declare_extension_used("ERHE_node");
        }
        if (!m_gltf_asset.lights.empty()) {
            declare_extension_used("ERHE_light");
        }
        if (!m_internal_camera_extensions.empty()) {
            declare_extension_used("ERHE_camera");
        }
        if (!m_internal_material_extensions.empty()) {
            declare_extension_used("ERHE_material");
        }
    }

    // Caller-declared extension names (for the payloads above).
    for (const std::string& extension_name : m_arguments.extensions_used) {
        declare_extension_used(extension_name);
    }

    fastgltf::Exporter exporter{};
    exporter.setUserPointer(&export_extras_context);
    exporter.setExtensionsWriteCallback(
        [](std::size_t object_index, std::size_t sub_object_index, fastgltf::Category object_type, void* user_pointer) -> std::optional<std::string> {
            const Export_extras_context* context = static_cast<const Export_extras_context*>(user_pointer);
            switch (object_type) {
                case fastgltf::Category::Asset: {
                    if (!context->asset_extensions.empty()) {
                        return context->asset_extensions;
                    }
                    return std::nullopt;
                }
                case fastgltf::Category::Scenes: {
                    if ((object_index == 0) && !context->scene_extensions.empty()) {
                        return context->scene_extensions;
                    }
                    return std::nullopt;
                }
                case fastgltf::Category::Nodes: {
                    const auto it = context->node_extensions.find(object_index);
                    if (it != context->node_extensions.end()) {
                        return it->second;
                    }
                    return std::nullopt;
                }
                case fastgltf::Category::Cameras: {
                    const auto it = context->camera_extensions.find(object_index);
                    if (it != context->camera_extensions.end()) {
                        return it->second;
                    }
                    return std::nullopt;
                }
                case fastgltf::Category::Materials: {
                    const auto it = context->material_extensions.find(object_index);
                    if (it != context->material_extensions.end()) {
                        return it->second;
                    }
                    return std::nullopt;
                }
                case fastgltf::Category::Meshes: {
                    if (sub_object_index == fastgltf::invalidSubObjectIndex) {
                        const auto it = context->mesh_extensions.find(object_index);
                        if (it != context->mesh_extensions.end()) {
                            return it->second;
                        }
                        return std::nullopt;
                    }
                    const auto it = context->mesh_primitive_extensions.find({object_index, sub_object_index});
                    if (it != context->mesh_primitive_extensions.end()) {
                        return it->second;
                    }
                    return std::nullopt;
                }
                default: {
                    return std::nullopt;
                }
            }
        }
    );
    // No extras write callback: the legacy erhe_flags node extras and the
    // material extras migrated to the ERHE_node / ERHE_material extensions
    // (doc/gltf-scene-roundtrip-plan.md phase 3); the extras remain parsed
    // for files written before the migration.

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

