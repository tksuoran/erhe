#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/shapes/capsule.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_usd/image_header.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/point_instancer.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/xform_op.hpp"

// LightUSD headers. Together with usd.cpp this is the only place in erhe
// that includes them; everything the rest of erhe sees is in usd.hpp.
#include "lightusd.hh"
#include "core/composition-types.hh"
#include "core/prim.hh"
#include "core/prim-metas.hh"
#include "core/prim-spec.hh"
#include "core/variant-types.hh"
#include "prim-reconstruct.hh"
#include "layer.hh"
#include "stage.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdMtlx.hh"
#include "usdLux.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/scene-access.hh"
#include "value-pprint.hh"
#include "timesamples.hh"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace erhe::usd {

namespace {

// The OpenPBR `base_color` fallback. erhe's own `base_color` default is
// white, so an OpenPBR network that leaves the input unauthored composes to a
// value erhe has to write as a local one for the composed result to be the
// one the file specifies (doc/usd-compatibility-plan.md I2), the way
// `c_usd_diffuse_color_fallback` is the UsdPreviewSurface one.
constexpr glm::vec3 c_open_pbr_base_color_fallback{0.8f, 0.8f, 0.8f};

using Tydra_scene     = lightusd::tydra::RenderScene;
using Tydra_node      = lightusd::tydra::Node;
using Tydra_mesh      = lightusd::tydra::RenderMesh;
using Tydra_material  = lightusd::tydra::RenderMaterial;
using Tydra_camera    = lightusd::tydra::RenderCamera;
using Tydra_light     = lightusd::tydra::RenderLight;
using Tydra_attribute = lightusd::tydra::VertexAttribute;
using Tydra_subset    = lightusd::tydra::MaterialSubset;

// Number of float components an attribute format carries. Tydra converts
// primvars to float formats; anything else is reported and skipped.
[[nodiscard]] auto attribute_component_count(const lightusd::tydra::VertexAttributeFormat format) -> std::size_t
{
    switch (format) {
        case lightusd::tydra::VertexAttributeFormat::Float: return 1;
        case lightusd::tydra::VertexAttributeFormat::Vec2:  return 2;
        case lightusd::tydra::VertexAttributeFormat::Vec3:  return 3;
        case lightusd::tydra::VertexAttributeFormat::Vec4:  return 4;
        default: return 0;
    }
}

// Which element of an attribute belongs to one polygon corner. This is the
// element table of doc/usd_compatibility.md read backwards: constant is one
// value for the mesh, uniform one per facet, vertex / varying one per
// vertex, faceVarying one per corner. An indexed primvar keeps its own
// index buffer, addressed by corner (the form Tydra produces for indexed
// faceVarying and vertex primvars alike).
[[nodiscard]] auto attribute_element_index(
    const Tydra_attribute& attribute,
    const std::size_t      vertex,
    const std::size_t      facet,
    const std::size_t      corner
) -> std::size_t
{
    switch (attribute.variability) {
        case lightusd::tydra::VertexVariability::Constant:    return 0;
        case lightusd::tydra::VertexVariability::Uniform:     return facet;
        case lightusd::tydra::VertexVariability::Varying:     return vertex;
        case lightusd::tydra::VertexVariability::Vertex:      return vertex;
        case lightusd::tydra::VertexVariability::FaceVarying: return corner;
        case lightusd::tydra::VertexVariability::Indexed:     return corner;
        default:                                             return 0;
    }
}

// Whether the render mesh carries a color for its vertices. Tydra delivers
// `primvars:displayColor` as `vertex_colors` when it varies, and a single
// constant value (the whole mesh one color) in `displayColor` with
// `has_authored_displayColor` set; both are authored colors.
[[nodiscard]] auto usd_mesh_has_color(const lightusd::tydra::RenderMesh& usd_mesh) -> bool
{
    if (usd_mesh.has_authored_displayColor) {
        return true;
    }
    return !usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3);
}

[[nodiscard]] auto read_attribute(const Tydra_attribute& attribute, const std::size_t element) -> glm::vec4;

// The color of one corner: the varying vertex color when there is one, else
// the mesh's constant displayColor. Opacity 1 unless a varying
// `primvars:displayOpacity` names it.
[[nodiscard]] auto usd_mesh_corner_color(
    const lightusd::tydra::RenderMesh& usd_mesh,
    const std::size_t                  vertex,
    const std::size_t                  facet,
    const std::size_t                  corner
) -> glm::vec4
{
    glm::vec4 color{usd_mesh.displayColor.r, usd_mesh.displayColor.g, usd_mesh.displayColor.b, 1.0f};
    if (!usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3)) {
        color = read_attribute(usd_mesh.vertex_colors, attribute_element_index(usd_mesh.vertex_colors, vertex, facet, corner));
    }
    color.w = 1.0f;
    if (!usd_mesh.vertex_opacities.empty() && (attribute_component_count(usd_mesh.vertex_opacities.format) >= 1)) {
        color.w = read_attribute(usd_mesh.vertex_opacities, attribute_element_index(usd_mesh.vertex_opacities, vertex, facet, corner)).x;
    }
    return color;
}

[[nodiscard]] auto read_attribute(const Tydra_attribute& attribute, const std::size_t element) -> glm::vec4
{
    glm::vec4         result{0.0f, 0.0f, 0.0f, 1.0f};
    const std::size_t components = attribute_component_count(attribute.format);
    if (components == 0) {
        return result;
    }
    std::size_t index = element;
    if (attribute.variability == lightusd::tydra::VertexVariability::Indexed) {
        if (element >= attribute.indices.size()) {
            return result;
        }
        index = attribute.indices[element];
    }
    const std::size_t stride = attribute.stride_bytes();
    const std::size_t offset = index * stride;
    if ((offset + (components * sizeof(float))) > attribute.data.size()) {
        return result;
    }
    const std::uint8_t* source = attribute.data.data() + offset;
    float               values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(values, source, components * sizeof(float));
    for (std::size_t i = 0; i < components; ++i) {
        result[static_cast<glm::length_t>(i)] = values[i];
    }
    return result;
}

// A USD matrix4d is row-major with USD's row-vector convention; a glm mat4
// is column-major with the column-vector convention. The two store the same
// coefficients in the same order, so an element-for-element copy is the
// correct conversion (not a transpose).
[[nodiscard]] auto to_glm(const lightusd::value::matrix4d& matrix) -> glm::mat4
{
    glm::mat4 result{1.0f};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            result[j][i] = static_cast<float>(matrix.m[j][i]);
        }
    }
    return result;
}

// A prim whose class carries no transform must not silently swallow one, so
// the conversion tests the matrix Tydra composed for it.
[[nodiscard]] auto is_identity_matrix(const glm::mat4& matrix) -> bool
{
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const float expected = (i == j) ? 1.0f : 0.0f;
            if (matrix[j][i] != expected) {
                return false;
            }
        }
    }
    return true;
}

// The glm column-vector matrix of a USD matrix4d, in double precision: the
// element-for-element copy to_glm above makes, without the narrowing.
[[nodiscard]] auto to_glm_double(const lightusd::value::matrix4d& matrix) -> glm::dmat4
{
    glm::dmat4 result{1.0};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            result[j][i] = matrix.m[j][i];
        }
    }
    return result;
}

[[nodiscard]] auto is_near_matrix(const glm::mat4& lhs, const glm::mat4& rhs) -> bool
{
    constexpr float tolerance = 1e-5f;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            if (std::abs(lhs[j][i] - rhs[j][i]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// The erhe counterpart of a USD xformOp type. `ResetXformStack` is a flag of
// the stack rather than an op, so it is not one of these and the caller
// handles it.
[[nodiscard]] auto to_erhe_xform_op_type(
    const lightusd::XformOp::OpType op_type,
    erhe::scene::Xform_op_type&     out_type
) -> bool
{
    using Usd_type  = lightusd::XformOp::OpType;
    using Erhe_type = erhe::scene::Xform_op_type;
    switch (op_type) {
        case Usd_type::Transform: out_type = Erhe_type::transform;  return true;
        case Usd_type::Translate: out_type = Erhe_type::translate;  return true;
        case Usd_type::Scale:     out_type = Erhe_type::scale;      return true;
        case Usd_type::RotateX:   out_type = Erhe_type::rotate_x;   return true;
        case Usd_type::RotateY:   out_type = Erhe_type::rotate_y;   return true;
        case Usd_type::RotateZ:   out_type = Erhe_type::rotate_z;   return true;
        case Usd_type::RotateXYZ: out_type = Erhe_type::rotate_xyz; return true;
        case Usd_type::RotateXZY: out_type = Erhe_type::rotate_xzy; return true;
        case Usd_type::RotateYXZ: out_type = Erhe_type::rotate_yxz; return true;
        case Usd_type::RotateYZX: out_type = Erhe_type::rotate_yzx; return true;
        case Usd_type::RotateZXY: out_type = Erhe_type::rotate_zxy; return true;
        case Usd_type::RotateZYX: out_type = Erhe_type::rotate_zyx; return true;
        case Usd_type::Orient:    out_type = Erhe_type::orient;     return true;
        default:                                                    return false;
    }
}

// The USD value of one op at `time_code`. `samples` is the op's time samples
// when it has any, in which case they are the op's value and its `default` is
// not consulted: a USD attribute that carries both is the samples everywhere
// the samples reach.
template <typename T>
[[nodiscard]] auto get_usd_op_value_at(
    const lightusd::XformOp&            op,
    const lightusd::value::TimeSamples* samples,
    const double                        time_code,
    T&                                  out_value
) -> bool
{
    if (samples != nullptr) {
        return samples->get(&out_value, time_code, lightusd::value::TimeSampleInterpolationType::Linear);
    }
    const nonstd::optional<T> value = op.get_value<T>();
    if (!value) {
        return false;
    }
    out_value = value.value();
    return true;
}

// The erhe form of one USD op value. The overload set is the op value types
// USD spells; each answers the variant member erhe::scene::Xform_op::value
// takes for the op types that use that spelling.
[[nodiscard]] auto to_xform_op_value(const lightusd::value::matrix4d& value) -> erhe::scene::Xform_op_value
{
    return to_glm_double(value);
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::double3& value) -> erhe::scene::Xform_op_value
{
    return glm::dvec3{value[0], value[1], value[2]};
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::float3& value) -> erhe::scene::Xform_op_value
{
    return glm::dvec3{
        static_cast<double>(value[0]),
        static_cast<double>(value[1]),
        static_cast<double>(value[2])
    };
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::half3& value) -> erhe::scene::Xform_op_value
{
    return glm::dvec3{
        static_cast<double>(lightusd::value::half_to_float(value[0])),
        static_cast<double>(lightusd::value::half_to_float(value[1])),
        static_cast<double>(lightusd::value::half_to_float(value[2]))
    };
}
[[nodiscard]] auto to_xform_op_value(const double value) -> erhe::scene::Xform_op_value
{
    return value;
}
[[nodiscard]] auto to_xform_op_value(const float value) -> erhe::scene::Xform_op_value
{
    return static_cast<double>(value);
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::half value) -> erhe::scene::Xform_op_value
{
    return static_cast<double>(lightusd::value::half_to_float(value));
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::quatd& value) -> erhe::scene::Xform_op_value
{
    return glm::dquat{value.real, value.imag[0], value.imag[1], value.imag[2]};
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::quatf& value) -> erhe::scene::Xform_op_value
{
    return glm::dquat{
        static_cast<double>(value.real),
        static_cast<double>(value.imag[0]),
        static_cast<double>(value.imag[1]),
        static_cast<double>(value.imag[2])
    };
}
[[nodiscard]] auto to_xform_op_value(const lightusd::value::quath& value) -> erhe::scene::Xform_op_value
{
    return glm::dquat{
        static_cast<double>(lightusd::value::half_to_float(value.real)),
        static_cast<double>(lightusd::value::half_to_float(value.imag[0])),
        static_cast<double>(lightusd::value::half_to_float(value.imag[1])),
        static_cast<double>(lightusd::value::half_to_float(value.imag[2]))
    };
}

// One op of USD value type T: its value at `time_code`, and its time samples
// as authored when it carries any.
template <typename T>
[[nodiscard]] auto read_typed_xform_op(
    const lightusd::XformOp&            usd_op,
    const lightusd::value::TimeSamples* samples,
    const double                        time_code,
    erhe::scene::Xform_op&              out_op
) -> bool
{
    T value{};
    if (!get_usd_op_value_at<T>(usd_op, samples, time_code, value)) {
        return false;
    }
    out_op.value = to_xform_op_value(value);
    if (samples == nullptr) {
        return true;
    }
    const std::size_t count = samples->size();
    out_op.samples.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const nonstd::optional<double> sample_time = samples->get_time(i);
        if (!sample_time) {
            return false;
        }
        T sample_value{};
        if (!samples->get(&sample_value, sample_time.value(), lightusd::value::TimeSampleInterpolationType::Linear)) {
            return false;
        }
        out_op.samples.push_back(
            erhe::scene::Xform_op_sample{
                .time_code = sample_time.value(),
                .value     = to_xform_op_value(sample_value)
            }
        );
    }
    return true;
}

// The earliest time any op of `xformable` samples, and whether one does.
[[nodiscard]] auto get_first_xform_op_sample_time(const lightusd::Xformable& xformable, double& out_time_code) -> bool
{
    bool found{false};
    for (const lightusd::XformOp& usd_op : xformable.xformOps) {
        if (!usd_op.has_timesamples()) {
            continue;
        }
        const nonstd::optional<lightusd::value::TimeSamples> samples = usd_op.get_timesamples();
        if (!samples || samples.value().empty()) {
            continue;
        }
        const nonstd::optional<double> time = samples.value().get_time(0);
        if (!time) {
            continue;
        }
        if (!found || (time.value() < out_time_code)) {
            out_time_code = time.value();
            found         = true;
        }
    }
    return found;
}

// One authored `xformOp:<type>[:<suffix>]` as an erhe Xform_op: the type, the
// suffix and the invert flag as authored, the value at `time_code` in double
// precision, the authored time samples when the op has any, and the authored
// value type kept as the op's precision, so the op is written back as the type
// it was authored with.
[[nodiscard]] auto read_xform_op(
    const lightusd::XformOp& usd_op,
    const double             time_code,
    erhe::scene::Xform_op&   out_op
) -> bool
{
    using Precision = erhe::scene::Xform_op_precision;
    if (!to_erhe_xform_op_type(usd_op.op_type, out_op.type)) {
        return false;
    }
    out_op.suffix   = usd_op.suffix;
    out_op.inverted = usd_op.inverted;

    nonstd::optional<lightusd::value::TimeSamples> samples;
    if (usd_op.has_timesamples()) {
        samples = usd_op.get_timesamples();
        if (samples && samples.value().empty()) {
            samples = nonstd::nullopt;
        }
    }
    const lightusd::value::TimeSamples* sample_pointer = samples ? &samples.value() : nullptr;

    const std::string type_name = usd_op.get_value_type_name();
    if (type_name == lightusd::value::kMatrix4d) {
        out_op.precision = Precision::double_;
        return read_typed_xform_op<lightusd::value::matrix4d>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kDouble3) {
        out_op.precision = Precision::double_;
        return read_typed_xform_op<lightusd::value::double3>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kFloat3) {
        out_op.precision = Precision::float_;
        return read_typed_xform_op<lightusd::value::float3>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kHalf3) {
        out_op.precision = Precision::half_;
        return read_typed_xform_op<lightusd::value::half3>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kDouble) {
        out_op.precision = Precision::double_;
        return read_typed_xform_op<double>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kFloat) {
        out_op.precision = Precision::float_;
        return read_typed_xform_op<float>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kHalf) {
        out_op.precision = Precision::half_;
        return read_typed_xform_op<lightusd::value::half>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kQuatd) {
        out_op.precision = Precision::double_;
        return read_typed_xform_op<lightusd::value::quatd>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kQuatf) {
        out_op.precision = Precision::float_;
        return read_typed_xform_op<lightusd::value::quatf>(usd_op, sample_pointer, time_code, out_op);
    }
    if (type_name == lightusd::value::kQuath) {
        out_op.precision = Precision::half_;
        return read_typed_xform_op<lightusd::value::quath>(usd_op, sample_pointer, time_code, out_op);
    }
    return false;
}

[[nodiscard]] auto is_srgb_color_space(const lightusd::tydra::ColorSpace color_space) -> bool
{
    switch (color_space) {
        case lightusd::tydra::ColorSpace::sRGB:
        case lightusd::tydra::ColorSpace::sRGB_Texture:
        case lightusd::tydra::ColorSpace::sRGB_DisplayP3:
        case lightusd::tydra::ColorSpace::g22_Rec709:
        case lightusd::tydra::ColorSpace::g18_Rec709:
            return true;
        default:
            return false;
    }
}

// The path an asset literal carries, or nothing when the literal is not an
// asset literal. USDA delimits an asset path with `@ ... @`, and with
// `@@@ ... @@@` when the path itself contains an `@`; the path between the
// delimiters is the property text form (M6) verbatim, so it keeps the
// spaces and brackets a path may contain.
[[nodiscard]] auto usd_asset_literal_path(const std::string& literal) -> std::optional<std::string>
{
    static constexpr std::string_view triple{"@@@"};
    if ((literal.size() >= 6) && (literal.compare(0, triple.size(), triple) == 0) && (literal.compare(literal.size() - triple.size(), triple.size(), triple) == 0)) {
        return literal.substr(triple.size(), literal.size() - (2 * triple.size()));
    }
    if ((literal.size() >= 2) && (literal.front() == '@') && (literal.back() == '@')) {
        return literal.substr(1, literal.size() - 2);
    }
    return std::nullopt;
}

// A USDA literal rewritten in erhe's property text form (D16): a tuple or
// array becomes space-separated components, a quoted token or string loses
// its quotes. `(1, 0.5, 0)` becomes `1 0.5 0`, `"guide"` becomes `guide`,
// `5000` stays `5000`. A string value that carries a comma or a bracket of
// its own is not representable this way and is left to fail parsing.
[[nodiscard]] auto usd_literal_to_property_text(const std::string& literal) -> std::string
{
    std::string text;
    text.reserve(literal.size());
    bool pending_space = false;
    for (const char character : literal) {
        switch (character) {
            case '(':
            case ')':
            case '[':
            case ']':
            case '"':
            case '\'': {
                break;
            }
            case ',':
            case ' ':
            case '\t':
            case '\n':
            case '\r': {
                pending_space = !text.empty();
                break;
            }
            default: {
                if (pending_space) {
                    text.push_back(' ');
                    pending_space = false;
                }
                text.push_back(character);
                break;
            }
        }
    }
    return text;
}

// The USD `purpose` vocabulary, which erhe's Purpose enumeration mirrors
// token for token (doc/usd_compatibility.md, property system).
[[nodiscard]] auto to_erhe_purpose(const lightusd::Purpose purpose) -> erhe::Purpose
{
    switch (purpose) {
        case lightusd::Purpose::Default: return erhe::Purpose::default_;
        case lightusd::Purpose::Render:  return erhe::Purpose::render;
        case lightusd::Purpose::Proxy:   return erhe::Purpose::proxy;
        case lightusd::Purpose::Guide:   return erhe::Purpose::guide;
        default:                         return erhe::Purpose::default_;
    }
}

// One group of facets that shares a material: a GeomSubset with
// familyName = materialBind, or the facets no subset claims.
class Facet_group final
{
public:
    std::string               name;
    int                       material_id{-1};
    std::vector<std::uint32_t> facets;
};

// How many joint influences one erhe vertex attribute set holds, and how
// many sets erhe's vertex format has (`joint_indices_0` / `joint_indices_1`
// and their weights).
constexpr std::size_t c_joints_per_set{4};
constexpr std::size_t c_max_joint_sets{2};

// The joint influences of one skinned mesh, in the shape erhe's vertex
// attributes hold them (doc/usd-compatibility-plan.md K1): `set_count` sets
// of four (joint index, weight) pairs per vertex, indexed by the USD point
// index. USD authors `elementSize` influences per vertex, unnormalized and
// in no particular order, so the strongest `set_count * 4` are kept and
// their weights normalized to sum to one - which is what the shader's
// weighted sum of `world_from_joint * inverse_bind` needs.
class Joint_influences final
{
public:
    [[nodiscard]] auto empty() const -> bool { return set_count == 0; }

    std::size_t                set_count   {0};
    std::size_t                vertex_count{0};
    std::vector<std::uint32_t> indices; // set_count * 4 per vertex
    std::vector<float>         weights; // set_count * 4 per vertex
};

[[nodiscard]] auto make_joint_influences(const Tydra_mesh& usd_mesh) -> Joint_influences
{
    Joint_influences result{};
    if (usd_mesh.skel_id < 0) {
        return result;
    }
    const lightusd::tydra::JointAndWeight& skin = usd_mesh.joint_and_weights;
    if ((skin.elementSize < 1) || skin.jointIndices.empty()) {
        return result;
    }
    const std::size_t element_size = static_cast<std::size_t>(skin.elementSize);
    const std::size_t vertex_count = usd_mesh.points.size();
    const std::size_t needed       = vertex_count * element_size;
    if ((skin.jointIndices.size() < needed) || (skin.jointWeights.size() < needed)) {
        log_usd->warn(
            "USD prim '{}': `primvars:skel:jointIndices` / `jointWeights` hold fewer than {} values - the mesh is not skinned",
            usd_mesh.abs_path, needed
        );
        return result;
    }

    const std::size_t capacity  = c_max_joint_sets * c_joints_per_set;
    const std::size_t set_count = std::min(
        c_max_joint_sets,
        (element_size + c_joints_per_set - 1) / c_joints_per_set
    );
    if (element_size > capacity) {
        log_usd->warn(
            "USD prim '{}': `elementSize` {} of the skin primvars exceeds the {} influences per vertex erhe carries - the strongest {} are kept",
            usd_mesh.abs_path, element_size, capacity, capacity
        );
    }

    const std::size_t slots = set_count * c_joints_per_set;
    result.set_count    = set_count;
    result.vertex_count = vertex_count;
    result.indices.assign(vertex_count * slots, 0u);
    result.weights.assign(vertex_count * slots, 0.0f);

    // One influence of one vertex, sorted by weight so the strongest survive
    // the cut. Held outside the loop so the sort scratch is allocated once.
    class Influence final
    {
    public:
        float         weight{0.0f};
        std::uint32_t index {0};
    };
    std::vector<Influence> influences;
    influences.reserve(element_size);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        influences.clear();
        for (std::size_t element = 0; element < element_size; ++element) {
            const std::size_t source = (vertex * element_size) + element;
            const float       weight = skin.jointWeights[source];
            const int         index  = skin.jointIndices[source];
            if ((weight <= 0.0f) || (index < 0)) {
                continue;
            }
            influences.push_back(Influence{.weight = weight, .index = static_cast<std::uint32_t>(index)});
        }
        std::stable_sort(
            influences.begin(),
            influences.end(),
            [](const Influence& lhs, const Influence& rhs) -> bool { return lhs.weight > rhs.weight; }
        );
        const std::size_t kept = std::min(slots, influences.size());
        float             sum  = 0.0f;
        for (std::size_t i = 0; i < kept; ++i) {
            sum += influences[i].weight;
        }
        const float scale = (sum > 0.0f) ? (1.0f / sum) : 0.0f;
        for (std::size_t i = 0; i < kept; ++i) {
            result.indices[(vertex * slots) + i] = influences[i].index;
            result.weights[(vertex * slots) + i] = influences[i].weight * scale;
        }
    }
    return result;
}

class Importer final
{
public:
    Importer(const Usd_load_arguments& arguments, Usd_load_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    void convert(const Stage::Impl& impl)
    {
        ERHE_PROFILE_FUNCTION();

        m_impl = &impl;
        const lightusd::Stage& stage = impl.stage;

        report_skipped_physics(stage);
        read_time_codes(stage);

        lightusd::tydra::RenderSceneConverterEnv env{stage};
        env.usd_filename = m_arguments.path.generic_string();
        // Authored topology and authored primvar variability are what the
        // geometry-normative path needs, so neither triangulation nor
        // vertex-index building runs: facet counts and indices go straight
        // into geogram, and every attribute keeps the variability that
        // names its erhe element domain.
        env.mesh_config.triangulate           = false;
        env.mesh_config.build_vertex_indices  = false;
        env.mesh_config.extract_all_texcoords = true;
        // Everything the stage evaluates is evaluated at one time code, so
        // that the transform Tydra composes for a prim and the authored
        // xformOp stack the M8 reader reads say the same thing
        // (src/erhe/usd/notes.md, "Time samples"). A stage that samples
        // nothing keeps USD's default time code, which is the only value an
        // attribute without samples has.
        env.timecode = m_has_time_samples
            ? m_import_time_code
            : lightusd::value::TimeCode::Default();
        // The search path for texture assets is the file's own directory.
        const std::string directory = m_arguments.path.parent_path().generic_string();
        if (!directory.empty()) {
            env.set_search_paths({directory});
        }
        // The converter runs metadata-only for textures: it resolves each
        // image's path and color space and reads no texel. erhe decodes the
        // image itself (convert_images / Usd_image), and Tydra's decode would
        // be a second one, followed by an 8-bit-to-float conversion nothing
        // reads. (The LIGHTUSD_WITH_BUILTIN_IMAGE_LOADER option does not
        // compile Tydra's decoder out; this flag is what keeps it idle.)
        env.scene_config.load_texture_assets = false;

        lightusd::tydra::RenderSceneConverter converter;
        Tydra_scene                           scene;
        const bool converted = converter.ConvertToRenderScene(env, &scene);
        const std::string converter_warning = filter_converter_warning(converter.GetWarning());
        if (!converter_warning.empty()) {
            m_result.warning = converter_warning;
            log_usd->warn("USD '{}': {}", env.usd_filename, converter_warning);
        }
        if (!converted) {
            m_result.error = converter.GetError().empty()
                ? std::string{"USD to render scene conversion failed"}
                : converter.GetError();
            log_usd->error("USD '{}' conversion failed: {}", env.usd_filename, m_result.error);
            return;
        }

        m_stage = &stage;
        m_scene = &scene;

        m_result.data.up_axis         = scene.meta.upAxis;
        m_result.data.meters_per_unit = scene.meta.metersPerUnit;
        m_result.data.default_prim    = stage.metas().defaultPrim.str();
        for (const lightusd::SubLayer& sub_layer : stage.metas().subLayers) {
            m_result.data.sublayers.push_back(sub_layer.assetPath.GetAssetPath());
        }
        read_custom_layer_data(stage);

        read_layer_composition();
        index_skeletons();
        append_unconverted_materials(env, converter, scene);
        convert_images();
        convert_materials();
        convert_meshes();
        resolve_brush_geometry();
        resolve_node_graph_geometry();
        convert_cameras();
        convert_lights();
        convert_nodes();
        build_skins();
        apply_variant_bindings();
        elide_default_local_values();
        apply_authored_opinions();
        build_animation();

        log_usd->info(
            "USD '{}': {} nodes, {} meshes, {} materials, {} images, {} cameras, {} lights, {} classes",
            env.usd_filename,
            m_result.data.nodes.size(),
            m_result.data.meshes.size(),
            m_result.data.materials.size(),
            m_result.data.images.size(),
            m_result.data.cameras.size(),
            m_result.data.lights.size(),
            m_result.data.classes.size()
        );
    }

private:
    // The authored opinions of every prim, applied once the elision pass
    // below has run. An authored value that happens to equal the property's
    // default is still authored (D32) - `visibility = "inherited"` and
    // `custom bool erhe:Mesh:shadow_cast = 0` say something - so it must not
    // meet the elision, which exists for the values the conversion writes
    // unconditionally.
    void apply_authored_opinions()
    {
        ERHE_PROFILE_FUNCTION();
        for (const Authored_opinions& opinions : m_authored_opinions) {
            if (opinions.visibility_target != nullptr) {
                apply_visibility_and_purpose(opinions.absolute_path, *opinions.visibility_target);
                apply_active(opinions.absolute_path, *opinions.visibility_target);
                apply_defined(opinions.absolute_path, *opinions.visibility_target);
                apply_double_sided(opinions.absolute_path, *opinions.visibility_target);
            }
            apply_erhe_custom_attributes(opinions.absolute_path, opinions.primary, opinions.secondary);
        }
    }

    // A local value is an authored value (doc/property-system.md D32,
    // doc/usd-compatibility-plan.md M4). The conversions above ask the
    // composed prim which attributes carry an authored opinion and write
    // only those, so this pass is the safety net for the few fields that
    // are still filled unconditionally: the light type, which comes from
    // the prim's schema type rather than from an attribute, and the erhe
    // properties an item's constructor seeds. It takes back a local value
    // that merely repeats the item's own default.
    void elide_default_local_values()
    {
        ERHE_PROFILE_FUNCTION();
        const auto elide = [](const std::shared_ptr<erhe::Item_base>& item) {
            if (item) {
                erhe::property::clear_default_valued_local_properties(*item);
            }
        };
        for (const std::shared_ptr<erhe::scene::Node>&         node     : m_result.data.nodes)     { elide(node);     }
        for (const std::shared_ptr<erhe::Typed>&              prim     : m_result.data.prims)     { elide(prim);     }
        for (const std::shared_ptr<erhe::scene::Mesh>&         mesh     : m_result.data.meshes)    { elide(mesh);     }
        for (const std::shared_ptr<erhe::scene::Light>&        light    : m_result.data.lights)    { elide(light);    }
        for (const std::shared_ptr<erhe::scene::Camera>&       camera   : m_result.data.cameras)   { elide(camera);   }
        for (const std::shared_ptr<erhe::primitive::Material>& material : m_result.data.materials) { elide(material); }
    }

    // The stage's time coordinates, as the layer stack authored them. The
    // fallbacks LightUSD hands back for the ones no layer authored are erhe's
    // too (24 time codes per second, a range of nothing), so only what the
    // file spelled is marked authored and only that is written back.
    void read_time_codes(const lightusd::Stage& stage)
    {
        const lightusd::StageMetas& metas = stage.metas();
        Usd_time_codes&             out   = m_result.data.time_codes;
        out.time_codes_per_second          = metas.timeCodesPerSecond.get_value();
        out.time_codes_per_second_authored = metas.timeCodesPerSecond.authored();
        out.start_time_code                = metas.startTimeCode.get_value();
        out.start_time_code_authored       = metas.startTimeCode.authored();
        out.end_time_code                  = metas.endTimeCode.get_value();
        out.end_time_code_authored         = metas.endTimeCode.authored();

        // The time the whole stage is evaluated at: `startTimeCode` when the
        // layer stack authors one, else the earliest time any prim of the
        // stage samples a transform at. It is the time a viewer opens the
        // stage at, so the pose the import gives the scene is the reference
        // frame of its clips.
        double earliest_sample_time{0.0};
        m_has_time_samples = false;
        for (const lightusd::Prim& prim : stage.root_prims()) {
            find_earliest_sample_time(prim, earliest_sample_time, m_has_time_samples);
        }
        m_import_time_code = out.start_time_code_authored ? out.start_time_code : earliest_sample_time;
    }

    // The earliest time any xformOp of `prim` or its descendants samples.
    static void find_earliest_sample_time(const lightusd::Prim& prim, double& out_time_code, bool& out_found)
    {
        const lightusd::Xformable* xformable = get_xformable(prim);
        if (xformable != nullptr) {
            double prim_time{0.0};
            if (get_first_xform_op_sample_time(*xformable, prim_time)) {
                if (!out_found || (prim_time < out_time_code)) {
                    out_time_code = prim_time;
                    out_found     = true;
                }
            }
        }
        for (const lightusd::Prim& child : prim.children()) {
            find_earliest_sample_time(child, out_time_code, out_found);
        }
    }

    // Which erhe animation path an op drives, and in which order the three
    // may appear: erhe applies a channel by writing the component into the
    // node's TRS, so a stack the animation can drive is one whose composition
    // IS that TRS - at most one translate, one rotate and one scale op, in
    // that order, none inverted and none suffixed.
    [[nodiscard]] static auto get_xform_op_animation_path(
        const erhe::scene::Xform_op_type   type,
        erhe::scene::Animation_path&       out_path
    ) -> bool
    {
        using Op   = erhe::scene::Xform_op_type;
        using Path = erhe::scene::Animation_path;
        switch (type) {
            case Op::translate:  out_path = Path::TRANSLATION; return true;
            case Op::scale:      out_path = Path::SCALE;       return true;
            case Op::rotate_x:
            case Op::rotate_y:
            case Op::rotate_z:
            case Op::rotate_xyz:
            case Op::rotate_xzy:
            case Op::rotate_yxz:
            case Op::rotate_yzx:
            case Op::rotate_zxy:
            case Op::rotate_zyx:
            case Op::orient:     out_path = Path::ROTATION;    return true;
            default:                                           return false;
        }
    }

    // Why the stack's time samples cannot become animation channels, or an
    // empty string when they can.
    [[nodiscard]] static auto get_animation_refusal(const erhe::scene::Xform_op_stack& stack) -> std::string
    {
        using Path = erhe::scene::Animation_path;
        int  previous_order{-1};
        bool has_translation{false};
        bool has_rotation   {false};
        bool has_scale      {false};
        for (const erhe::scene::Xform_op& op : stack.ops) {
            if (op.inverted) {
                return "the stack inverts an op";
            }
            if (!op.suffix.empty()) {
                return "the stack has a suffixed op (a pivot pair)";
            }
            erhe::scene::Animation_path path{Path::INVALID};
            if (!get_xform_op_animation_path(op.type, path)) {
                return "the stack has a matrix op";
            }
            const int order = (path == Path::TRANSLATION) ? 0 : (path == Path::ROTATION) ? 1 : 2;
            if (order < previous_order) {
                return "the ops are not in translate, rotate, scale order";
            }
            previous_order = order;
            bool& seen = (path == Path::TRANSLATION) ? has_translation : (path == Path::ROTATION) ? has_rotation : has_scale;
            if (seen) {
                return "the stack has more than one op of the same kind";
            }
            seen = true;
        }
        return std::string{};
    }

    // The value an op has at one time code: its sample there, the linear
    // interpolation of the two samples around it (a quaternion slerps), the
    // first or last sample outside the sampled range - USD's time sample
    // semantics for a floating-point attribute. A matrix op holds its earlier
    // sample: two matrices do not interpolate componentwise into a transform.
    [[nodiscard]] static auto get_op_value_at(const erhe::scene::Xform_op& op, const double time_code) -> erhe::scene::Xform_op_value
    {
        if (op.samples.empty()) {
            return op.value;
        }
        const erhe::scene::Xform_op_sample* before = nullptr;
        const erhe::scene::Xform_op_sample* after  = nullptr;
        for (const erhe::scene::Xform_op_sample& sample : op.samples) {
            if ((sample.time_code <= time_code) && ((before == nullptr) || (sample.time_code > before->time_code))) {
                before = &sample;
            }
            if ((sample.time_code >= time_code) && ((after == nullptr) || (sample.time_code < after->time_code))) {
                after = &sample;
            }
        }
        if (before == nullptr) {
            return after->value;
        }
        if ((after == nullptr) || (after == before) || (after->time_code <= before->time_code)) {
            return before->value;
        }
        const double t = (time_code - before->time_code) / (after->time_code - before->time_code);
        if (std::holds_alternative<glm::dvec3>(before->value) && std::holds_alternative<glm::dvec3>(after->value)) {
            return glm::mix(std::get<glm::dvec3>(before->value), std::get<glm::dvec3>(after->value), t);
        }
        if (std::holds_alternative<double>(before->value) && std::holds_alternative<double>(after->value)) {
            return std::get<double>(before->value) + (std::get<double>(after->value) - std::get<double>(before->value)) * t;
        }
        if (std::holds_alternative<glm::dquat>(before->value) && std::holds_alternative<glm::dquat>(after->value)) {
            return glm::slerp(std::get<glm::dquat>(before->value), std::get<glm::dquat>(after->value), t);
        }
        return before->value;
    }

    // The rotation one sample of a rotate / orient op holds, as a quaternion.
    [[nodiscard]] static auto get_sample_rotation(
        const erhe::scene::Xform_op&        op,
        const erhe::scene::Xform_op_sample& sample
    ) -> glm::dquat
    {
        if (op.type == erhe::scene::Xform_op_type::orient) {
            return std::get<glm::dquat>(sample.value);
        }
        erhe::scene::Xform_op sample_op{};
        sample_op.type  = op.type;
        sample_op.value = sample.value;
        return glm::quat_cast(glm::dmat3{sample_op.to_matrix()});
    }

    // The file's time-sampled xformOps as one Animation
    // (src/erhe/usd/notes.md, "Time samples"). One channel per sampled op of
    // every prim whose stack the channels can express; a stack they cannot is
    // named in one warning and keeps the transform the import gave it, which
    // is its pose at the evaluation time code.
    // The file's one animation, made when the first channel of it is
    // (src/erhe/usd/notes.md, "Time samples"): a file's sampled `xformOp`s
    // and its `SkelAnimation` joint channels are channels of the same
    // animation, so playing it poses the whole file.
    void ensure_animation(std::shared_ptr<erhe::scene::Animation>& animation)
    {
        if (animation) {
            return;
        }
        const std::string name = m_arguments.path.stem().generic_string();
        animation = std::make_shared<erhe::scene::Animation>(name.empty() ? std::string{"animation"} : name);
        animation->set_source_path(m_arguments.path);
        animation->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    }

    // The erhe animation path of one Tydra channel path, INVALID for the
    // paths erhe drives no joint with (blend-shape weights and the custom
    // property channels, which are future work - plan section 5).
    [[nodiscard]] static auto to_erhe_animation_path(const lightusd::tydra::AnimationPath path) -> erhe::scene::Animation_path
    {
        switch (path) {
            case lightusd::tydra::AnimationPath::Translation: return erhe::scene::Animation_path::TRANSLATION;
            case lightusd::tydra::AnimationPath::Rotation:    return erhe::scene::Animation_path::ROTATION;
            case lightusd::tydra::AnimationPath::Scale:       return erhe::scene::Animation_path::SCALE;
            default:                                          return erhe::scene::Animation_path::INVALID;
        }
    }

    // The joint channels of the file's `SkelAnimation` prims, as channels of
    // the file's animation targeting the joint prims
    // (doc/usd-compatibility-plan.md K1). A joint's animation is node
    // animation in erhe, exactly as it is for a glTF skin, so a skeleton with
    // no animation source leaves its joints at the rest pose - which is the
    // pose UsdSkel gives them too. Tydra keys a skeletal sampler in the
    // file's time codes, so the seconds erhe keys in are the same division
    // the sampled `xformOp`s take.
    void add_skeletal_animation_channels(
        std::shared_ptr<erhe::scene::Animation>& animation,
        const double                             time_codes_per_second
    )
    {
        for (std::size_t clip_index = 0, clip_end = m_scene->animations.size(); clip_index < clip_end; ++clip_index) {
            const lightusd::tydra::AnimationClip& clip = m_scene->animations[clip_index];
            for (const lightusd::tydra::AnimationChannel& channel : clip.channels) {
                if ((channel.target_type != lightusd::tydra::ChannelTargetType::SkeletonJoint) || !channel.is_valid()) {
                    continue;
                }
                const erhe::scene::Animation_path path = to_erhe_animation_path(channel.path);
                if (path == erhe::scene::Animation_path::INVALID) {
                    continue;
                }
                const std::size_t skeleton_index = static_cast<std::size_t>(channel.skeleton_id);
                if (skeleton_index >= m_skeleton_joint_nodes.size()) {
                    continue;
                }
                const std::vector<std::shared_ptr<erhe::scene::Node>>& joint_nodes = m_skeleton_joint_nodes[skeleton_index];
                const std::size_t joint_index = static_cast<std::size_t>(channel.joint_id);
                if ((joint_index >= joint_nodes.size()) || !joint_nodes[joint_index]) {
                    continue;
                }
                const std::size_t sampler_index = static_cast<std::size_t>(channel.sampler);
                if (sampler_index >= clip.samplers.size()) {
                    continue;
                }
                const lightusd::tydra::KeyframeSampler& source          = clip.samplers[sampler_index];
                const std::size_t                       component_count = erhe::scene::get_component_count(path);
                if (source.times.empty() || (source.values.size() < (source.times.size() * component_count))) {
                    continue;
                }

                std::vector<float> timestamps;
                std::vector<float> values;
                timestamps.reserve(source.times.size());
                values.reserve(source.times.size() * component_count);
                glm::vec4 previous_rotation{0.0f, 0.0f, 0.0f, 1.0f};
                bool      has_previous_rotation{false};
                for (std::size_t key = 0; key < source.times.size(); ++key) {
                    timestamps.push_back(static_cast<float>(static_cast<double>(source.times[key]) / time_codes_per_second));
                    if (path == erhe::scene::Animation_path::ROTATION) {
                        // Tydra copies a `quatf` sample as its four floats,
                        // which are the imaginary parts followed by the real
                        // one - the (x, y, z, w) an erhe rotation channel
                        // holds. Keep the sampled quaternions on one
                        // hemisphere so the interpolation never takes the
                        // long way round.
                        glm::vec4 rotation{
                            source.values[(key * 4) + 0],
                            source.values[(key * 4) + 1],
                            source.values[(key * 4) + 2],
                            source.values[(key * 4) + 3]
                        };
                        if (has_previous_rotation && (glm::dot(previous_rotation, rotation) < 0.0f)) {
                            rotation = -rotation;
                        }
                        previous_rotation     = rotation;
                        has_previous_rotation = true;
                        values.push_back(rotation.x);
                        values.push_back(rotation.y);
                        values.push_back(rotation.z);
                        values.push_back(rotation.w);
                    } else {
                        for (std::size_t component = 0; component < component_count; ++component) {
                            values.push_back(source.values[(key * component_count) + component]);
                        }
                    }
                }

                ensure_animation(animation);
                erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
                sampler.set(std::move(timestamps), std::move(values));
                animation->samplers.push_back(std::move(sampler));
                animation->channels.push_back(
                    erhe::scene::Animation_channel{
                        .path           = path,
                        .sampler_index  = animation->samplers.size() - 1,
                        .target         = joint_nodes[joint_index],
                        .start_position = 0,
                        .value_offset   = 0
                    }
                );
            }
        }
    }

    // A sampled stack whose ops are not one translate, one rotate and one
    // scale in that order - `[orient, translate]`, a pivot pair, a sampled
    // matrix op, two ops of a kind - cannot be driven op by op, because a
    // channel writes one TRS component of the target. Its transform is baked
    // instead: at the union of the ops' sample time codes the whole stack is
    // posed and composed, the matrix is decomposed into translation, rotation
    // and scale, and those become the three channels. Exact at every sample,
    // linear between them, the way the per-op channels are; the samples stay
    // on the ops, so a save writes the authored stack back unchanged.
    void bake_stack_animation(
        const std::shared_ptr<erhe::scene::Node>& node,
        const erhe::scene::Xform_op_stack&        stack,
        const double                              time_codes_per_second,
        const std::string&                        reason,
        std::shared_ptr<erhe::scene::Animation>&  animation
    )
    {
        std::vector<double> time_codes;
        for (const erhe::scene::Xform_op& op : stack.ops) {
            for (const erhe::scene::Xform_op_sample& sample : op.samples) {
                time_codes.push_back(sample.time_code);
            }
        }
        std::sort(time_codes.begin(), time_codes.end());
        time_codes.erase(std::unique(time_codes.begin(), time_codes.end()), time_codes.end());
        if (time_codes.empty()) {
            return;
        }
        std::vector<float> timestamps;
        std::vector<float> translations;
        std::vector<float> rotations;
        std::vector<float> scales;
        timestamps  .reserve(time_codes.size());
        translations.reserve(time_codes.size() * 3);
        rotations   .reserve(time_codes.size() * 4);
        scales      .reserve(time_codes.size() * 3);
        glm::quat previous_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool      has_previous_rotation{false};
        erhe::scene::Xform_op_stack posed = stack;
        for (const double time_code : time_codes) {
            for (std::size_t i = 0; i < stack.ops.size(); ++i) {
                posed.ops[i].value = get_op_value_at(stack.ops[i], time_code);
            }
            const glm::mat4 matrix = glm::mat4{posed.compose()};
            glm::vec3 scale      {1.0f};
            glm::quat rotation   {1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 translation{0.0f};
            glm::vec3 skew       {0.0f};
            glm::vec4 perspective{0.0f};
            glm::decompose(matrix, scale, rotation, translation, skew, perspective);
            if (has_previous_rotation && (glm::dot(previous_rotation, rotation) < 0.0f)) {
                rotation = -rotation;
            }
            previous_rotation     = rotation;
            has_previous_rotation = true;
            timestamps.push_back(static_cast<float>(time_code / time_codes_per_second));
            translations.insert(translations.end(), {translation.x, translation.y, translation.z});
            rotations   .insert(rotations   .end(), {rotation.x, rotation.y, rotation.z, rotation.w});
            scales      .insert(scales      .end(), {scale.x, scale.y, scale.z});
        }
        ensure_animation(animation);
        const auto add_channel = [&](const erhe::scene::Animation_path path, std::vector<float>&& values) {
            erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
            sampler.set(std::vector<float>{timestamps}, std::move(values));
            animation->samplers.push_back(std::move(sampler));
            animation->channels.push_back(
                erhe::scene::Animation_channel{
                    .path           = path,
                    .sampler_index  = animation->samplers.size() - 1,
                    .target         = node,
                    .start_position = 0,
                    .value_offset   = 0
                }
            );
        };
        add_channel(erhe::scene::Animation_path::TRANSLATION, std::move(translations));
        add_channel(erhe::scene::Animation_path::ROTATION,    std::move(rotations));
        add_channel(erhe::scene::Animation_path::SCALE,       std::move(scales));
        log_usd->info(
            "USD prim '{}': {} - its time-sampled transform is baked into translate, rotate, scale channels at {} sample time code(s)",
            node->get_name(), reason, time_codes.size()
        );
    }

    void build_animation()
    {
        const double time_codes_per_second = (m_result.data.time_codes.time_codes_per_second > 0.0)
            ? m_result.data.time_codes.time_codes_per_second
            : 24.0;
        std::shared_ptr<erhe::scene::Animation> animation;
        for (const std::shared_ptr<erhe::scene::Node>& node : m_result.data.nodes) {
            if (!node) {
                continue;
            }
            const erhe::scene::Xform_op_stack* stack = node->get_xform_op_stack();
            if ((stack == nullptr) || !stack->has_time_samples()) {
                continue;
            }
            const std::string refusal = get_animation_refusal(*stack);
            if (!refusal.empty()) {
                bake_stack_animation(node, *stack, time_codes_per_second, refusal, animation);
                continue;
            }
            for (const erhe::scene::Xform_op& op : stack->ops) {
                if (op.samples.empty()) {
                    continue;
                }
                erhe::scene::Animation_path path{erhe::scene::Animation_path::INVALID};
                if (!get_xform_op_animation_path(op.type, path)) {
                    continue;
                }
                ensure_animation(animation);
                const std::size_t  component_count = erhe::scene::get_component_count(path);
                std::vector<float> timestamps;
                std::vector<float> values;
                timestamps.reserve(op.samples.size());
                values.reserve(op.samples.size() * component_count);
                glm::dquat previous_rotation{1.0, 0.0, 0.0, 0.0};
                bool       has_previous_rotation{false};
                for (const erhe::scene::Xform_op_sample& sample : op.samples) {
                    timestamps.push_back(static_cast<float>(sample.time_code / time_codes_per_second));
                    if (path == erhe::scene::Animation_path::ROTATION) {
                        glm::dquat rotation = get_sample_rotation(op, sample);
                        // Keep the sampled quaternions on one hemisphere: a
                        // sign flip between two Euler samples would otherwise
                        // make the interpolation take the long way round.
                        if (has_previous_rotation && (glm::dot(previous_rotation, rotation) < 0.0)) {
                            rotation = -rotation;
                        }
                        previous_rotation     = rotation;
                        has_previous_rotation = true;
                        values.push_back(static_cast<float>(rotation.x));
                        values.push_back(static_cast<float>(rotation.y));
                        values.push_back(static_cast<float>(rotation.z));
                        values.push_back(static_cast<float>(rotation.w));
                    } else {
                        const glm::dvec3 vector = std::get<glm::dvec3>(sample.value);
                        values.push_back(static_cast<float>(vector.x));
                        values.push_back(static_cast<float>(vector.y));
                        values.push_back(static_cast<float>(vector.z));
                    }
                }
                // USD interpolates time samples of a floating-point attribute
                // linearly, and authors no per-attribute interpolation for a
                // reader to pick another one from.
                erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
                sampler.set(std::move(timestamps), std::move(values));
                animation->samplers.push_back(std::move(sampler));
                animation->channels.push_back(
                    erhe::scene::Animation_channel{
                        .path           = path,
                        .sampler_index  = animation->samplers.size() - 1,
                        .target         = node,
                        .start_position = 0,
                        .value_offset   = 0
                    }
                );
            }
        }
        add_skeletal_animation_channels(animation, time_codes_per_second);
        if (!animation) {
            return;
        }
        animation->notify_keyframes_changed();
        log_usd->info(
            "USD '{}': {} time-sampled transform channel(s) as animation '{}'",
            m_arguments.path.generic_string(), animation->channels.size(), animation->get_name()
        );
        m_result.data.animations.push_back(std::move(animation));
    }

    // The root layer's `customLayerData`, string entries only: an erhe save
    // puts the editor's scene state there (doc/scene_serialization.md, USD-
    // backed scenes), and a value of any other type belongs to a writer this
    // conversion does not read.
    void read_custom_layer_data(const lightusd::Stage& stage)
    {
        for (const std::pair<const std::string, lightusd::MetaVariable>& entry : stage.metas().customLayerData) {
            const nonstd::optional<std::string> text = entry.second.get_value<std::string>();
            if (text.has_value()) {
                m_result.data.custom_layer_data.emplace(entry.first, text.value());
                continue;
            }
            // A value the ASCII parser read arrives as StringData - the type
            // that also carries which quote form the layer spelled - while a
            // value a writer put there is a plain std::string. Both are the
            // same string entry to a reader.
            const nonstd::optional<lightusd::value::StringData> string_data =
                entry.second.get_value<lightusd::value::StringData>();
            if (string_data.has_value()) {
                m_result.data.custom_layer_data.emplace(entry.first, string_data.value().value);
            }
        }
    }

    // UsdPhysics is future work (doc/usd-compatibility-plan.md section 5).
    // Say so once per file rather than silently dropping the schemas.
    void report_skipped_physics(const lightusd::Stage& stage)
    {
        std::size_t physics_prim_count = 0;
        for (const lightusd::Prim& prim : stage.root_prims()) {
            count_physics_prims(prim, physics_prim_count);
        }
        if (physics_prim_count > 0) {
            log_usd->info(
                "USD '{}': {} prim(s) carry UsdPhysics schemas - physics is not imported",
                m_arguments.path.generic_string(),
                physics_prim_count
            );
        }
    }

    void count_physics_prims(const lightusd::Prim& prim, std::size_t& count)
    {
        const std::string& type_name = prim.type_name();
        if (type_name.rfind("Physics", 0) == 0) {
            ++count;
        } else if (prim.metas().has_apiSchemas()) {
            const lightusd::APISchemas api_schemas = prim.metas().get_apiSchemas();
            for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
                if (is_physics_api_schema(entry.first)) {
                    ++count;
                    break;
                }
            }
        }
        for (const lightusd::Prim& child : prim.children()) {
            count_physics_prims(child, count);
        }
    }

    [[nodiscard]] static auto is_physics_api_schema(const lightusd::APISchemas::APIName name) -> bool
    {
        switch (name) {
            case lightusd::APISchemas::APIName::PhysicsRigidBodyAPI:
            case lightusd::APISchemas::APIName::PhysicsCollisionAPI:
            case lightusd::APISchemas::APIName::PhysicsMaterialAPI:
            case lightusd::APISchemas::APIName::PhysicsMeshCollisionAPI:
            case lightusd::APISchemas::APIName::PhysicsMassAPI:
            case lightusd::APISchemas::APIName::PhysicsFilteredPairsAPI:
            case lightusd::APISchemas::APIName::PhysicsArticulationRootAPI:
            case lightusd::APISchemas::APIName::PhysicsDriveAPI:
            case lightusd::APISchemas::APIName::PhysicsLimitAPI:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] auto find_prim(const std::string& absolute_path) const -> const lightusd::Prim*
    {
        if (m_stage == nullptr) {
            return nullptr;
        }
        const lightusd::Prim* prim = nullptr;
        std::string           error;
        if (!m_stage->find_prim_at_path(lightusd::Path{absolute_path, ""}, prim, &error)) {
            return nullptr;
        }
        return prim;
    }

    // The Xformable face of a prim, for the prim classes the conversion gives
    // a transform to. LightUSD's concrete prim classes carry the xformOps, so
    // the one the prim is has to be asked for by name, the way
    // get_visibility_and_purpose asks for the two attributes it reads.
    template <typename T>
    [[nodiscard]] static auto as_xformable(const lightusd::Prim& prim) -> const lightusd::Xformable*
    {
        return prim.as<T>();
    }

    [[nodiscard]] static auto get_xformable(const lightusd::Prim& prim) -> const lightusd::Xformable*
    {
        const lightusd::Xformable* result = nullptr;
        if ((result = as_xformable<lightusd::Xform        >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomMesh     >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCamera   >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::SphereLight  >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::DistantLight >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::RectLight    >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::DiskLight    >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::CylinderLight>(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCube      >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomSphere    >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCone      >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCylinder  >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCylinder_1>(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCapsule   >(prim)) != nullptr) { return result; }
        if ((result = as_xformable<lightusd::GeomCapsule_1 >(prim)) != nullptr) { return result; }
        return nullptr;
    }

    // The prim's authored xformOp stack (doc/usd-compatibility-plan.md M8).
    // Tydra composes the ops into one local matrix, so the stack is only on
    // the raw prim; reading it is what lets the prim be written back with the
    // ops it was authored with. A prim that authored no ops answers with an
    // empty stack, which composes to identity and exports as no xformOps at
    // all. False means the stack is not readable - the prim is not in the
    // stage, its class carries no xformOps, or an op uses a type erhe has no
    // counterpart for - and the caller keeps the composed matrix instead.
    [[nodiscard]] auto read_xform_op_stack(const std::string& absolute_path, erhe::scene::Xform_op_stack& out_stack) -> bool
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return false;
        }
        const lightusd::Xformable* xformable = get_xformable(*prim);
        std::vector<lightusd::XformOp> model_ops;
        if (xformable == nullptr) {
            // A typeless prim - the `Model` LightUSD reconstructs for
            // `def "name"` and `over "name"` - keeps every attribute as a raw
            // property: LightUSD builds no xformOps for it and Tydra evaluates
            // no transform for it. USD gives such a prim its type through the
            // arc it authors, and its own xformOps apply on top of the composed
            // target, so the raw `xformOp:*` properties are reconstructed here
            // the way an `over`'s are (read_override_xform_ops).
            const lightusd::Model* model = prim->as<lightusd::Model>();
            if ((model == nullptr) || (model->props.find("xformOpOrder") == model->props.end())) {
                return false;
            }
            std::map<std::string, lightusd::Property> properties = model->props;
            std::set<std::string>                     table;
            std::string                               error;
            if (!lightusd::prim::ReconstructXformOpsFromProperties(model->spec, table, properties, &model_ops, &error)) {
                log_usd->warn("USD prim '{}': the typeless prim has unreadable xformOps: {}", absolute_path, error);
                return false;
            }
        }
        const std::vector<lightusd::XformOp>& ops = (xformable != nullptr) ? xformable->xformOps : model_ops;
        const double time_code = m_import_time_code;
        for (const lightusd::XformOp& usd_op : ops) {
            if (usd_op.op_type == lightusd::XformOp::OpType::ResetXformStack) {
                out_stack.reset_xform_stack = true;
                continue;
            }
            erhe::scene::Xform_op op{};
            if (!read_xform_op(usd_op, time_code, op)) {
                log_usd->warn(
                    "USD prim '{}': xformOp of value type '{}' has no erhe counterpart - the prim keeps the composed transform",
                    absolute_path,
                    usd_op.get_value_type_name()
                );
                return false;
            }
            out_stack.ops.push_back(std::move(op));
        }
        return true;
    }

    // Whether the render-scene conversion evaluated a transform for the prim.
    // Tydra composes a local matrix only for the prim types it carries; for a
    // prim it does not know - the `_1` primitive-schema variants, whose
    // meshes erhe builds itself (S1) - it reports the identity, so the
    // authored xformOp stack is the prim's transform and there is nothing to
    // compare it against.
    enum class Composed_transform
    {
        evaluated,
        unevaluated
    };

    // The local transform of an imported prim: its authored stack when it has
    // a readable one, else the matrix Tydra composed. `extra_transform` is the
    // stage's upAxis / metersPerUnit correction (make_stage_transform), which
    // reaches the top-level prims of a non-Y-up or non-metre stage; a prim it
    // applies to writes a transform that is not what its ops say, so it keeps
    // the composed matrix and no stack.
    void apply_local_transform(
        erhe::scene::Node&       node,
        const Tydra_node&        usd_node,
        const glm::mat4&         extra_transform,
        const Composed_transform composed_transform
    )
    {
        const glm::mat4 composed = to_glm(usd_node.local_matrix);
        if (is_identity_matrix(extra_transform)) {
            erhe::scene::Xform_op_stack stack{};
            if (read_xform_op_stack(usd_node.abs_path, stack)) {
                const glm::mat4 stack_matrix{stack.compose()};
                // A time-sampled stack is the prim's transform whatever the
                // render scene composed: LightUSD evaluates an op that carries
                // both a `default` and time samples at its default, while USD
                // says the samples win at any time code the samples reach
                // (src/erhe/usd/notes.md, "Time samples").
                if (
                    (composed_transform == Composed_transform::unevaluated) ||
                    stack.has_time_samples() ||
                    is_near_matrix(stack_matrix, composed)
                ) {
                    node.set_xform_op_stack(std::move(stack));
                    return;
                }
                log_usd->warn(
                    "USD prim '{}': the authored xformOp stack does not compose to the transform the stage evaluates - the prim keeps the composed transform",
                    usd_node.abs_path
                );
            }
        } else {
            log_usd->debug(
                "USD prim '{}': the stage up-axis / units correction applies to it, so it carries no authored xformOp stack",
                usd_node.abs_path
            );
        }
        node.node_data.transforms.parent_from_node.set(extra_transform * composed);
    }

    [[nodiscard]] auto has_api_schema(const std::string& absolute_path, const lightusd::APISchemas::APIName name) const -> bool
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if ((prim == nullptr) || !prim->metas().has_apiSchemas()) {
            return false;
        }
        const lightusd::APISchemas api_schemas = prim->metas().get_apiSchemas();
        for (const std::pair<lightusd::APISchemas::APIName, std::string>& entry : api_schemas.names) {
            if (entry.first == name) {
                return true;
            }
        }
        return false;
    }

    // The authored opinions of one prim (doc/usd-compatibility-plan.md I2).
    // Tydra's render scene reports a schema fallback exactly the way it
    // reports an authored opinion, so the composed prim is the only place
    // the two can be told apart: LightUSD's typed attribute wrappers answer
    // `authored()`, and Tydra's GetPropertyNames collects the names of the
    // builtin attributes that answer true plus every custom attribute the
    // prim carries. An opinion that arrives over a reference or a sublayer
    // is authored on the composed prim, so it is in this set. The set is
    // built once per prim: the conversion asks about several attributes of
    // the same prim.
    [[nodiscard]] auto authored_property_names(const std::string& absolute_path) -> const std::set<std::string>&
    {
        const std::map<std::string, std::set<std::string>>::iterator cached = m_authored_property_names.find(absolute_path);
        if (cached != m_authored_property_names.end()) {
            return cached->second;
        }
        std::set<std::string> names;
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim != nullptr) {
            std::vector<std::string> name_list;
            std::string              error;
            if (lightusd::tydra::GetPropertyNames(*prim, &name_list, &error)) {
                for (std::string& name : name_list) {
                    names.insert(std::move(name));
                }
                read_open_pbr_property_names(*prim, names);
            } else if (!read_gprim_property_names(*prim, names) && !error.empty()) {
                log_usd->warn("USD prim '{}': {}", absolute_path, error);
            }
        }
        return m_authored_property_names.emplace(absolute_path, std::move(names)).first->second;
    }

    // The authored property names of one GPrim-derived prim, read from the
    // prim itself: the attributes every GPrim carries, and the custom
    // properties of its `props` map. Tydra's GetPropertyNames knows a fixed
    // set of prim types and answers "TODO: Prim type <name>" for the UsdGeom
    // primitive schemas (S1), so this is what lets a `Cube` prim's
    // `visibility`, `purpose` and `erhe:` attributes be read at all.
    template <typename T>
    [[nodiscard]] static auto read_typed_gprim_property_names(const lightusd::Prim& prim, std::set<std::string>& names) -> bool
    {
        const T* typed = prim.as<T>();
        if (typed == nullptr) {
            return false;
        }
        if (typed->visibility.authored())  { names.insert("visibility");  }
        if (typed->purpose.authored())     { names.insert("purpose");     }
        if (typed->doubleSided.authored()) { names.insert("doubleSided"); }
        if (typed->orientation.authored()) { names.insert("orientation"); }
        if (typed->extent.authored())      { names.insert("extent");      }
        for (const std::pair<const std::string, lightusd::Property>& property : typed->props) {
            names.insert(property.first);
        }
        return true;
    }

    // Tydra's warning text, minus the lines that report something erhe reads
    // for itself. A `PointInstancer` prototype "resolved to no RenderMesh"
    // says the render-scene conversion did not expand the instancer, which is
    // exactly what erhe does not want it to do: the expansion is
    // convert_point_instancer's (S1), off the raw prim.
    [[nodiscard]] auto filter_converter_warning(const std::string& warning) -> std::string
    {
        std::string       filtered;
        std::size_t       start = 0;
        while (start < warning.size()) {
            const std::size_t      end  = warning.find('\n', start);
            const std::string_view line = (end == std::string::npos)
                ? std::string_view{warning}.substr(start)
                : std::string_view{warning}.substr(start, end - start);
            start = (end == std::string::npos) ? warning.size() : (end + 1);
            if (
                (line.find("PointInstancer") != std::string_view::npos) &&
                (line.find("resolved to no RenderMesh") != std::string_view::npos)
            ) {
                continue;
            }
            if (line.empty() && filtered.empty()) {
                continue;
            }
            filtered.append(line);
            filtered.push_back('\n');
        }
        while (!filtered.empty() && (filtered.back() == '\n')) {
            filtered.pop_back();
        }
        return filtered;
    }

    [[nodiscard]] static auto read_gprim_property_names(const lightusd::Prim& prim, std::set<std::string>& names) -> bool
    {
        return
            read_typed_gprim_property_names<lightusd::GeomCube      >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomSphere    >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCone      >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCylinder  >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCylinder_1>(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCapsule   >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCapsule_1 >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomPointInstancer>(prim, names);
    }

    // The authored input names of an OpenPBR surface Shader prim. Tydra's
    // GetPropertyNames answers a Shader's `info:id` and its custom properties,
    // and reaches the typed inputs of a UsdPreviewSurface only: an OpenPBR
    // network's inputs live in the struct LightUSD parsed them into, so the
    // ones erhe reads are asked for by name the way
    // read_typed_gprim_property_names asks a GPrim's. Each is added under the
    // OpenPBR spelling `apply_open_pbr_surface` asks about, so an Autodesk
    // Standard Surface - the same knobs under other names, which Tydra
    // converts into the same shader - answers the same questions.
    static void read_open_pbr_property_names(const lightusd::Prim& prim, std::set<std::string>& names)
    {
        const lightusd::Shader* shader = prim.as<lightusd::Shader>();
        if (shader == nullptr) {
            return;
        }
        const auto add = [&names](const bool authored, const char* name)
        {
            if (authored) {
                names.insert(std::string{"inputs:"} + name);
            }
        };
        const lightusd::MtlxOpenPBRSurface* mtlx_open_pbr = shader->value.as<lightusd::MtlxOpenPBRSurface>();
        if (mtlx_open_pbr != nullptr) {
            add(mtlx_open_pbr->base_color                   .authored(), "base_color");
            add(mtlx_open_pbr->base_metalness               .authored(), "base_metalness");
            add(mtlx_open_pbr->specular_roughness           .authored(), "specular_roughness");
            add(mtlx_open_pbr->specular_roughness_anisotropy.authored(), "specular_roughness_anisotropy");
            add(mtlx_open_pbr->specular_anisotropy          .authored(), "specular_anisotropy");
            add(mtlx_open_pbr->specular_ior                 .authored(), "specular_ior");
            add(mtlx_open_pbr->transmission_weight          .authored(), "transmission_weight");
            add(mtlx_open_pbr->emission_luminance           .authored(), "emission_luminance");
            add(mtlx_open_pbr->emission_color               .authored(), "emission_color");
            add(mtlx_open_pbr->geometry_opacity             .authored(), "geometry_opacity");
            return;
        }
        const lightusd::OpenPBRSurface* open_pbr = shader->value.as<lightusd::OpenPBRSurface>();
        if (open_pbr != nullptr) {
            add(open_pbr->base_color                   .authored(), "base_color");
            add(open_pbr->base_metalness               .authored(), "base_metalness");
            add(open_pbr->specular_roughness           .authored(), "specular_roughness");
            add(open_pbr->specular_roughness_anisotropy.authored(), "specular_roughness_anisotropy");
            add(open_pbr->specular_anisotropy          .authored(), "specular_anisotropy");
            add(open_pbr->specular_ior                 .authored(), "specular_ior");
            add(open_pbr->transmission_weight          .authored(), "transmission_weight");
            add(open_pbr->emission_luminance           .authored(), "emission_luminance");
            add(open_pbr->emission_color               .authored(), "emission_color");
            add(open_pbr->opacity                      .authored(), "opacity");
            return;
        }
        const lightusd::MtlxAutodeskStandardSurface* standard = shader->value.as<lightusd::MtlxAutodeskStandardSurface>();
        if (standard != nullptr) {
            add(standard->base_color         .authored(), "base_color");
            add(standard->metalness          .authored(), "base_metalness");
            add(standard->specular_roughness .authored(), "specular_roughness");
            add(standard->specular_anisotropy.authored(), "specular_anisotropy");
            add(standard->specular_IOR       .authored(), "specular_ior");
            add(standard->transmission       .authored(), "transmission_weight");
            add(standard->emission           .authored(), "emission_luminance");
            add(standard->emission_color     .authored(), "emission_color");
            add(standard->opacity            .authored(), "opacity");
        }
    }

    [[nodiscard]] auto is_authored(const std::string& absolute_path, const std::string& name) -> bool
    {
        const std::set<std::string>& names = authored_property_names(absolute_path);
        return names.find(name) != names.end();
    }

    // `visibility` and `purpose` of the composed prim. Tydra's GetProperty
    // reaches a prim's own schema attributes and its custom ones, but not
    // the ones a prim inherits from GPrim or from LightAPI, so these two
    // are read from the concrete prim class. Every prim type the conversion
    // makes a node for is listed: the GPrim-derived geometry, point and
    // curve types, the camera, the UsdSkel types, and the UsdLux types -
    // the last two carry their own copies of the two attributes rather than
    // deriving from GPrim.
    template <typename T>
    [[nodiscard]] static auto read_visibility_and_purpose(
        const lightusd::Prim&  prim,
        lightusd::Visibility&  visibility,
        lightusd::Purpose&     purpose
    ) -> bool
    {
        const T* typed = prim.as<T>();
        if (typed == nullptr) {
            return false;
        }
        lightusd::Visibility           scalar_visibility = lightusd::Visibility::Inherited;
        const lightusd::Animatable<lightusd::Visibility>& animatable = typed->visibility.get_value();
        if (animatable.get_default(&scalar_visibility)) {
            visibility = scalar_visibility;
        }
        purpose = typed->purpose.get_value();
        return true;
    }

    [[nodiscard]] static auto get_visibility_and_purpose(
        const lightusd::Prim& prim,
        lightusd::Visibility& visibility,
        lightusd::Purpose&    purpose
    ) -> bool
    {
        return
            read_visibility_and_purpose<lightusd::Xform        >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::Scope        >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomMesh     >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCamera   >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::SphereLight  >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::DistantLight >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::RectLight    >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::DiskLight    >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::CylinderLight>(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCube      >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomSphere    >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCone      >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCylinder  >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCylinder_1>(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCapsule   >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomCapsule_1 >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomPoints        >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomBasisCurves   >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::GeomPointInstancer>(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::SkelRoot          >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::Skeleton          >(prim, visibility, purpose) ||
            read_visibility_and_purpose<lightusd::DomeLight         >(prim, visibility, purpose);
    }

    // `doubleSided` of the composed prim. Like `visibility` and `purpose` it
    // is a GPrim attribute rather than one of the concrete schema, so it is
    // read from the concrete prim class; every GPrim-derived type the
    // conversion makes geometry for is listed.
    template <typename T>
    [[nodiscard]] static auto read_double_sided(const lightusd::Prim& prim, bool& double_sided) -> bool
    {
        const T* typed = prim.as<T>();
        if (typed == nullptr) {
            return false;
        }
        double_sided = typed->doubleSided.get_value();
        return true;
    }

    [[nodiscard]] static auto get_double_sided(const lightusd::Prim& prim, bool& double_sided) -> bool
    {
        return
            read_double_sided<lightusd::GeomMesh     >(prim, double_sided) ||
            read_double_sided<lightusd::GeomCube     >(prim, double_sided) ||
            read_double_sided<lightusd::GeomSphere   >(prim, double_sided) ||
            read_double_sided<lightusd::GeomCone     >(prim, double_sided) ||
            read_double_sided<lightusd::GeomCylinder >(prim, double_sided) ||
            read_double_sided<lightusd::GeomCylinder_1>(prim, double_sided) ||
            read_double_sided<lightusd::GeomCapsule  >(prim, double_sided) ||
            read_double_sided<lightusd::GeomCapsule_1>(prim, double_sided);
    }

    // `doubleSided` (doc/usd_compatibility.md, geometry attributes) lands on
    // the geometry prim's own `Gprim.double_sided` property. An unauthored
    // attribute writes nothing, so the property keeps its default; the erhe
    // default and the USD fallback are both false, so a file that authors
    // none reads back as none (M4).
    void apply_double_sided(const std::string& absolute_path, erhe::Item_base& item)
    {
        erhe::scene::Gprim* gprim = dynamic_cast<erhe::scene::Gprim*>(&item);
        if (gprim == nullptr) {
            return;
        }
        if (!is_authored(absolute_path, "doubleSided")) {
            return;
        }
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        bool double_sided = false;
        if (!get_double_sided(*prim, double_sided)) {
            log_usd->warn("USD prim '{}': doubleSided is not readable from a '{}' prim", absolute_path, prim->type_name());
            return;
        }
        gprim->set_double_sided(double_sided);
    }

    // `visibility` and `purpose` land on the erhe item properties that carry
    // the same vocabulary (doc/usd-compatibility-plan.md M3). An unauthored
    // attribute writes nothing, so `visible` keeps its default and `purpose`
    // keeps the per-object default its flag bits derive (D31).
    void apply_visibility_and_purpose(const std::string& absolute_path, erhe::Item_base& item)
    {
        const bool visibility_authored = is_authored(absolute_path, "visibility");
        const bool purpose_authored    = is_authored(absolute_path, "purpose");
        if (!visibility_authored && !purpose_authored) {
            return;
        }
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        lightusd::Visibility visibility = lightusd::Visibility::Inherited;
        lightusd::Purpose    purpose    = lightusd::Purpose::Default;
        if (!get_visibility_and_purpose(*prim, visibility, purpose)) {
            log_usd->warn("USD prim '{}': visibility and purpose are not readable from a '{}' prim", absolute_path, prim->type_name());
            return;
        }
        if (visibility_authored) {
            item.set_value(erhe::Item_base::visible_property, visibility == lightusd::Visibility::Inherited);
        }
        if (purpose_authored) {
            item.set_value(erhe::Item_base::purpose_property, to_erhe_purpose(purpose));
        }
    }

    // The `active` prim metadatum (doc/usd-compatibility-plan.md X2). It is
    // metadata rather than an attribute, so `has_active()` is what says it
    // was authored; a prim that authors none leaves the item's `active`
    // property at its default. The Tydra render-scene conversion the
    // importer walks does not prune inactive prims (unlike Prim::IsActive's
    // traversal note), so an inactive prim still becomes an item - which is
    // what lets `active = false` survive a round trip.
    void apply_active(const std::string& absolute_path, erhe::Item_base& item)
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        if (!prim->metas().has_active()) {
            return;
        }
        item.set_value(erhe::Item_base::active_property, prim->metas().get_active());
    }

    // The prim's composed specifier (doc/usd-compatibility-plan.md X2). A
    // prim that no layer defines keeps `over`, and USD's default traversal
    // predicate requires a defined prim - so Hydra renders neither it nor
    // anything below it, `def` descendants included, while the prim still
    // exists on the stage and is a valid reference target. erhe keeps the
    // item for exactly that reason (references into it must resolve, and its
    // opinions must survive the round trip) and records the specifier as
    // `defined = false`; the derived Item_flags::active bit then prunes the
    // item and its subtree the way the traversal does. A `class` prim is a
    // Style item (X3) and never reaches this.
    //
    // The specifier is read from the composed layer's prim spec: LightUSD's
    // LayerToStage copies the specifier into the typed prim struct only and
    // leaves Prim::specifier() at Specifier::Invalid, so the stage prim
    // cannot answer this (the writer lowers the typed struct's field for the
    // same reason, see apply_defined_specifier in usd_export.cpp).
    //
    // A prim that authors a `references` or `payload` arc is defined by the
    // arc's target: `over "x" (references = @file.usda@)` composes to a
    // defined prim whenever the target is a `def`, which is the case an arc
    // exists for (a target that does not resolve is reported by the arc's
    // resolution, not here). LightUSD composes no arcs, so the layer spec
    // still says `over`; such a prim keeps `defined` at its default.
    void apply_defined(const std::string& absolute_path, erhe::Item_base& item)
    {
        const lightusd::PrimSpec* spec = find_layer_primspec(absolute_path);
        if (spec == nullptr) {
            return;
        }
        if (spec->specifier() != lightusd::Specifier::Over) {
            return;
        }
        if (!read_prim_references(absolute_path).empty()) {
            return;
        }
        item.set_value(erhe::Item_base::defined_property, false);
    }

    // A namespaced custom attribute `erhe:<Owner>:<name>` is an erhe property
    // value addressed by its qualified name (doc/property-system.md D30). USD
    // separates namespace components with `:` and reserves `.` for the
    // property separator of a path, so the erhe qualified name `Owner.name`
    // is spelled `erhe:Owner:name` on a prim (doc/usd_compatibility.md,
    // property system); `erhe:<name>` without a namespace component names a
    // property of the item's own class. The registry resolves the name
    // against each item the prim maps to - the attachment the prim's type
    // made first, then the node that carries it, which is what lets
    // `erhe:Light:color` name either the light itself or a node-held
    // attachment value. The USDA literal is converted to the property's text
    // form and parsed with the D16 `from_string` of the property's type. A
    // name that resolves to no property, and a value that fails to parse or
    // to validate, cost one warning each and are skipped.
    // The erhe property one `erhe:` attribute name addresses, and the object
    // that holds it. Each candidate object is asked in turn - for a scene
    // graph prim the attachment its type made, then the node that carries
    // it; for a Material prim the material alone - and for each,
    // `Owner.name` resolves either the way an editor holder addresses it (an
    // attached property, R7, or a secondary property the object holds for
    // another class, D30) or as `name` on an object of exactly the class
    // `Owner` names. Asking one object both ways before moving on is what
    // sends `Light.temperature` on a light prim to the light itself, and
    // `Light.color` on a plain Xform prim to the node that holds it.
    [[nodiscard]] static auto resolve_erhe_property(
        erhe::property::Dependency_object*  primary,
        erhe::property::Dependency_object*  secondary,
        const std::string&                  qualified_name,
        erhe::property::Dependency_object*& out_target
    ) -> const erhe::property::Dependency_property*
    {
        const erhe::property::Property_registry&        registry    = erhe::property::Property_registry::get();
        const std::size_t                               dot         = qualified_name.find('.');
        const std::string                               bare_name   = (dot == std::string::npos) ? qualified_name : qualified_name.substr(dot + 1);
        const std::optional<erhe::property::Owner_type> named_owner = (dot == std::string::npos)
            ? std::optional<erhe::property::Owner_type>{}
            : registry.find_owner_type(std::string_view{qualified_name}.substr(0, dot));
        if ((dot != std::string::npos) && !named_owner.has_value()) {
            return nullptr;
        }
        erhe::property::Dependency_object* const objects[2] = {primary, secondary};
        for (erhe::property::Dependency_object* const object : objects) {
            if (object == nullptr) {
                continue;
            }
            const erhe::property::Dependency_property* property = registry.find_for_object(*object, qualified_name);
            if (property == nullptr) {
                property = registry.find_for_object(*object, bare_name);
                if ((property != nullptr) && named_owner.has_value() && (property->get_owner_type() != named_owner.value())) {
                    property = nullptr;
                }
            }
            if (property != nullptr) {
                out_target = object;
                return property;
            }
        }
        return nullptr;
    }

    void apply_erhe_custom_attributes(
        const std::string&                 absolute_path,
        erhe::property::Dependency_object* primary,
        erhe::property::Dependency_object* secondary
    )
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return;
        }
        static constexpr std::string_view prefix{"erhe:"};
        const std::set<std::string>& names = authored_property_names(absolute_path);
        for (const std::string& name : names) {
            if (name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            // USD namespace form to erhe qualified form: the first `:`
            // after the `erhe` component separates owner from property.
            std::string       qualified_name = name.substr(prefix.size());
            const std::size_t separator      = qualified_name.find(':');
            if (separator != std::string::npos) {
                qualified_name[separator] = '.';
            }
            erhe::property::Dependency_object*         target   = nullptr;
            const erhe::property::Dependency_property* property = resolve_erhe_property(primary, secondary, qualified_name, target);
            if (property == nullptr) {
                log_usd->warn("USD prim '{}': custom attribute '{}' names no erhe property", absolute_path, name);
                continue;
            }
            if (property->is_read_only()) {
                log_usd->warn("USD prim '{}': erhe property '{}' is read-only", absolute_path, qualified_name);
                continue;
            }
            if (property->get_type() == erhe::property::Property_type::object) {
                log_usd->warn("USD prim '{}': erhe property '{}' is an object reference, which a custom attribute cannot name", absolute_path, qualified_name);
                continue;
            }
            lightusd::Attribute attribute;
            std::string         error;
            if (!lightusd::tydra::GetAttribute(*prim, name, &attribute, &error)) {
                log_usd->warn("USD prim '{}': custom attribute '{}' has no value: {}", absolute_path, name, error);
                continue;
            }
            const std::string                literal    = lightusd::value::pprint_value(attribute.get_var().value_raw());
            const std::optional<std::string> asset_path = usd_asset_literal_path(literal);
            const std::string                text       = asset_path.has_value() ? asset_path.value() : usd_literal_to_property_text(literal);
            const std::optional<erhe::property::Property_value> value = erhe::property::parse_value(*property, text);
            if (!value.has_value()) {
                log_usd->warn("USD prim '{}': custom attribute '{}' value '{}' is not a valid {}", absolute_path, name, text, erhe::property::c_str(property->get_type()));
                continue;
            }
            std::string validation_error;
            if (!target->validate_value(*property, value.value(), validation_error)) {
                log_usd->warn("USD prim '{}': custom attribute '{}' value '{}' was rejected: {}", absolute_path, name, text, validation_error);
                continue;
            }
            target->set_value(*property, value.value());
        }
    }

    // A polygon mesh is geometry-normative when its subdivisionScheme is
    // `none`; USD's schema fallback is catmullClark, and a subdivision cage
    // is imported as a triangle soup the way glTF primitives are
    // (doc/usd-compatibility-plan.md I1).
    [[nodiscard]] auto is_geometry_normative(const std::string& absolute_path) const -> bool
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return false;
        }
        const lightusd::GeomMesh* geom_mesh = prim->as<lightusd::GeomMesh>();
        if (geom_mesh == nullptr) {
            return false;
        }
        return geom_mesh->subdivisionScheme.get_value() == lightusd::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone;
    }

    // The archive directory of the `.usdz` the stage was loaded from. A
    // `.usdz` is a zip of stored, uncompressed entries, and LightUSD reads
    // only the root layer out of it when it opens the stage, so the archive
    // is read a second time here - once per import - for the entries the
    // materials name.
    [[nodiscard]] auto ensure_usdz_asset() -> bool
    {
        if (m_usdz_read) {
            return m_usdz_ok;
        }
        m_usdz_read = true;
        const std::string filename = m_arguments.path.string();
        if (!lightusd::IsUSDZ(filename)) {
            return false;
        }
        std::string warning;
        std::string error;
        m_usdz_ok = lightusd::ReadUSDZAssetInfoFromFile(filename, &m_usdz_asset, &warning, &error);
        if (!warning.empty()) {
            log_usd->warn("USD '{}': reading the usdz archive: {}", filename, warning);
        }
        if (!m_usdz_ok) {
            log_usd->warn("USD '{}': the usdz archive could not be read: {}", filename, error);
        }
        return m_usdz_ok;
    }

    // Whether the archive holds an entry of that name. USD writes a packaged
    // asset path relative to the archive root, which is the key of the
    // archive's own directory - directory components and all, so a texture
    // packed under `0/` is the key `0/texture.png`; a leading `./` is not
    // part of that key.
    [[nodiscard]] static auto usdz_entry_key(const std::string& asset_identifier) -> std::string
    {
        return (asset_identifier.compare(0, 2, "./") == 0)
            ? asset_identifier.substr(2)
            : asset_identifier;
    }

    // The bytes of one archive entry, empty when the stage is no `.usdz` or
    // the archive holds no such entry.
    [[nodiscard]] auto usdz_entry_bytes(const std::string& asset_identifier) -> std::vector<std::uint8_t>
    {
        if (asset_identifier.empty() || !ensure_usdz_asset()) {
            return {};
        }
        const std::string key = usdz_entry_key(asset_identifier);
        const std::map<std::string, std::pair<std::size_t, std::size_t>>::const_iterator i = m_usdz_asset.asset_map.find(key);
        if (i == m_usdz_asset.asset_map.end()) {
            return {};
        }
        const std::size_t    begin = i->second.first;
        const std::size_t    end   = i->second.second;
        const std::uint8_t*  base  = m_usdz_asset.is_mmaped() ? m_usdz_asset.addr : m_usdz_asset.data.data();
        const std::size_t    size  = m_usdz_asset.is_mmaped() ? m_usdz_asset.size : m_usdz_asset.data.size();
        if ((base == nullptr) || (end <= begin) || (end > size)) {
            log_usd->warn("USD image '{}': the usdz entry is out of the archive's range", asset_identifier);
            return {};
        }
        return std::vector<std::uint8_t>{base + begin, base + end};
    }

    void convert_images()
    {
        m_result.data.images.reserve(m_scene->images.size());
        const std::filesystem::path directory = m_arguments.path.parent_path();
        for (const lightusd::tydra::TextureImage& image : m_scene->images) {
            Usd_image usd_image{};
            usd_image.bytes = usdz_entry_bytes(image.asset_identifier);
            // The asset identifier is the authored one: LightUSD resolves an
            // asset path only when it opens the asset, and its image loaders
            // are off. A relative path is resolved here, against the stage
            // file's own directory, so the caller receives a path it can open.
            std::filesystem::path image_path{image.asset_identifier};
            if (image_path.is_relative() && !directory.empty()) {
                image_path = (directory / image_path).lexically_normal();
            }
            usd_image.path = image_path;
            usd_image.name = usd_image.path.filename().generic_string();
            usd_image.srgb = resolve_srgb(image, usd_image);
            m_result.data.images.push_back(usd_image);
        }
    }

    // Whether the image's texels are sRGB-encoded. An authored color space
    // says so itself. `auto` - the schema fallback, which Tydra reports as
    // Unknown when it reads no texel - is the UsdPreviewSurface rule applied
    // to the image's own header: sRGB when it is 8-bit with 3 or 4
    // components (is_srgb_by_auto_rule), data otherwise.
    [[nodiscard]] auto resolve_srgb(const lightusd::tydra::TextureImage& image, const Usd_image& usd_image) -> bool
    {
        if (image.usdColorSpace != lightusd::tydra::ColorSpace::Unknown) {
            return is_srgb_color_space(image.usdColorSpace);
        }
        const Image_header header = usd_image.bytes.empty()
            ? probe_image_header(usd_image.path)
            : probe_image_header(std::span<const std::uint8_t>{usd_image.bytes.data(), usd_image.bytes.size()});
        if (!header.known) {
            log_usd->info(
                "USD image '{}': color space is 'auto' and the header is not one this reader knows - read as data",
                usd_image.path.generic_string()
            );
        }
        return is_srgb_by_auto_rule(header);
    }

    // The image behind one UsdPreviewSurface input, or no_image when the
    // input carries a plain value.
    static constexpr std::size_t no_image = ~std::size_t{0};

    [[nodiscard]] auto image_of(const std::int32_t texture_id) const -> std::size_t
    {
        if (texture_id < 0) {
            return no_image;
        }
        const std::size_t texture_index = static_cast<std::size_t>(texture_id);
        if (texture_index >= m_scene->textures.size()) {
            return no_image;
        }
        const std::int64_t image_id = m_scene->textures[texture_index].texture_image_id;
        if (image_id < 0) {
            return no_image;
        }
        const std::size_t image_index = static_cast<std::size_t>(image_id);
        if (image_index >= m_result.data.images.size()) {
            return no_image;
        }
        return image_index;
    }

    // Whether two render-scene textures read the same image: the same image
    // entry, or two entries on one asset (Tydra gives each UsdUVTexture prim
    // an image entry of its own). Asked of the render scene, not of the erhe
    // image list: a material appended after Tydra's pass has textures the
    // erhe list does not hold yet.
    [[nodiscard]] auto same_texture_image(const std::int32_t texture_id_a, const std::int32_t texture_id_b) const -> bool
    {
        if ((texture_id_a < 0) || (texture_id_b < 0)) {
            return false;
        }
        const std::size_t index_a = static_cast<std::size_t>(texture_id_a);
        const std::size_t index_b = static_cast<std::size_t>(texture_id_b);
        if ((index_a >= m_scene->textures.size()) || (index_b >= m_scene->textures.size())) {
            return false;
        }
        const std::int64_t image_a = m_scene->textures[index_a].texture_image_id;
        const std::int64_t image_b = m_scene->textures[index_b].texture_image_id;
        if ((image_a < 0) || (image_b < 0)) {
            return false;
        }
        if (image_a == image_b) {
            return true;
        }
        const std::size_t image_index_a = static_cast<std::size_t>(image_a);
        const std::size_t image_index_b = static_cast<std::size_t>(image_b);
        if ((image_index_a >= m_scene->images.size()) || (image_index_b >= m_scene->images.size())) {
            return false;
        }
        return m_scene->images[image_index_a].asset_identifier == m_scene->images[image_index_b].asset_identifier;
    }

    // A value that equals the property's default is not written: a local
    // value is an authored value (doc/property-system.md D32).
    template <typename T>
    static void set_or_clear_value(
        erhe::primitive::Material&         material,
        const erhe::property::Property<T>& property,
        const T&                           value
    )
    {
        if (erhe::property::make_value(value) == material.get_default_value(property.get())) {
            static_cast<void>(material.clear_value(property.get()));
        } else {
            material.set_value(property, value);
        }
    }

    // The inputs:scale component of the channel a scalar input is connected
    // to: UsdPreviewSurface reads one channel of the texture, and USD scales
    // per channel.
    [[nodiscard]] static auto connected_scale(const lightusd::tydra::UVTexture& uv_texture) -> float
    {
        switch (uv_texture.connectedOutputChannel) {
            case lightusd::tydra::UVTexture::Channel::R: return uv_texture.scale[0];
            case lightusd::tydra::UVTexture::Channel::G: return uv_texture.scale[1];
            case lightusd::tydra::UVTexture::Channel::B: return uv_texture.scale[2];
            case lightusd::tydra::UVTexture::Channel::A: return uv_texture.scale[3];
            default:                                    return uv_texture.scale[0];
        }
    }

    // The erhe channel selector for the output a scalar UsdPreviewSurface
    // input is connected through. A scalar input connected to `outputs:rgb`
    // or `outputs:rgba` names no single channel; USD leaves that undefined,
    // so the erhe default for the slot stands and the material is named in
    // one warning.
    [[nodiscard]] static auto connected_channel(
        const lightusd::tydra::UVTexture&      uv_texture,
        const erhe::primitive::Texture_channel default_channel,
        const std::string&                     material_name,
        const char*                            input_name
    ) -> erhe::primitive::Texture_channel
    {
        switch (uv_texture.connectedOutputChannel) {
            case lightusd::tydra::UVTexture::Channel::R: return erhe::primitive::Texture_channel::r;
            case lightusd::tydra::UVTexture::Channel::G: return erhe::primitive::Texture_channel::g;
            case lightusd::tydra::UVTexture::Channel::B: return erhe::primitive::Texture_channel::b;
            case lightusd::tydra::UVTexture::Channel::A: return erhe::primitive::Texture_channel::a;
            default: {
                log_usd->warn(
                    "USD material '{}': the '{}' input is connected to a multi-channel output - the '{}' channel is read",
                    material_name, input_name, erhe::primitive::c_str(default_channel)
                );
                return default_channel;
            }
        }
    }

    // The UsdUVTexture behind one UsdPreviewSurface input, or null when the
    // input carries a plain value.
    [[nodiscard]] auto uv_texture_of(const std::int32_t texture_id) const -> const lightusd::tydra::UVTexture*
    {
        if (texture_id < 0) {
            return nullptr;
        }
        const std::size_t texture_index = static_cast<std::size_t>(texture_id);
        if (texture_index >= m_scene->textures.size()) {
            return nullptr;
        }
        return &m_scene->textures[texture_index];
    }

    // UsdUVTexture's four wrap modes onto the three erhe address modes. erhe
    // has no border color, so `black` (which USD maps onto clamp-to-border
    // with a transparent black border) becomes clamp-to-edge: the closest
    // mode erhe has, and the one Tydra already reports for the `useMetadata`
    // default.
    [[nodiscard]] static auto to_address_mode(const lightusd::tydra::UVTexture::WrapMode wrap) -> erhe::graphics::Sampler_address_mode
    {
        switch (wrap) {
            case lightusd::tydra::UVTexture::WrapMode::REPEAT:          return erhe::graphics::Sampler_address_mode::repeat;
            case lightusd::tydra::UVTexture::WrapMode::MIRROR:          return erhe::graphics::Sampler_address_mode::mirrored_repeat;
            case lightusd::tydra::UVTexture::WrapMode::CLAMP_TO_EDGE:   return erhe::graphics::Sampler_address_mode::clamp_to_edge;
            case lightusd::tydra::UVTexture::WrapMode::CLAMP_TO_BORDER: return erhe::graphics::Sampler_address_mode::clamp_to_edge;
            default:                                                    return erhe::graphics::Sampler_address_mode::clamp_to_edge;
        }
    }

    // The sampling parameters of one UsdUVTexture onto the erhe material
    // slot they belong to: the wrap modes and the UsdTransform2d. USD
    // composes a UV as `in * scale`, then the rotation, then the
    // translation, which is the order the erhe slot transform applies
    // (Material_texture_sampler: `rotation_scale * uv + offset`), so the
    // three values map across unchanged apart from the degrees USD spells
    // the rotation in.
    static void apply_texture_sampling(
        erhe::primitive::Material&                 material,
        erhe::primitive::Material_texture_sampler& slot,
        const lightusd::tydra::UVTexture&          uv_texture
    )
    {
        erhe::primitive::Material_sampler_state sampler_state = slot.sampler;
        sampler_state.wrap_u = to_address_mode(uv_texture.wrapS);
        sampler_state.wrap_v = to_address_mode(uv_texture.wrapT);
        material.set_slot_sampler(slot, sampler_state);
        // The slot transform applies to the flipped texcoord the importer
        // stores, so a UsdTransform2d is converted through the flip
        // (src/erhe/usd/notes.md, "Texture coordinates"). The USD identity
        // maps onto the erhe identity, so a texture without one keeps the
        // slot's defaults.
        if (uv_texture.has_transform2d) {
            const Usd_uv_transform_2d usd_transform{
                .rotation_degrees = uv_texture.tx_rotation,
                .scale            = glm::vec2{uv_texture.tx_scale[0], uv_texture.tx_scale[1]},
                .translation      = glm::vec2{uv_texture.tx_translation[0], uv_texture.tx_translation[1]}
            };
            const Erhe_uv_transform erhe_transform = to_erhe_uv_transform(usd_transform);
            material.set_slot_uv_transform(slot, erhe_transform.rotation, erhe_transform.offset, erhe_transform.scale);
        }
    }

    // A UsdUVTexture reads its texels as `texel * scale + bias`. erhe carries
    // that decode for the normal slot only (Material::normal_texture_decode_*)
    // and, for a color slot, folds the scale into the factor the shader
    // multiplies the texture with. Everything else - a bias on a color slot,
    // and a scale or bias on a slot with no factor - is one warning naming
    // the material and the input.
    static void warn_about_texel_transform(
        const std::string&                material_name,
        const char*                       input_name,
        const lightusd::tydra::UVTexture& uv_texture,
        const bool                        scale_is_carried
    )
    {
        const bool has_bias  = (uv_texture.bias[0]  != 0.0f) || (uv_texture.bias[1]  != 0.0f) || (uv_texture.bias[2]  != 0.0f);
        const bool has_scale = (uv_texture.scale[0] != 1.0f) || (uv_texture.scale[1] != 1.0f) || (uv_texture.scale[2] != 1.0f);
        if (has_bias) {
            log_usd->warn("USD material '{}': inputs:bias of the '{}' texture is not carried", material_name, input_name);
        }
        if (has_scale && !scale_is_carried) {
            log_usd->warn("USD material '{}': inputs:scale of the '{}' texture is not carried", material_name, input_name);
        }
    }

    void bind_texture(const std::size_t material_index, const Usd_material_texture_slot slot, const std::int32_t texture_id)
    {
        const std::size_t image_index = image_of(texture_id);
        if (image_index == no_image) {
            return;
        }
        m_result.data.material_texture_bindings.push_back(
            Usd_material_texture_binding{material_index, slot, image_index}
        );
    }

    // The Shader prim the material's `outputs:surface` connects to: where
    // the UsdPreviewSurface inputs Tydra reports are authored (or not).
    [[nodiscard]] auto find_surface_shader_path(const std::string& material_absolute_path) -> std::string
    {
        const lightusd::Prim* prim = find_prim(material_absolute_path);
        if (prim == nullptr) {
            return {};
        }
        const lightusd::Material* usd_material = prim->as<lightusd::Material>();
        if (usd_material == nullptr) {
            return {};
        }
        const std::vector<lightusd::Path>& connections = usd_material->surface.get_connections();
        if (connections.empty()) {
            return {};
        }
        const lightusd::tstring_view prim_part = connections[0].prim_part();
        return std::string{prim_part.data(), prim_part.size()};
    }

    // Whether erhe carries the `inputs:scale` of the UsdUVTexture a shading
    // input reads: a color or scalar factor folds it in, a slot with no
    // factor of its own does not.
    enum class Texel_scale
    {
        carried,
        not_carried
    };

    // The texture half of one shading input, shared by the UsdPreviewSurface
    // and the OpenPBR paths: bind the image to the erhe slot, carry the
    // UsdUVTexture's wrap modes and its UsdTransform2d onto that slot, and
    // warn about the texel transform erhe does not carry. An input carrying a
    // plain value does nothing. The order the slots are applied in is the
    // order the bindings are recorded in.
    void apply_slot_texture(
        const std::size_t                          material_index,
        erhe::primitive::Material&                 material,
        erhe::primitive::Material_texture_sampler& slot,
        const Usd_material_texture_slot            slot_kind,
        const std::int32_t                         texture_id,
        const char*                                input_name,
        const Texel_scale                          texel_scale
    )
    {
        const lightusd::tydra::UVTexture* uv_texture = uv_texture_of(texture_id);
        if (uv_texture == nullptr) {
            return;
        }
        bind_texture(material_index, slot_kind, texture_id);
        apply_texture_sampling(material, slot, *uv_texture);
        warn_about_texel_transform(material.get_name(), input_name, *uv_texture, texel_scale == Texel_scale::carried);
    }

    // The normal slot: the texture, its sampling, and the texel decode
    // `texel * scale + bias` that erhe carries for this slot only (the shader
    // decodes the sampled normal with it).
    void apply_normal_slot(
        const std::size_t          material_index,
        erhe::primitive::Material& material,
        const std::int32_t         texture_id
    )
    {
        using erhe::primitive::Material;
        const lightusd::tydra::UVTexture* uv_texture = uv_texture_of(texture_id);
        if (uv_texture == nullptr) {
            return;
        }
        erhe::primitive::Material_texture_samplers& slots = material.data.texture_samplers;
        bind_texture(material_index, Usd_material_texture_slot::normal, texture_id);
        apply_texture_sampling(material, slots.normal, *uv_texture);
        set_or_clear_value(
            material,
            Material::normal_texture_decode_scale_property,
            glm::vec4{uv_texture->scale[0], uv_texture->scale[1], uv_texture->scale[2], uv_texture->scale[3]}
        );
        set_or_clear_value(
            material,
            Material::normal_texture_decode_bias_property,
            glm::vec4{uv_texture->bias[0], uv_texture->bias[1], uv_texture->bias[2], uv_texture->bias[3]}
        );
    }

    // erhe has one metallic-roughness slot; a USD surface shader reads the
    // two channels through separate texture inputs that a glTF-derived file
    // points at one image. The roughness input names the image when both are
    // textured, and each input's connection names which channel of it that
    // input reads.
    // The two inputs share one erhe slot, so an input that names no texture
    // of its own while the other one does reads no channel of the bound image:
    // its plain value stands alone (Texture_channel::none). Without that, a
    // file that textures roughness and gives metallic a constant would have
    // metallic modulated by whatever the roughness image holds in the channel
    // the erhe default names.
    void apply_metallic_roughness_slot(
        const std::size_t          material_index,
        erhe::primitive::Material& material,
        const std::int32_t         metallic_texture_id,
        const std::int32_t         roughness_texture_id,
        const char*                metallic_input_name,
        const char*                roughness_input_name
    )
    {
        using erhe::primitive::Material;
        const std::int32_t                slot_texture_id   = (roughness_texture_id >= 0) ? roughness_texture_id : metallic_texture_id;
        const lightusd::tydra::UVTexture* metallic_texture  = uv_texture_of(metallic_texture_id);
        const lightusd::tydra::UVTexture* roughness_texture = uv_texture_of(roughness_texture_id);
        const lightusd::tydra::UVTexture* slot_texture      = uv_texture_of(slot_texture_id);
        if (slot_texture == nullptr) {
            return;
        }
        const std::string&                          material_name = material.get_name();
        erhe::primitive::Material_texture_samplers& slots         = material.data.texture_samplers;
        bind_texture(material_index, Usd_material_texture_slot::metallic_roughness, slot_texture_id);
        apply_texture_sampling(material, slots.metallic_roughness, *slot_texture);
        if (metallic_texture != nullptr) {
            warn_about_texel_transform(material_name, metallic_input_name, *metallic_texture, true);
            set_or_clear_value(
                material, Material::metallic_channel_property,
                connected_channel(*metallic_texture, erhe::primitive::Texture_channel::b, material_name, metallic_input_name)
            );
        } else {
            material.set_value(Material::metallic_channel_property, erhe::primitive::Texture_channel::none);
        }
        if (roughness_texture != nullptr) {
            warn_about_texel_transform(material_name, roughness_input_name, *roughness_texture, true);
            set_or_clear_value(
                material, Material::roughness_channel_property,
                connected_channel(*roughness_texture, erhe::primitive::Texture_channel::g, material_name, roughness_input_name)
            );
        } else {
            material.set_value(Material::roughness_channel_property, erhe::primitive::Texture_channel::none);
        }
    }

    // erhe's fragment alpha comes from the base color texture, so an opacity
    // input is carried only when it reads that same image - the same
    // UsdUVTexture prim, or another one on the same file, which Tydra gives an
    // image entry of its own (RoughnessTest.usdz reads `roughness-spec.png`
    // through two shaders, rgb and a); an opacity map of its own has no erhe
    // slot to live in.
    void apply_opacity_channel(
        erhe::primitive::Material& material,
        const std::int32_t         opacity_texture_id,
        const std::int32_t         base_color_texture_id
    )
    {
        using erhe::primitive::Material;
        const lightusd::tydra::UVTexture* opacity_texture = uv_texture_of(opacity_texture_id);
        if (opacity_texture == nullptr) {
            return;
        }
        const std::string& material_name = material.get_name();
        if (!same_texture_image(opacity_texture_id, base_color_texture_id)) {
            log_usd->warn(
                "USD material '{}': inputs:opacity reads an image of its own - erhe takes the alpha of the base color texture",
                material_name
            );
            return;
        }
        set_or_clear_value(
            material, Material::opacity_channel_property,
            connected_channel(*opacity_texture, erhe::primitive::Texture_channel::a, material_name, "opacity")
        );
    }

    // Only an authored UsdPreviewSurface input becomes a local value
    // (doc/usd-compatibility-plan.md I2). An input the shader leaves at its
    // fallback writes nothing, so the erhe property keeps the ERHE default -
    // which is not the USD fallback for every input (`diffuseColor` 0.18 vs
    // erhe's white `base_color` is the one that differs most).
    void apply_preview_surface(
        const lightusd::tydra::PreviewSurfaceShader& shader,
        const std::string&                           shader_path,
        const std::size_t                            material_index,
        erhe::primitive::Material&                   material
    )
    {
        using erhe::primitive::Material;
        // A connected input takes its value from the texture, so the erhe
        // factor - which the shader multiplies the sampled texel with - is
        // the UsdUVTexture's inputs:scale (white / one when the file authors
        // none), never the plain value the input still carries. The factor
        // is written only when it differs from the erhe default, so a
        // material's local set stays what the file authors (I2).
        const lightusd::tydra::UVTexture* diffuse_texture   = uv_texture_of(shader.diffuseColor.texture_id);
        const lightusd::tydra::UVTexture* emissive_texture  = uv_texture_of(shader.emissiveColor.texture_id);
        const lightusd::tydra::UVTexture* metallic_texture  = uv_texture_of(shader.metallic.texture_id);
        const lightusd::tydra::UVTexture* roughness_texture = uv_texture_of(shader.roughness.texture_id);
        const std::string&                material_name     = material.get_name();
        if (diffuse_texture != nullptr) {
            const glm::vec3 factor{diffuse_texture->scale[0], diffuse_texture->scale[1], diffuse_texture->scale[2]};
            set_or_clear_value(material, Material::base_color_property, factor);
        } else if (is_authored(shader_path, "inputs:diffuseColor")) {
            material.set_value(
                Material::base_color_property,
                glm::vec3{shader.diffuseColor.value[0], shader.diffuseColor.value[1], shader.diffuseColor.value[2]}
            );
        } else {
            // An unauthored input is the schema fallback, and the effective
            // value must be the one USD composes (I2). UsdPreviewSurface's
            // diffuseColor fallback is the 0.18 grey usdview shows; erhe's
            // base_color default is white, so the fallback is written as a
            // local value. Every other input erhe carries has the same
            // fallback in both (roughness 0.5, metallic 0, opacity 1, ior
            // 1.5, emissiveColor black), so none of those is written.
            material.set_value(Material::base_color_property, c_usd_diffuse_color_fallback);
        }
        if (emissive_texture != nullptr) {
            const glm::vec3 factor{emissive_texture->scale[0], emissive_texture->scale[1], emissive_texture->scale[2]};
            set_or_clear_value(material, Material::emissive_property, factor);
        } else if (is_authored(shader_path, "inputs:emissiveColor")) {
            material.set_value(
                Material::emissive_property,
                glm::vec3{shader.emissiveColor.value[0], shader.emissiveColor.value[1], shader.emissiveColor.value[2]}
            );
        }
        if (metallic_texture != nullptr) {
            set_or_clear_value(material, Material::metallic_property, connected_scale(*metallic_texture));
        } else if (is_authored(shader_path, "inputs:metallic")) {
            material.set_value(Material::metallic_property, shader.metallic.value);
        }
        if (roughness_texture != nullptr) {
            const float factor = connected_scale(*roughness_texture);
            set_or_clear_value(material, Material::roughness_property, glm::vec2{factor, factor});
        } else if (is_authored(shader_path, "inputs:roughness")) {
            // erhe's roughness is anisotropic; UsdPreviewSurface has one value.
            material.set_value(Material::roughness_property, glm::vec2{shader.roughness.value, shader.roughness.value});
        }
        if (is_authored(shader_path, "inputs:opacity")) {
            material.set_value(Material::opacity_property, shader.opacity.value);
        }
        if (is_authored(shader_path, "inputs:ior")) {
            material.set_value(Material::ior_property, shader.ior.value);
        }
        if (is_authored(shader_path, "inputs:occlusion")) {
            material.set_value(Material::occlusion_texture_strength_property, shader.occlusion.value);
        }
        // UsdPreviewSurface says opacityThreshold > 0 is a cutout and an
        // opacity below one without a threshold is blended. Both erhe
        // properties are derived, so they are written only when the input
        // they derive from is authored.
        if (is_authored(shader_path, "inputs:opacityThreshold") && (shader.opacityThreshold.value > 0.0f)) {
            material.set_value(Material::blending_mode_property, erhe::primitive::Material_blending_mode::alpha_test);
            material.set_value(Material::alpha_cutoff_property, shader.opacityThreshold.value);
        } else if (is_authored(shader_path, "inputs:opacity") && ((shader.opacity.value < 1.0f) || shader.opacity.is_texture())) {
            material.set_value(Material::blending_mode_property, erhe::primitive::Material_blending_mode::alpha_blend);
        }

        // The textures the shader's inputs read, on the erhe slots they belong
        // to: the image, the wrap modes and the UsdTransform2d of each
        // UsdUVTexture, and the texel decode of the normal slot.
        erhe::primitive::Material_texture_samplers& slots = material.data.texture_samplers;
        apply_slot_texture(
            material_index, material, slots.base_color, Usd_material_texture_slot::base_color,
            shader.diffuseColor.texture_id, "diffuseColor", Texel_scale::carried
        );
        apply_slot_texture(
            material_index, material, slots.emissive, Usd_material_texture_slot::emissive,
            shader.emissiveColor.texture_id, "emissiveColor", Texel_scale::carried
        );
        apply_normal_slot(material_index, material, shader.normal.texture_id);
        apply_slot_texture(
            material_index, material, slots.occlusion, Usd_material_texture_slot::occlusion,
            shader.occlusion.texture_id, "occlusion", Texel_scale::not_carried
        );
        apply_metallic_roughness_slot(
            material_index, material,
            shader.metallic.texture_id, shader.roughness.texture_id,
            "metallic", "roughness"
        );
        // Which channel of the occlusion texture the scalar input reads. USD
        // names it in the connection; erhe's defaults are glTF's packing, so a
        // file that follows glTF writes no local value here.
        const lightusd::tydra::UVTexture* occlusion_texture = uv_texture_of(shader.occlusion.texture_id);
        if (occlusion_texture != nullptr) {
            set_or_clear_value(
                material, Material::occlusion_channel_property,
                connected_channel(*occlusion_texture, erhe::primitive::Texture_channel::r, material_name, "occlusion")
            );
        }
        apply_opacity_channel(material, shader.opacity.texture_id, shader.diffuseColor.texture_id);
    }

    // The `info:id` of a Shader prim that is an OpenPBR surface terminal.
    // Tydra converts each of these into `RenderMaterial::openPBRShader`.
    [[nodiscard]] static auto is_open_pbr_shader(const lightusd::Prim& prim) -> bool
    {
        const lightusd::Shader* shader = prim.as<lightusd::Shader>();
        if (shader == nullptr) {
            return false;
        }
        return
            (shader->info_id == "ND_open_pbr_surface_surfaceshader") ||
            (shader->info_id == "ND_standard_surface_surfaceshader") ||
            (shader->info_id == "OpenPBRSurface");
    }

    // The Shader prim carrying the OpenPBR network of a material, the way
    // `find_surface_shader_path` names the UsdPreviewSurface one: the prim
    // `outputs:mtlx:surface` connects to when the file authors that terminal,
    // and the `outputs:surface` prim otherwise - a material whose only
    // terminal is an OpenPBR shader connects the plain output. A terminal
    // naming a NodeGraph is resolved to the one Shader child that is an
    // OpenPBR surface, which is the shape a MaterialX export writes.
    [[nodiscard]] auto find_open_pbr_shader_path(const std::string& material_absolute_path) -> std::string
    {
        const lightusd::Prim* prim = find_prim(material_absolute_path);
        if (prim == nullptr) {
            return {};
        }
        const lightusd::Material* usd_material = prim->as<lightusd::Material>();
        if (usd_material == nullptr) {
            return {};
        }
        const std::vector<lightusd::Path>& mtlx_connections    = usd_material->mtlxSurface.get_connections();
        const std::vector<lightusd::Path>& surface_connections = usd_material->surface.get_connections();
        const std::vector<lightusd::Path>& connections = mtlx_connections.empty() ? surface_connections : mtlx_connections;
        if (connections.empty()) {
            return {};
        }
        const lightusd::tstring_view prim_part = connections[0].prim_part();
        const std::string            shader_path{prim_part.data(), prim_part.size()};
        const lightusd::Prim*        shader_prim = find_prim(shader_path);
        if (shader_prim == nullptr) {
            return {};
        }
        if (is_open_pbr_shader(*shader_prim)) {
            return shader_path;
        }
        for (const lightusd::Prim& child : shader_prim->children()) {
            if (is_open_pbr_shader(child)) {
                const lightusd::tstring_view element_name = child.element_name();
                return shader_path + "/" + std::string{element_name.data(), element_name.size()};
            }
        }
        return {};
    }

    // OpenPBR's two directional roughnesses from `specular_roughness` and the
    // anisotropy, which is what erhe's `roughness` vec2 holds (the shader
    // squares each component into the GGX alpha of that tangent direction).
    // The parameterization is MaterialX's own `roughness_anisotropy` node,
    // the node a MaterialX surface reaches this value through and the one
    // LightUSD's renderer implements: with `alpha = roughness * roughness`
    // and `aspect = sqrt(1 - clamp(anisotropy, 0, 0.98))`, the two alphas are
    // `min(alpha / aspect, 1)` and `alpha * aspect`. erhe stores roughness
    // rather than alpha, so it carries the square roots of those.
    [[nodiscard]] static auto to_anisotropic_roughness(const float roughness, const float anisotropy) -> glm::vec2
    {
        if (anisotropy == 0.0f) {
            return glm::vec2{roughness, roughness};
        }
        const float alpha   = roughness * roughness;
        const float aspect  = std::sqrt(1.0f - std::clamp(anisotropy, 0.0f, 0.98f));
        const float alpha_x = std::min(alpha / aspect, 1.0f);
        const float alpha_y = alpha * aspect;
        return glm::vec2{std::sqrt(alpha_x), std::sqrt(alpha_y)};
    }

    // The OpenPBR network Tydra converts into `RenderMaterial::openPBRShader`
    // onto the erhe material. The authored-opinion rule is the
    // UsdPreviewSurface one (I2): an input the file leaves at its fallback
    // writes nothing, except where the OpenPBR fallback is not the erhe
    // default - `base_color` (0.8 grey against erhe's white) and
    // `specular_roughness` (0.3 against erhe's 0.5) - which compose as the
    // fallback and so are written as local values. The erhe-only fields no
    // OpenPBR input carries (reflectance, the brushed-metal block) keep their
    // `erhe:Material:<name>` custom-attribute path, which the authored-opinion
    // pass applies after this one.
    void apply_open_pbr_surface(
        const lightusd::tydra::OpenPBRSurfaceShader& shader,
        const std::string&                           shader_path,
        const std::size_t                            material_index,
        erhe::primitive::Material&                   material
    )
    {
        using erhe::primitive::Material;
        // A connected input takes its value from the texture, so the erhe
        // factor - which the shader multiplies the sampled texel with - is
        // the UsdUVTexture's inputs:scale, never the plain value the input
        // still carries (the UsdPreviewSurface path spells the same rule).
        const lightusd::tydra::UVTexture* base_color_texture = uv_texture_of(shader.base_color.texture_id);
        const lightusd::tydra::UVTexture* emission_texture   = uv_texture_of(shader.emission_color.texture_id);
        const lightusd::tydra::UVTexture* metalness_texture  = uv_texture_of(shader.base_metalness.texture_id);
        const lightusd::tydra::UVTexture* roughness_texture  = uv_texture_of(shader.specular_roughness.texture_id);

        if (base_color_texture != nullptr) {
            const glm::vec3 factor{base_color_texture->scale[0], base_color_texture->scale[1], base_color_texture->scale[2]};
            set_or_clear_value(material, Material::base_color_property, factor);
        } else if (is_authored(shader_path, "inputs:base_color")) {
            material.set_value(
                Material::base_color_property,
                glm::vec3{shader.base_color.value[0], shader.base_color.value[1], shader.base_color.value[2]}
            );
        } else {
            material.set_value(Material::base_color_property, c_open_pbr_base_color_fallback);
        }

        if (metalness_texture != nullptr) {
            set_or_clear_value(material, Material::metallic_property, connected_scale(*metalness_texture));
        } else if (is_authored(shader_path, "inputs:base_metalness")) {
            material.set_value(Material::metallic_property, shader.base_metalness.value);
        }

        // OpenPBR spells an anisotropic surface as one roughness plus an
        // anisotropy; `specular_roughness_anisotropy` is the OpenPBR input
        // and `specular_anisotropy` the Autodesk Standard Surface spelling of
        // the same knob, which Tydra fills from the network it converted.
        // The composed roughness is a local value in every case, the OpenPBR
        // fallback (0.3) not being the erhe default.
        const float roughness = (roughness_texture != nullptr)
            ? connected_scale(*roughness_texture)
            : shader.specular_roughness.value;
        const float anisotropy = is_authored(shader_path, "inputs:specular_roughness_anisotropy")
            ? shader.specular_roughness_anisotropy.value
            : shader.specular_anisotropy.value;
        material.set_value(Material::roughness_property, to_anisotropic_roughness(roughness, anisotropy));
        // The two roughness components only reach the shading through a BXDF
        // model that reads both, so an anisotropic network names one.
        if (anisotropy != 0.0f) {
            material.set_value(Material::bxdf_model_property, erhe::primitive::Bxdf_model::anisotropic_brdf);
        }

        if (is_authored(shader_path, "inputs:specular_ior")) {
            material.set_value(Material::ior_property, shader.specular_ior.value);
        }
        // The erhe-only field OpenPBR does carry an input for.
        if (is_authored(shader_path, "inputs:transmission_weight")) {
            material.set_value(Material::transmission_property, shader.transmission_weight.value);
        }

        // OpenPBR's emission is a photometric luminance times a color; erhe's
        // emissive is the linear color the shader adds, so the luminance
        // scales the color.
        const bool emission_authored =
            (emission_texture != nullptr) ||
            is_authored(shader_path, "inputs:emission_luminance") ||
            is_authored(shader_path, "inputs:emission_color");
        if (emission_authored) {
            const glm::vec3 emission_color = (emission_texture != nullptr)
                ? glm::vec3{emission_texture->scale[0], emission_texture->scale[1], emission_texture->scale[2]}
                : glm::vec3{shader.emission_color.value[0], shader.emission_color.value[1], shader.emission_color.value[2]};
            set_or_clear_value(material, Material::emissive_property, emission_color * shader.emission_luminance.value);
        }

        // OpenPBR spells opacity as `geometry_opacity`; a file following the
        // UsdPreviewSurface habit spells it `opacity`, which LightUSD parses
        // into the same field.
        const bool opacity_authored =
            is_authored(shader_path, "inputs:geometry_opacity") ||
            is_authored(shader_path, "inputs:opacity");
        if (opacity_authored) {
            material.set_value(Material::opacity_property, shader.opacity.value);
            // OpenPBR has no opacity threshold, so a cutout is not
            // expressible: an opacity below one is blended.
            if ((shader.opacity.value < 1.0f) || shader.opacity.is_texture()) {
                material.set_value(Material::blending_mode_property, erhe::primitive::Material_blending_mode::alpha_blend);
            }
        }

        erhe::primitive::Material_texture_samplers& slots = material.data.texture_samplers;
        apply_slot_texture(
            material_index, material, slots.base_color, Usd_material_texture_slot::base_color,
            shader.base_color.texture_id, "base_color", Texel_scale::carried
        );
        apply_slot_texture(
            material_index, material, slots.emissive, Usd_material_texture_slot::emissive,
            shader.emission_color.texture_id, "emission_color", Texel_scale::carried
        );
        apply_normal_slot(material_index, material, shader.normal.texture_id);
        apply_metallic_roughness_slot(
            material_index, material,
            shader.base_metalness.texture_id, shader.specular_roughness.texture_id,
            "base_metalness", "specular_roughness"
        );
        apply_opacity_channel(material, shader.opacity.texture_id, shader.base_color.texture_id);
    }

    void convert_materials()
    {
        m_result.data.materials.reserve(m_scene->materials.size());
        for (std::size_t material_index = 0, end = m_scene->materials.size(); material_index < end; ++material_index) {
            const Tydra_material& usd_material = m_scene->materials[material_index];
            // The create info carries the name only: every value field of a
            // default Material_values equals the erhe default, so the new
            // material starts with no local value at all and the authored
            // inputs below are the complete local set.
            erhe::primitive::Material_create_info create_info{};
            create_info.name = usd_material.name.empty()
                ? fmt::format("material_{}", material_index)
                : usd_material.name;

            std::shared_ptr<erhe::primitive::Material> material = std::make_shared<erhe::primitive::Material>(create_info);
            material->set_source_path(m_arguments.path);
            // A resource prim is shown in the UI and is not scene content
            // (src/editor/content_library/notes.md), which is what keeps the
            // glTF node writer from writing it as a node.
            material->enable_flag_bits(erhe::Item_flags::show_in_ui);
            m_material_by_path[usd_material.abs_path] = material_index;

            // Which surface terminals a Material prim offers is Tydra's
            // reading of the file; erhe chooses between them. The OpenPBR
            // network is the richer one - it is the only one that carries
            // erhe's anisotropic roughness and its transmission - so it is
            // read wherever it is there, and a material offering both is
            // named in one line saying so.
            if (usd_material.openPBRShader.has_value()) {
                if (usd_material.surfaceShader.has_value()) {
                    log_usd->info(
                        "USD material '{}' carries both a UsdPreviewSurface and an OpenPBR network - the OpenPBR network is read",
                        create_info.name
                    );
                    // An erhe texture graph binds a material slot through a
                    // UsdPreviewSurface input (doc/usd-texture-graphs-plan.md),
                    // whichever terminal supplies the values, so the bindings
                    // are read off that shader here too.
                    read_material_graph_bindings(material_index, find_surface_shader_path(usd_material.abs_path));
                }
                // Which of the shader's inputs the file authors is read off
                // the Shader prim, so a terminal that resolves to none of them
                // would silently read every input as its fallback.
                const std::string shader_path = find_open_pbr_shader_path(usd_material.abs_path);
                if (shader_path.empty()) {
                    log_usd->warn(
                        "USD material '{}': the OpenPBR terminal names no Shader prim - every input reads as its fallback",
                        create_info.name
                    );
                }
                apply_open_pbr_surface(
                    usd_material.openPBRShader.value(),
                    shader_path,
                    material_index,
                    *material.get()
                );
            } else if (usd_material.surfaceShader.has_value()) {
                const std::string shader_path = find_surface_shader_path(usd_material.abs_path);
                apply_preview_surface(
                    usd_material.surfaceShader.value(),
                    shader_path,
                    material_index,
                    *material.get()
                );
                read_material_graph_bindings(material_index, shader_path);
            } else {
                log_usd->warn(
                    "USD material '{}' has no UsdPreviewSurface and no OpenPBR shader - erhe material defaults are used",
                    create_info.name
                );
            }
            m_authored_opinions.push_back(
                Authored_opinions{
                    .absolute_path     = usd_material.abs_path,
                    .visibility_target = nullptr,
                    .primary           = material.get(),
                    .secondary         = nullptr
                }
            );
            m_result.data.materials.push_back(material);
        }
    }

    [[nodiscard]] auto material_at(const int material_id) const -> std::shared_ptr<erhe::primitive::Material>
    {
        if (material_id < 0) {
            return {};
        }
        const std::size_t index = static_cast<std::size_t>(material_id);
        if (index >= m_result.data.materials.size()) {
            return {};
        }
        return m_result.data.materials[index];
    }

    // Split a mesh's facets into the groups that share a material: one per
    // materialBind GeomSubset, plus the facets no subset claims.
    // The subset names of a mesh in the order the prims are authored in.
    // Tydra keys its subset map by name, so the map alone would reorder a
    // mesh's primitives by name on every load - and a save that writes them
    // back in erhe's order would then not reproduce its own file.
    [[nodiscard]] auto authored_subset_names(const std::string& mesh_absolute_path) const -> std::vector<std::string>
    {
        std::vector<std::string> names;
        const lightusd::Prim* prim = find_prim(mesh_absolute_path);
        if (prim == nullptr) {
            return names;
        }
        for (const lightusd::Prim& child : prim->children()) {
            if (child.type_name() == "GeomSubset") {
                const lightusd::tstring_view element_name = child.element_name();
                names.emplace_back(element_name.data(), element_name.size());
            }
        }
        return names;
    }

    // `holeIndices` names the faces USD does not draw. Tydra removes them
    // only when it triangulates, and the conversion keeps the authored
    // polygons instead, so the hole facets are dropped here: each is claimed
    // before any subset sees it and so lands in no facet group, which both
    // the geometry-normative build and the triangle-soup build walk.
    [[nodiscard]] auto authored_hole_facets(const std::string& mesh_absolute_path) const -> std::vector<std::uint32_t>
    {
        std::vector<std::uint32_t> hole_facets;
        const lightusd::Prim* prim = find_prim(mesh_absolute_path);
        if (prim == nullptr) {
            return hole_facets;
        }
        const lightusd::GeomMesh* geom_mesh = prim->as<lightusd::GeomMesh>();
        if (geom_mesh == nullptr) {
            return hole_facets;
        }
        lightusd::Animatable<std::vector<std::int32_t>> animatable{};
        if (!geom_mesh->holeIndices.get_value(&animatable)) {
            return hole_facets;
        }
        std::vector<std::int32_t> indices{};
        if (!animatable.get_default(&indices)) {
            return hole_facets;
        }
        hole_facets.reserve(indices.size());
        for (const std::int32_t index : indices) {
            if (index >= 0) {
                hole_facets.push_back(static_cast<std::uint32_t>(index));
            }
        }
        return hole_facets;
    }

    [[nodiscard]] auto make_facet_groups(const Tydra_mesh& usd_mesh) const -> std::vector<Facet_group>
    {
        const std::size_t facet_count = usd_mesh.faceVertexCounts().size();
        std::vector<Facet_group> groups;
        std::vector<bool>        claimed(facet_count, false);

        for (const std::uint32_t hole_facet : authored_hole_facets(usd_mesh.abs_path)) {
            if (hole_facet < facet_count) {
                claimed[hole_facet] = true;
            }
        }

        // Authored order first, then anything the map holds that no prim
        // named (a subset Tydra synthesized).
        std::vector<const std::pair<const std::string, Tydra_subset>*> ordered_subsets;
        std::set<std::string>                                          visited_subsets;
        for (const std::string& name : authored_subset_names(usd_mesh.abs_path)) {
            const std::map<std::string, Tydra_subset>::const_iterator i = usd_mesh.material_subsetMap.find(name);
            if ((i != usd_mesh.material_subsetMap.end()) && visited_subsets.insert(name).second) {
                ordered_subsets.push_back(&*i);
            }
        }
        for (const std::pair<const std::string, Tydra_subset>& entry : usd_mesh.material_subsetMap) {
            if (visited_subsets.insert(entry.first).second) {
                ordered_subsets.push_back(&entry);
            }
        }

        for (const std::pair<const std::string, Tydra_subset>* entry_pointer : ordered_subsets) {
            const std::pair<const std::string, Tydra_subset>& entry = *entry_pointer;
            Facet_group group{};
            group.name        = entry.first;
            group.material_id = entry.second.material_id;
            for (const int facet : entry.second.indices()) {
                if ((facet < 0) || (static_cast<std::size_t>(facet) >= facet_count)) {
                    continue;
                }
                const std::size_t facet_index = static_cast<std::size_t>(facet);
                if (claimed[facet_index]) {
                    continue;
                }
                claimed[facet_index] = true;
                group.facets.push_back(static_cast<std::uint32_t>(facet_index));
            }
            if (!group.facets.empty()) {
                groups.push_back(std::move(group));
            }
        }

        Facet_group remainder{};
        remainder.name        = "";
        remainder.material_id = usd_mesh.material_id;
        for (std::size_t facet_index = 0; facet_index < facet_count; ++facet_index) {
            if (!claimed[facet_index]) {
                remainder.facets.push_back(static_cast<std::uint32_t>(facet_index));
            }
        }
        if (!remainder.facets.empty()) {
            groups.push_back(std::move(remainder));
        }
        return groups;
    }

    // First corner (face-vertex) index of every facet.
    [[nodiscard]] static auto make_facet_corner_offsets(const Tydra_mesh& usd_mesh) -> std::vector<std::uint32_t>
    {
        const std::vector<std::uint32_t>& counts = usd_mesh.faceVertexCounts();
        std::vector<std::uint32_t>        offsets;
        offsets.reserve(counts.size() + 1);
        std::uint32_t offset = 0;
        for (const std::uint32_t count : counts) {
            offsets.push_back(offset);
            offset += count;
        }
        offsets.push_back(offset);
        return offsets;
    }

    void set_corner_attributes(
        erhe::geometry::Geometry&  geometry,
        const Tydra_mesh&          usd_mesh,
        const GEO::index_t         corner,
        const std::size_t          usd_vertex,
        const std::size_t          usd_facet,
        const std::size_t          usd_corner
    ) const
    {
        erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        if (!usd_mesh.normals.empty() && (attribute_component_count(usd_mesh.normals.format) >= 3)) {
            const glm::vec4 normal = read_attribute(
                usd_mesh.normals,
                attribute_element_index(usd_mesh.normals, usd_vertex, usd_facet, usd_corner)
            );
            attributes.corner_normal.set(corner, GEO::vec3f{normal.x, normal.y, normal.z});
        }
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto texcoord_i = usd_mesh.texcoords.find(static_cast<std::uint32_t>(slot));
            if (texcoord_i == usd_mesh.texcoords.end()) {
                continue;
            }
            const Tydra_attribute& texcoord = texcoord_i->second;
            if (texcoord.empty() || (attribute_component_count(texcoord.format) < 2)) {
                continue;
            }
            const glm::vec4 uv = read_attribute(
                texcoord,
                attribute_element_index(texcoord, usd_vertex, usd_facet, usd_corner)
            );
            // USD's `st` origin is the image's bottom-left corner, erhe's the
            // top-left one (src/erhe/usd/notes.md, "Texture coordinates").
            const glm::vec2 flipped = flip_texcoord_v(glm::vec2{uv.x, uv.y});
            attributes.corner_texcoord(slot).set(corner, GEO::vec2f{flipped.x, flipped.y});
        }
        if (usd_mesh_has_color(usd_mesh)) {
            const glm::vec4 color = usd_mesh_corner_color(usd_mesh, usd_vertex, usd_facet, usd_corner);
            attributes.corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, color.w});
        }
    }

    // Geometry-normative build: the authored facet counts and indices go
    // straight into geogram and the primvars become corner attributes per
    // the element table of doc/usd_compatibility.md.
    [[nodiscard]] auto build_geometry(
        const Tydra_mesh&                 usd_mesh,
        const Facet_group&                group,
        const std::vector<std::uint32_t>& facet_corner_offsets,
        const std::string&                name,
        const Joint_influences&           joint_influences
    ) const -> std::shared_ptr<erhe::geometry::Geometry>
    {
        const std::vector<std::uint32_t>& counts  = usd_mesh.faceVertexCounts();
        const std::vector<std::uint32_t>& indices = usd_mesh.faceVertexIndices();

        std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(name);
        geometry->get_attributes().unbind();
        GEO::Mesh& mesh = geometry->get_mesh();
        mesh.clear(false, false);
        mesh.vertices.set_single_precision();

        // Only the vertices the group's facets use, in first-use order.
        std::unordered_map<std::uint32_t, GEO::index_t> vertex_map;
        std::vector<std::uint32_t>                      used_vertices;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            for (std::uint32_t local = 0; local < counts[facet]; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                if (vertex_map.find(usd_vertex) == vertex_map.end()) {
                    vertex_map[usd_vertex] = static_cast<GEO::index_t>(used_vertices.size());
                    used_vertices.push_back(usd_vertex);
                }
            }
        }

        mesh.vertices.create_vertices(static_cast<GEO::index_t>(used_vertices.size()));
        for (std::size_t i = 0; i < used_vertices.size(); ++i) {
            const lightusd::tydra::vec3& point = usd_mesh.points[used_vertices[i]];
            float* destination = mesh.vertices.single_precision_point_ptr(static_cast<GEO::index_t>(i));
            destination[0] = point[0];
            destination[1] = point[1];
            destination[2] = point[2];
        }

        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const GEO::index_t  new_facet     = mesh.facets.create_polygon(corner_count);
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                mesh.facets.set_vertex(new_facet, local, vertex_map[usd_vertex]);
            }
        }

        geometry->get_attributes().bind();
        std::size_t new_facet_index = 0;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const GEO::index_t  new_facet     = static_cast<GEO::index_t>(new_facet_index++);
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                set_corner_attributes(
                    *geometry.get(),
                    usd_mesh,
                    mesh.facets.corner(new_facet, local),
                    indices[corner_offset + local],
                    facet,
                    corner_offset + local
                );
            }
        }

        // The skin influences of the vertices this group kept: they are
        // vertex-variability primvars, so they land on the geogram vertex the
        // USD point became (K1).
        if (!joint_influences.empty()) {
            erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            const std::size_t                slots      = joint_influences.set_count * c_joints_per_set;
            for (std::size_t i = 0; i < used_vertices.size(); ++i) {
                const std::size_t usd_vertex = used_vertices[i];
                if (usd_vertex >= joint_influences.vertex_count) {
                    continue;
                }
                const GEO::index_t vertex = static_cast<GEO::index_t>(i);
                for (std::size_t set = 0; set < joint_influences.set_count; ++set) {
                    const std::size_t base = (usd_vertex * slots) + (set * c_joints_per_set);
                    attributes.vertex_joint_indices(set).set(
                        vertex,
                        GEO::vec4u{
                            joint_influences.indices[base + 0],
                            joint_influences.indices[base + 1],
                            joint_influences.indices[base + 2],
                            joint_influences.indices[base + 3]
                        }
                    );
                    attributes.vertex_joint_weights(set).set(
                        vertex,
                        GEO::vec4f{
                            joint_influences.weights[base + 0],
                            joint_influences.weights[base + 1],
                            joint_influences.weights[base + 2],
                            joint_influences.weights[base + 3]
                        }
                    );
                }
            }
        }

        // Facet adjacency, edges and the smooth vertex normals the wide-line
        // renderer needs. The same processing the editor's glTF finalize
        // pass runs for imported geometry that arrives without edges.
        geometry->process(
            {
                .flags =
                    erhe::geometry::Geometry::process_flag_connect                  |
                    erhe::geometry::Geometry::process_flag_build_edges              |
                    erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
            }
        );
        return geometry;
    }

    // Triangle-soup build for everything that is not a geometry-normative
    // polygon mesh: one vertex per polygon corner, polygons fanned into
    // triangles. Same carrier the glTF importer produces.
    [[nodiscard]] auto build_triangle_soup(
        const Tydra_mesh&                 usd_mesh,
        const Facet_group&                group,
        const std::vector<std::uint32_t>& facet_corner_offsets,
        const Joint_influences&           joint_influences
    ) const -> std::shared_ptr<erhe::primitive::Triangle_soup>
    {
        using namespace erhe::dataformat;

        const std::vector<std::uint32_t>& counts  = usd_mesh.faceVertexCounts();
        const std::vector<std::uint32_t>& indices = usd_mesh.faceVertexIndices();

        std::shared_ptr<erhe::primitive::Triangle_soup> soup = std::make_shared<erhe::primitive::Triangle_soup>();
        soup->primitive_type = erhe::primitive::Primitive_type::triangles;
        soup->vertex_format.streams.emplace_back(0);
        soup->vertex_format.streams.front().emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
        const bool has_normal   = !usd_mesh.normals.empty() && (attribute_component_count(usd_mesh.normals.format) >= 3);
        const bool has_color    = usd_mesh_has_color(usd_mesh);
        const auto texcoord_i   = usd_mesh.texcoords.find(0u);
        const bool has_texcoord = (texcoord_i != usd_mesh.texcoords.end()) &&
                                  !texcoord_i->second.empty() &&
                                  (attribute_component_count(texcoord_i->second.format) >= 2);
        if (has_normal) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal, 0);
        }
        if (has_texcoord) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0);
        }
        // Authored colors only: a color attribute in the soup format is what
        // marks the mesh as vertex colored (Buffer_mesh::has_vertex_colors),
        // and an unbound mesh's displayColor is its albedo on that account.
        if (has_color) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color, 0);
        }
        for (std::size_t set = 0; set < joint_influences.set_count; ++set) {
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec4_uint,  Vertex_attribute_usage::joint_indices, set);
            soup->vertex_format.streams.front().emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, set);
        }
        soup->vertex_format.streams.front().finalize_stride();

        std::size_t corner_total = 0;
        for (const std::uint32_t facet : group.facets) {
            corner_total += counts[facet];
        }
        const std::size_t stride = soup->vertex_format.streams.front().stride;
        soup->vertex_data.resize(corner_total * stride);

        std::size_t vertex_index = 0;
        for (const std::uint32_t facet : group.facets) {
            const std::uint32_t corner_count  = counts[facet];
            const std::uint32_t corner_offset = facet_corner_offsets[facet];
            const std::size_t   facet_first   = vertex_index;
            for (std::uint32_t local = 0; local < corner_count; ++local) {
                const std::uint32_t usd_vertex = indices[corner_offset + local];
                const std::size_t   usd_corner = corner_offset + local;
                std::uint8_t*       destination = soup->vertex_data.data() + (vertex_index * stride);
                std::size_t         offset      = 0;

                const lightusd::tydra::vec3& point = usd_mesh.points[usd_vertex];
                const float position[3] = {point[0], point[1], point[2]};
                std::memcpy(destination + offset, position, sizeof(position));
                offset += sizeof(position);

                if (has_normal) {
                    const glm::vec4 normal = read_attribute(
                        usd_mesh.normals,
                        attribute_element_index(usd_mesh.normals, usd_vertex, facet, usd_corner)
                    );
                    const float normal_values[3] = {normal.x, normal.y, normal.z};
                    std::memcpy(destination + offset, normal_values, sizeof(normal_values));
                    offset += sizeof(normal_values);
                }
                if (has_texcoord) {
                    const glm::vec4 uv = read_attribute(
                        texcoord_i->second,
                        attribute_element_index(texcoord_i->second, usd_vertex, facet, usd_corner)
                    );
                    const glm::vec2 flipped   = flip_texcoord_v(glm::vec2{uv.x, uv.y});
                    const float     uv_values[2] = {flipped.x, flipped.y};
                    std::memcpy(destination + offset, uv_values, sizeof(uv_values));
                    offset += sizeof(uv_values);
                }
                if (has_color) {
                    const glm::vec4 color = usd_mesh_corner_color(usd_mesh, usd_vertex, facet, usd_corner);
                    const float color_values[4] = {color.x, color.y, color.z, color.w};
                    std::memcpy(destination + offset, color_values, sizeof(color_values));
                    offset += sizeof(color_values);
                }

                // The skin influences of the USD point this corner came from
                // (K1). They follow the color in the stream, in the order the
                // attributes were added above.
                if (!joint_influences.empty() && (usd_vertex < joint_influences.vertex_count)) {
                    const std::size_t slots = joint_influences.set_count * c_joints_per_set;
                    for (std::size_t set = 0; set < joint_influences.set_count; ++set) {
                        const std::size_t   base = (usd_vertex * slots) + (set * c_joints_per_set);
                        const std::uint32_t index_values[4] = {
                            joint_influences.indices[base + 0],
                            joint_influences.indices[base + 1],
                            joint_influences.indices[base + 2],
                            joint_influences.indices[base + 3]
                        };
                        std::memcpy(destination + offset, index_values, sizeof(index_values));
                        offset += sizeof(index_values);
                        const float weight_values[4] = {
                            joint_influences.weights[base + 0],
                            joint_influences.weights[base + 1],
                            joint_influences.weights[base + 2],
                            joint_influences.weights[base + 3]
                        };
                        std::memcpy(destination + offset, weight_values, sizeof(weight_values));
                        offset += sizeof(weight_values);
                    }
                }

                ++vertex_index;
            }
            for (std::uint32_t local = 2; local < corner_count; ++local) {
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first));
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first + local - 1));
                soup->index_data.push_back(static_cast<std::uint32_t>(facet_first + local));
            }
        }
        return soup;
    }

    void convert_meshes()
    {
        m_result.data.meshes.reserve(m_scene->meshes.size());
        for (std::size_t mesh_index = 0, end = m_scene->meshes.size(); mesh_index < end; ++mesh_index) {
            const Tydra_mesh& usd_mesh = m_scene->meshes[mesh_index];
            const std::string mesh_name = usd_mesh.prim_name.empty()
                ? fmt::format("mesh_{}", mesh_index)
                : usd_mesh.prim_name;

            std::shared_ptr<erhe::scene::Mesh> mesh = make_mesh_shell(mesh_name);

            // A primitive-schema prim is the shape its schema attributes
            // describe, tessellated by the erhe generator of that shape
            // (S1). Tydra converts one of these to a triangle list with no
            // shared vertices, which carries no usable topology, so the
            // schema attributes are read instead and Tydra's tessellation of
            // this prim is left unused.
            const std::string prim_type_name = read_prim_type_name(usd_mesh.abs_path);
            if (is_primitive_schema_prim_type(prim_type_name)) {
                add_primitive_schema_primitive(*mesh.get(), usd_mesh.abs_path, prim_type_name, material_at(usd_mesh.material_id));
                m_mesh_group_names.push_back(std::vector<std::string>{std::string{}});
                m_mesh_index_by_path.emplace(usd_mesh.abs_path, mesh_index);
                m_result.data.meshes.push_back(mesh);
                continue;
            }

            const bool                       geometry_normative   = is_geometry_normative(usd_mesh.abs_path);
            const std::vector<std::uint32_t> facet_corner_offsets = make_facet_corner_offsets(usd_mesh);
            const std::vector<Facet_group>   groups               = make_facet_groups(usd_mesh);
            // A skinned mesh carries its influences whichever build it takes:
            // the primvars are vertex-variability, which is the domain the
            // geometry-normative build keeps and the soup build splits per
            // corner (K1).
            const Joint_influences           joint_influences     = make_joint_influences(usd_mesh);
            // The subset each primitive came from, in the order the
            // primitives are added: what a variant binding at a GeomSubset
            // path names (doc/usd-compatibility-plan.md X4).
            std::vector<std::string>         group_names;
            group_names.reserve(groups.size());
            for (const Facet_group& group : groups) {
                group_names.push_back(group.name);
            }
            m_mesh_group_names.push_back(std::move(group_names));
            for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
                const Facet_group& group = groups[group_index];
                const std::string  name  = group.name.empty()
                    ? fmt::format("{}[{}]", mesh_name, group_index)
                    : fmt::format("{}.{}", mesh_name, group.name);
                std::shared_ptr<erhe::primitive::Primitive> primitive;
                if (geometry_normative) {
                    primitive = std::make_shared<erhe::primitive::Primitive>(
                        build_geometry(usd_mesh, group, facet_corner_offsets, name, joint_influences)
                    );
                } else {
                    primitive = std::make_shared<erhe::primitive::Primitive>(
                        build_triangle_soup(usd_mesh, group, facet_corner_offsets, joint_influences)
                    );
                }
                mesh->add_primitive(primitive, material_at(group.material_id));
            }
            m_mesh_index_by_path.emplace(usd_mesh.abs_path, mesh_index);
            m_result.data.meshes.push_back(mesh);
        }
    }

    // The UsdGeom primitive schemas erhe builds geometry for: a `Cube`,
    // `Sphere`, `Cone`, `Cylinder`, `Capsule` or one of the `_1` schema
    // variants is the mesh its schema attributes describe
    // (doc/usd-compatibility-plan.md S1). Tydra converts no geometry for
    // these types, so the conversion reads the raw prim and tessellates the
    // analytic surface with the erhe generator of that shape.
    [[nodiscard]] static auto is_primitive_schema_prim_type(const std::string& type_name) -> bool
    {
        return
            (type_name == "Cube")       ||
            (type_name == "Sphere")     ||
            (type_name == "Cone")       ||
            (type_name == "Cylinder")   ||
            (type_name == "Cylinder_1") ||
            (type_name == "Capsule")    ||
            (type_name == "Capsule_1");
    }

    // A schema attribute's value at the default time. An unauthored
    // attribute answers with the schema fallback the prim struct carries,
    // and a time-sampled one with `fallback` - erhe reads no animation here.
    [[nodiscard]] static auto read_schema_double(
        const lightusd::TypedAttributeWithFallback<lightusd::Animatable<double>>& attribute,
        const double                                                              fallback
    ) -> double
    {
        double value = fallback;
        if (attribute.get_value().get_default(&value)) {
            return value;
        }
        return fallback;
    }

    // The rotation that takes the generator's own axis onto the prim's
    // `axis` token. The rotation is baked into the geometry rather than into
    // the node transform, so the prim's own xformOps stay what the file
    // authored (M8).
    [[nodiscard]] static auto rotation_from_x_axis(const lightusd::Axis axis) -> glm::mat4
    {
        switch (axis) {
            case lightusd::Axis::X: return glm::mat4{1.0f};
            case lightusd::Axis::Y: return glm::rotate(glm::mat4{1.0f},  glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
            default:                return glm::rotate(glm::mat4{1.0f}, -glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
        }
    }

    [[nodiscard]] static auto rotation_from_y_axis(const lightusd::Axis axis) -> glm::mat4
    {
        switch (axis) {
            case lightusd::Axis::X: return glm::rotate(glm::mat4{1.0f}, -glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
            case lightusd::Axis::Y: return glm::mat4{1.0f};
            default:                return glm::rotate(glm::mat4{1.0f},  glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        }
    }

    // Fixed tessellation of the primitive schemas. A USD primitive is an
    // analytic surface and an erhe Geometry is polygonal, so the conversion
    // picks the subdivision: 32 slices around the axis reads as smooth at
    // unit scale, 16 stacks give a sphere its latitude rings, 8 stacks per
    // hemisphere do the same for a capsule's caps, and a cone or cylinder
    // needs one stack along its axis because its side is ruled.
    static constexpr int          c_schema_slice_count           = 32;
    static constexpr int          c_schema_stack_count           = 1;
    static constexpr unsigned int c_schema_sphere_slice_count    = 32;
    static constexpr unsigned int c_schema_sphere_stack_count    = 16;
    static constexpr int          c_schema_capsule_stack_count   = 8;

    // `primvars:displayColor` / `primvars:displayOpacity` of a primitive-schema
    // prim, as the corner color the mesh path gives a `Mesh` prim's primvars
    // (set_corner_attributes): the fragment shader multiplies it into the base
    // color, bound material or not. A schema prim authors no topology, so the
    // only element a value can address is the whole surface: the first
    // element colors every corner, and a prim that authors more than one is
    // named in a warning. A prim that authors neither primvar leaves the
    // attribute absent, the way an uncolored `Mesh` does.
    void apply_primitive_schema_display_color(
        const lightusd::GPrim&    gprim,
        const std::string&        absolute_path,
        erhe::geometry::Geometry& geometry
    )
    {
        const std::vector<lightusd::value::color3f> colors = gprim.get_displayColors();
        std::vector<float>                           opacities;
        if (gprim.has_primvar("displayOpacity")) {
            lightusd::GeomPrimvar primvar;
            std::string           error;
            if (gprim.get_primvar("displayOpacity", &primvar, &error)) {
                primvar.flatten_with_indices(lightusd::value::TimeCode::Default(), &opacities);
            }
        }
        if (colors.empty() && opacities.empty()) {
            return;
        }
        if ((colors.size() > 1) || (opacities.size() > 1)) {
            add_warning(
                fmt::format(
                    "USD prim '{}': displayColor has {} and displayOpacity {} element(s) - a primitive schema prim has no topology to address them by, the first element colors the whole surface",
                    absolute_path,
                    colors.size(),
                    opacities.size()
                )
            );
        }
        const lightusd::value::color3f color   = colors.empty()    ? lightusd::value::color3f{1.0f, 1.0f, 1.0f} : colors.front();
        const float                    opacity = opacities.empty() ? 1.0f : opacities.front();
        const GEO::vec4f               value{color.r, color.g, color.b, opacity};
        erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        const GEO::Mesh&                 geo_mesh   = geometry.get_mesh();
        for (GEO::index_t corner = 0; corner < geo_mesh.facet_corners.nb(); ++corner) {
            attributes.corner_color_0.set(corner, value);
        }
    }

    // The geometry one primitive-schema prim describes, in the prim's own
    // local space: USD centers each of these shapes on the origin, so the
    // generator output is centered too and only the axis rotation is baked
    // in. Null when the prim is not of the type its `typeName` names.
    [[nodiscard]] auto build_primitive_schema_geometry(
        const lightusd::Prim& prim,
        const std::string&    type_name,
        const std::string&    absolute_path,
        const std::string&    name
    ) -> std::shared_ptr<erhe::geometry::Geometry>
    {
        std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>(name);
        GEO::Mesh&                                geo_mesh = geometry->get_mesh();
        glm::mat4                                 rotation{1.0f};
        const lightusd::GPrim*                    gprim = nullptr;
        if (type_name == "Cube") {
            const lightusd::GeomCube* cube = prim.as<lightusd::GeomCube>();
            if (cube == nullptr) {
                return {};
            }
            gprim = cube;
            const float size = static_cast<float>(read_schema_double(cube->size, 2.0));
            erhe::geometry::shapes::make_box(geo_mesh, size, size, size);
        } else if (type_name == "Sphere") {
            const lightusd::GeomSphere* sphere = prim.as<lightusd::GeomSphere>();
            if (sphere == nullptr) {
                return {};
            }
            gprim = sphere;
            erhe::geometry::shapes::make_sphere(
                geo_mesh,
                static_cast<float>(read_schema_double(sphere->radius, 1.0)),
                c_schema_sphere_slice_count,
                c_schema_sphere_stack_count
            );
        } else if (type_name == "Cone") {
            const lightusd::GeomCone* cone = prim.as<lightusd::GeomCone>();
            if (cone == nullptr) {
                return {};
            }
            gprim = cone;
            const float height = static_cast<float>(read_schema_double(cone->height, 2.0));
            const float radius = static_cast<float>(read_schema_double(cone->radius, 1.0));
            erhe::geometry::shapes::make_cone(
                geo_mesh,
                -0.5f * height,
                 0.5f * height,
                radius,
                true, // the base disc is part of the surface
                c_schema_slice_count,
                c_schema_stack_count
            );
            rotation = rotation_from_x_axis(cone->axis.get_value());
        } else if (type_name == "Cylinder") {
            const lightusd::GeomCylinder* cylinder = prim.as<lightusd::GeomCylinder>();
            if (cylinder == nullptr) {
                return {};
            }
            gprim = cylinder;
            const float height = static_cast<float>(read_schema_double(cylinder->height, 2.0));
            const float radius = static_cast<float>(read_schema_double(cylinder->radius, 1.0));
            erhe::geometry::shapes::make_cylinder(
                geo_mesh,
                -0.5f * height,
                 0.5f * height,
                radius,
                true,
                true,
                c_schema_slice_count,
                c_schema_stack_count
            );
            rotation = rotation_from_x_axis(cylinder->axis.get_value());
        } else if (type_name == "Cylinder_1") {
            const lightusd::GeomCylinder_1* cylinder = prim.as<lightusd::GeomCylinder_1>();
            if (cylinder == nullptr) {
                return {};
            }
            gprim = cylinder;
            const float height        = static_cast<float>(read_schema_double(cylinder->height,       2.0));
            const float radius_top    = static_cast<float>(read_schema_double(cylinder->radiusTop,    1.0));
            const float radius_bottom = static_cast<float>(read_schema_double(cylinder->radiusBottom, 1.0));
            erhe::geometry::shapes::make_conical_frustum(
                geo_mesh,
                -0.5f * height,
                 0.5f * height,
                radius_bottom,
                radius_top,
                true,
                true,
                c_schema_slice_count,
                c_schema_stack_count
            );
            rotation = rotation_from_x_axis(cylinder->axis.get_value());
        } else if (type_name == "Capsule") {
            const lightusd::GeomCapsule* capsule = prim.as<lightusd::GeomCapsule>();
            if (capsule == nullptr) {
                return {};
            }
            gprim = capsule;
            // USD's `height` is the length of the cylindrical mid-section,
            // which is erhe's `length`; the total height is height + 2 *
            // radius in both.
            erhe::geometry::shapes::make_capsule(
                geo_mesh,
                static_cast<float>(read_schema_double(capsule->radius, 0.5)),
                static_cast<float>(read_schema_double(capsule->height, 2.0)),
                c_schema_slice_count,
                c_schema_capsule_stack_count
            );
            rotation = rotation_from_y_axis(capsule->axis.get_value());
        } else if (type_name == "Capsule_1") {
            const lightusd::GeomCapsule_1* capsule = prim.as<lightusd::GeomCapsule_1>();
            if (capsule == nullptr) {
                return {};
            }
            gprim = capsule;
            const float height        = static_cast<float>(read_schema_double(capsule->height,       1.0));
            const float radius_top    = static_cast<float>(read_schema_double(capsule->radiusTop,    0.5));
            const float radius_bottom = static_cast<float>(read_schema_double(capsule->radiusBottom, 0.5));
            // The tapered generator needs a tangent cone between the two cap
            // spheres, which exists only while neither sphere contains the
            // other. A prim that authors radii too far apart for its height
            // is one warning and the larger radius as a plain capsule.
            if ((radius_top != radius_bottom) && (std::abs(radius_top - radius_bottom) >= height)) {
                const float radius = std::max(radius_top, radius_bottom);
                add_warning(
                    fmt::format(
                        "USD prim '{}': radiusTop {} and radiusBottom {} are further apart than height {} - it becomes a capsule of radius {}",
                        absolute_path,
                        radius_top,
                        radius_bottom,
                        height,
                        radius
                    )
                );
                erhe::geometry::shapes::make_capsule(geo_mesh, radius, height, c_schema_slice_count, c_schema_capsule_stack_count);
            } else if (radius_top == radius_bottom) {
                erhe::geometry::shapes::make_capsule(geo_mesh, radius_top, height, c_schema_slice_count, c_schema_capsule_stack_count);
            } else {
                erhe::geometry::shapes::make_capsule(
                    geo_mesh,
                    radius_bottom,
                    radius_top,
                    height,
                    c_schema_slice_count,
                    c_schema_capsule_stack_count
                );
            }
            rotation = rotation_from_y_axis(capsule->axis.get_value());
        } else {
            return {};
        }

        if (rotation != glm::mat4{1.0f}) {
            erhe::geometry::transform(*geometry.get(), *geometry.get(), erhe::geometry::to_geo_mat4f(rotation));
        }
        apply_primitive_schema_display_color(*gprim, absolute_path, *geometry.get());
        // The same processing a geometry-normative USD mesh gets: facet
        // adjacency, the edges and the smooth vertex normals the wide-line
        // renderer needs. The generators write the shading normals - a
        // vertex normal for the round shapes, a corner normal for the box -
        // and leave the smooth ones to this pass.
        geometry->process(
            {
                .flags =
                    erhe::geometry::Geometry::process_flag_connect                  |
                    erhe::geometry::Geometry::process_flag_build_edges              |
                    erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
            }
        );
        return geometry;
    }

    // The erhe Mesh prim every converted mesh starts as: the flags, the
    // layer and the source path a mesh of an imported file carries.
    [[nodiscard]] auto make_mesh_shell(const std::string& name) -> std::shared_ptr<erhe::scene::Mesh>
    {
        std::shared_ptr<erhe::scene::Mesh> mesh = std::make_shared<erhe::scene::Mesh>(name);
        mesh->set_source_path(m_arguments.path);
        mesh->layer_id = m_arguments.mesh_layer_id;
        mesh->enable_flag_bits(
            erhe::Item_flags::content    |
            erhe::Item_flags::show_in_ui |
            erhe::Item_flags::id
        );
        mesh->set_value(erhe::scene::Mesh::shadow_cast_property, true);
        return mesh;
    }

    // The USD `typeName` of the prim at `absolute_path`, empty for a typeless
    // `def` and for a path the stage does not answer for.
    [[nodiscard]] auto read_prim_type_name(const std::string& absolute_path) const -> std::string
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        return (prim == nullptr) ? std::string{} : get_usd_type_name(*prim);
    }

    // The single primitive of a primitive-schema prim: the geometry its
    // schema attributes describe, with the material given. The geometry is
    // normative, the way a `subdivisionScheme = none` mesh is, so the item
    // carries a Geometry and its edges.
    void add_primitive_schema_primitive(
        erhe::scene::Mesh&                                mesh,
        const std::string&                                absolute_path,
        const std::string&                                type_name,
        const std::shared_ptr<erhe::primitive::Material>& material
    )
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        const std::shared_ptr<erhe::geometry::Geometry> geometry = (prim != nullptr)
            ? build_primitive_schema_geometry(*prim, type_name, absolute_path, mesh.get_name())
            : std::shared_ptr<erhe::geometry::Geometry>{};
        if (!geometry) {
            add_warning(
                fmt::format(
                    "USD prim '{}' of type '{}' carries no readable schema attributes - it becomes a mesh with no geometry",
                    absolute_path,
                    type_name
                )
            );
            return;
        }
        mesh.add_primitive(std::make_shared<erhe::primitive::Primitive>(geometry), material);
    }

    // A primitive-schema prim the render-scene conversion did not reach: the
    // `_1` schema variants are not among the types Tydra tessellates, so the
    // prim reaches convert_node with no content and the mesh is made here.
    // The material is the one the prim's own `material:binding` names -
    // Tydra resolved none for a prim it did not convert.
    [[nodiscard]] auto make_primitive_schema_mesh(
        const std::string& absolute_path,
        const std::string& type_name,
        const std::string& name
    ) -> std::shared_ptr<erhe::scene::Mesh>
    {
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return {};
        }
        std::shared_ptr<erhe::scene::Mesh> mesh = make_mesh_shell(name);
        add_primitive_schema_primitive(
            *mesh.get(),
            absolute_path,
            type_name,
            find_material_by_path(read_prim_material_binding(*prim))
        );
        if (mesh->get_primitives().empty()) {
            return {};
        }
        m_result.data.meshes.push_back(mesh);
        // The mesh holds one primitive and no GeomSubset, so a variant that
        // binds a material at the prim's own path names that one primitive.
        m_mesh_group_names.push_back(std::vector<std::string>{std::string{}});
        m_mesh_by_path.emplace(
            absolute_path,
            Mesh_prim{.mesh = mesh, .template_index = m_mesh_group_names.size() - 1}
        );
        return mesh;
    }

    // The `material:binding` of one primitive-schema prim. Every one of these
    // types derives from GPrim, which holds its relationships in `props`.
    [[nodiscard]] static auto read_prim_material_binding(const lightusd::Prim& prim) -> std::string
    {
        if (const lightusd::GeomCube*       typed = prim.as<lightusd::GeomCube      >(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomSphere*     typed = prim.as<lightusd::GeomSphere    >(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomCone*       typed = prim.as<lightusd::GeomCone      >(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomCylinder*   typed = prim.as<lightusd::GeomCylinder  >(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomCylinder_1* typed = prim.as<lightusd::GeomCylinder_1>(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomCapsule*    typed = prim.as<lightusd::GeomCapsule   >(); typed != nullptr) { return read_material_binding(typed->props); }
        if (const lightusd::GeomCapsule_1*  typed = prim.as<lightusd::GeomCapsule_1 >(); typed != nullptr) { return read_material_binding(typed->props); }
        return std::string{};
    }

    // The geometry of every `Brush` prim the layer walk recorded: the prim's
    // `def Mesh "geometry"` child, converted the way every other mesh of the
    // file is (doc/usd-compatibility-plan.md E4a). The mesh is not scene
    // content - convert_node stops at the brush prim - so only its geometry
    // is taken. A brush prim without such a child is one warning and no
    // brush record.
    void resolve_brush_geometry()
    {
        std::vector<Usd_brush_prim> brushes;
        brushes.reserve(m_result.data.brushes.size());
        for (Usd_brush_prim& brush : m_result.data.brushes) {
            const std::string geometry_path = brush.stage_path + "/" + std::string{c_brush_geometry_prim_name};
            const std::map<std::string, std::size_t>::const_iterator i = m_mesh_index_by_path.find(geometry_path);
            if (i == m_mesh_index_by_path.end()) {
                add_warning(
                    fmt::format(
                        "USD brush prim '{}' holds no '{}' Mesh child - it becomes no brush",
                        brush.stage_path,
                        c_brush_geometry_prim_name
                    )
                );
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh>& mesh = m_result.data.meshes[i->second];
            if (mesh) {
                for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                    if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape) {
                        continue;
                    }
                    brush.geometry = mesh_primitive.primitive->render_shape->get_geometry();
                    if (brush.geometry) {
                        break;
                    }
                }
            }
            if (!brush.geometry) {
                add_warning(
                    fmt::format(
                        "USD brush prim '{}': the '{}' Mesh child carries no polygons - it becomes no brush",
                        brush.stage_path,
                        c_brush_geometry_prim_name
                    )
                );
                continue;
            }
            brushes.push_back(std::move(brush));
        }
        m_result.data.brushes = std::move(brushes);
    }

    // The evaluated geometry of every geometry graph the layer walk recorded:
    // the graph prim's `def Mesh "result"` child, converted the way every
    // other mesh of the file is (doc/usd-texture-graphs-plan.md section 4).
    // The mesh is not scene content - convert_node stops at the graph prim,
    // as it does at a brush - so only its geometry is taken, and a graph
    // written without one simply carries none: the nodes are what a reload
    // re-evaluates.
    void resolve_node_graph_geometry()
    {
        for (const std::pair<const std::string, std::size_t>& entry : m_geometry_graph_index_by_path) {
            const std::string result_path = entry.first + "/" + std::string{c_node_graph_result_prim_name};
            const std::map<std::string, std::size_t>::const_iterator i = m_mesh_index_by_path.find(result_path);
            if (i == m_mesh_index_by_path.end()) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Mesh>& mesh = m_result.data.meshes[i->second];
            if (!mesh) {
                continue;
            }
            Usd_node_graph& record = m_result.data.node_graphs[entry.second];
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape) {
                    continue;
                }
                record.geometry = mesh_primitive.primitive->render_shape->get_geometry();
                if (record.geometry) {
                    break;
                }
            }
        }
    }

    void convert_cameras()
    {
        m_result.data.cameras.reserve(m_scene->cameras.size());
        for (std::size_t camera_index = 0, end = m_scene->cameras.size(); camera_index < end; ++camera_index) {
            Tydra_camera      usd_camera = m_scene->cameras[camera_index];
            const std::string camera_name = usd_camera.name.empty()
                ? fmt::format("camera_{}", camera_index)
                : usd_camera.name;

            std::shared_ptr<erhe::scene::Camera> camera = std::make_shared<erhe::scene::Camera>(camera_name);
            camera->set_source_path(m_arguments.path);
            camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

            // Only an authored camera attribute becomes a local value
            // (doc/usd-compatibility-plan.md I2); the field of view and the
            // orthographic extents are derived from focalLength and the
            // apertures, so they are written when any of those is authored.
            const std::string& path             = usd_camera.abs_path;
            const bool         lens_authored    =
                is_authored(path, "focalLength")        ||
                is_authored(path, "horizontalAperture") ||
                is_authored(path, "verticalAperture");
            const bool         range_authored   = is_authored(path, "clippingRange");
            const bool         ortho            = usd_camera.projection == lightusd::GeomCamera::Projection::Orthographic;

            if (range_authored) {
                camera->set_value(erhe::scene::Camera::z_near_property,         usd_camera.znear);
                camera->set_value(erhe::scene::Camera::z_far_property,          usd_camera.zfar);
                camera->set_value(erhe::scene::Camera::infinite_z_far_property, false);
            }
            if (ortho) {
                // A USD aperture is in tenths of a scene unit, so the
                // orthographic view spans aperture / 10 world units.
                camera->set_value(erhe::scene::Camera::projection_type_property, erhe::scene::Projection::Type::orthogonal);
                if (is_authored(path, "horizontalAperture")) {
                    const float width = usd_camera.horizontalAperture * 0.1f;
                    camera->set_value(erhe::scene::Camera::ortho_width_property, width);
                    camera->set_value(erhe::scene::Camera::ortho_left_property, -0.5f * width);
                }
                if (is_authored(path, "verticalAperture")) {
                    const float height = usd_camera.verticalAperture * 0.1f;
                    camera->set_value(erhe::scene::Camera::ortho_height_property, height);
                    camera->set_value(erhe::scene::Camera::ortho_bottom_property, -0.5f * height);
                }
            } else if (lens_authored) {
                camera->set_value(erhe::scene::Camera::projection_type_property, erhe::scene::Projection::Type::perspective_vertical);
                camera->set_value(erhe::scene::Camera::fov_y_property, usd_camera.yfov());
                camera->set_value(erhe::scene::Camera::fov_x_property, usd_camera.xfov());
            }
            if (is_authored(path, "exposure")) {
                camera->set_exposure(usd_camera.exposure);
            }
            m_result.data.cameras.push_back(camera);
        }
    }

    // A `DomeLight` becomes the scene's ambient light: the dome's constant
    // radiance is `inputs:color * inputs:intensity * 2^inputs:exposure`, and
    // that is what the light block's ambient term holds. The first dome of a
    // file sets it; a second one is a warning and is recorded but not
    // composed, because erhe has one ambient term.
    void convert_dome_light(const Tydra_light& usd_light, const std::string& light_name)
    {
        const glm::vec3 color{usd_light.color[0], usd_light.color[1], usd_light.color[2]};
        const float     scale     = usd_light.intensity * std::pow(2.0f, usd_light.exposure);
        const bool      is_first  = m_result.data.dome_lights.empty();

        m_result.data.dome_lights.push_back(
            Usd_dome_light{
                .name         = light_name,
                .stage_path   = usd_light.abs_path,
                .color        = color,
                .intensity    = usd_light.intensity,
                .exposure     = usd_light.exposure,
                .texture_file = usd_light.textureFile
            }
        );
        if (is_first) {
            m_result.data.ambient_light = color * scale;
        } else {
            log_usd->warn(
                "USD light '{}': the file authors more than one DomeLight - erhe has one ambient light, so only the first is used",
                light_name
            );
        }
        if (!usd_light.textureFile.empty()) {
            log_usd->warn(
                "USD light '{}': DomeLight texture '{}' is not sampled - erhe has no environment map, so the dome contributes its constant color only",
                light_name,
                usd_light.textureFile
            );
        }
    }

    void convert_lights()
    {
        m_result.data.lights.resize(m_scene->lights.size());
        for (std::size_t light_index = 0, end = m_scene->lights.size(); light_index < end; ++light_index) {
            const Tydra_light& usd_light = m_scene->lights[light_index];
            const std::string  light_name = usd_light.name.empty()
                ? fmt::format("light_{}", light_index)
                : usd_light.name;

            erhe::scene::Light_type light_type = erhe::scene::Light_type::point;
            switch (usd_light.type) {
                case Tydra_light::Type::Distant: {
                    light_type = erhe::scene::Light_type::directional;
                    break;
                }
                case Tydra_light::Type::Point:
                case Tydra_light::Type::Sphere: {
                    // UsdLux puts a spot cone on a sphere light through
                    // ShapingAPI; without the schema it stays a point light.
                    light_type = has_api_schema(usd_light.abs_path, lightusd::APISchemas::APIName::ShapingAPI)
                        ? erhe::scene::Light_type::spot
                        : erhe::scene::Light_type::point;
                    break;
                }
                case Tydra_light::Type::Disk:
                case Tydra_light::Type::Rect:
                case Tydra_light::Type::Cylinder: {
                    log_usd->info(
                        "USD light '{}': area lights have no erhe counterpart - imported as a point light",
                        light_name
                    );
                    light_type = erhe::scene::Light_type::point;
                    break;
                }
                case Tydra_light::Type::Dome: {
                    // erhe has no environment map: a dome light is the
                    // scene's ambient light (doc/usd_compatibility.md,
                    // Lights). The prim is recorded so a save spells it back.
                    convert_dome_light(usd_light, light_name);
                    continue;
                }
                default: {
                    log_usd->info("USD light '{}': light type has no erhe counterpart - skipped", light_name);
                    continue;
                }
            }

            std::shared_ptr<erhe::scene::Light> light = std::make_shared<erhe::scene::Light>(light_name);
            light->set_source_path(m_arguments.path);
            // The prim's own type is always authored, so the light type is
            // always a local value; every other field is written only when
            // the UsdLux input it comes from carries an authored opinion
            // (doc/usd-compatibility-plan.md I2).
            light->set_light_type(light_type);
            const std::string& path = usd_light.abs_path;
            if (is_authored(path, "inputs:color")) {
                light->set_color(glm::vec3{usd_light.color[0], usd_light.color[1], usd_light.color[2]});
            }
            // UsdLux intensity and exposure are one photometric quantity;
            // erhe has no exposure on a light, so the two combine. No unit
            // conversion is applied (see doc/usd_compatibility.md, Lights).
            if (is_authored(path, "inputs:intensity") || is_authored(path, "inputs:exposure")) {
                light->set_intensity(usd_light.intensity * std::pow(2.0f, usd_light.exposure));
            }
            if (usd_light.enableColorTemperature && is_authored(path, "inputs:colorTemperature")) {
                light->set_temperature(usd_light.colorTemperature);
            }
            if ((light_type == erhe::scene::Light_type::spot) && is_authored(path, "inputs:shaping:cone:angle")) {
                const float outer = glm::radians(usd_light.shapingConeAngle);
                light->set_outer_spot_angle(outer);
                light->set_inner_spot_angle(outer * std::max(0.0f, 1.0f - usd_light.shapingConeSoftness));
            }
            if (is_authored(path, "inputs:shadow:enable")) {
                light->set_cast_shadow(usd_light.shadowEnable);
            }
            light->layer_id = 0;
            light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
            m_result.data.lights[light_index] = light;
        }
    }

    // upAxis and metersPerUnit of the stage as one transform applied to the
    // top-level imported nodes (doc/usd_compatibility.md, stage-level
    // constants): erhe, like glTF, is Y-up and metres.
    //
    // Only the ROOT layer of a stage carries these: USD composes a reference
    // or payload target without re-applying the target file's own upAxis and
    // metersPerUnit, and the composing stage's correction already reaches the
    // target's content through the carrier prim. A `referenced` load
    // therefore applies the identity (Stage_metrics, usd.hpp), while
    // Usd_data::up_axis / meters_per_unit still report the file's own values.
    [[nodiscard]] auto make_stage_transform() const -> glm::mat4
    {
        glm::mat4 transform{1.0f};
        if (m_arguments.stage_metrics == Stage_metrics::referenced) {
            return transform;
        }
        // USD's upAxis takes only `Y` and `Z`. Hydra applies no up-axis
        // transform to the geometry whatever the token says (the axis only
        // orients usdview's default camera, where an unknown token reads as
        // Y), so a file authoring `X` or a misspelling renders as Y-up there
        // and here alike.
        if (m_result.data.up_axis == "Z") {
            transform = glm::rotate(transform, -glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        } else if (!m_result.data.up_axis.empty() && (m_result.data.up_axis != "Y")) {
            log_usd->warn("USD stage up axis '{}' is not a valid upAxis (Y or Z) - imported as Y-up, as usdview renders it", m_result.data.up_axis);
        }
        const float scale = static_cast<float>(m_result.data.meters_per_unit);
        if (scale != 1.0f) {
            transform = glm::scale(transform, glm::vec3{scale, scale, scale});
        }
        return transform;
    }

    void convert_nodes()
    {
        const glm::mat4 stage_transform = make_stage_transform();
        for (const Tydra_node& usd_node : m_scene->nodes) {
            convert_node(usd_node, m_arguments.root_node, stage_transform);
        }
    }

    // The prim's USD `typeName`: the token a generic `Model` prim carries -
    // empty for a typeless `def` - and the schema class name of every typed
    // prim. It is what decides the erhe class of the prim
    // (doc/usd-compatibility-plan.md C5).
    [[nodiscard]] static auto get_usd_type_name(const lightusd::Prim& prim) -> std::string
    {
        return (prim.type_name() == "Model") ? prim.prim_type_name() : prim.type_name();
    }

    // The prim types that contribute no prim of the erhe tree. A `Material`
    // prim is the erhe material prim (U4, see place_material); the `Shader`
    // and `NodeGraph` prims below it are its network; a `GeomSubset`'s facets
    // already ride a primitive of its mesh; and a `DomeLight` is the scene's
    // ambient light (doc/usd-compatibility-plan.md S1), which
    // convert_dome_light() reads. Tydra lists each as a transform node all
    // the same. A prim of one of these types that does hold scene content
    // below it stays as a transform node, so the content keeps its place.
    [[nodiscard]] static auto is_contentless_prim_type(const std::string& type_name) -> bool
    {
        return
            (type_name == "Material")  ||
            (type_name == "Shader")    ||
            (type_name == "NodeGraph") ||
            (type_name == "GeomSubset")||
            (type_name == "DomeLight");
    }

    // The typeNames erhe has a transformable class for: the `Xform` prim
    // itself, and the prim types whose content the conversion attaches to
    // the node the prim becomes (U2 and U3 make those their own classes).
    [[nodiscard]] static auto is_xformable_prim_type(const std::string& type_name) -> bool
    {
        return
            (type_name == "Xform")         ||
            (type_name == "Mesh")          ||
            (type_name == "Camera")        ||
            (type_name == "SphereLight")   ||
            (type_name == "DistantLight")  ||
            (type_name == "DomeLight")     ||
            (type_name == "RectLight")     ||
            (type_name == "DiskLight")     ||
            (type_name == "CylinderLight") ||
            (type_name == "GeometryLight") ||
            (type_name == c_point_instancer_prim_type_name) ||
            is_primitive_schema_prim_type(type_name);
    }

    // A prim that authors a `references` or `payload` arc is a carrier: the
    // arcs become one prefab instance each, and an instance is a node
    // attachment, so a carrier has to be transformable
    // (doc/usd-compatibility-plan.md X1, S1). USD gives a typeless
    // referencing prim the type of the composed target, which LightUSD does
    // not compose at load, so the prim erhe sees is typeless; erhe imports it
    // - and a `Scope` carrier, whose composed content is transformable all
    // the same - as an `Xform`, which carries the prim's own authored
    // transform, the identity when it authors none. A carrier
    // of another type carries no transform and cannot hold an instance: its
    // arcs are dropped, with one warning naming the type
    // (resolve_usd_references).
    [[nodiscard]] auto imports_as_arc_carrier(const Tydra_node& usd_node, const std::string& type_name) -> bool
    {
        if (!type_name.empty() && (type_name != "Scope")) {
            return false;
        }
        return !read_prim_references(usd_node.abs_path).empty();
    }

    [[nodiscard]] auto subtree_has_scene_content(const Tydra_node& usd_node) const -> bool
    {
        // A mesh, camera, punctual light, skeleton or volume is content; a
        // transform node and an environment (dome) light are not - a dome is
        // the scene's ambient light and holds no place in the tree.
        const bool is_content =
            (usd_node.nodeType != lightusd::tydra::NodeType::Xform) &&
            (usd_node.nodeType != lightusd::tydra::NodeType::EnvmapLight);
        if (is_content) {
            return true;
        }
        for (const Tydra_node& usd_child : usd_node.children) {
            if (subtree_has_scene_content(usd_child)) {
                return true;
            }
        }
        return false;
    }

    // One prim of the composed stage as one prim of the erhe tree: the class
    // its `typeName` names (doc/usd-compatibility-plan.md C5). A prim the
    // stage lookup does not answer for is a transform node - that is what
    // Tydra reports it as.
    void convert_node(
        const Tydra_node&                       usd_node,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::mat4&                        extra_transform
    )
    {
        // A point instancer's prototype is held abstract for the reason a
        // `class` prim's is (doc/usd-compatibility-plan.md S1, X3): the
        // subtree converts with `Item_flags::content` clear, so it draws
        // nothing where the instancer sits and only the instances' clones do.
        if (m_point_instancer_prototype_paths.count(usd_node.abs_path) != 0) {
            ++m_prototype_depth;
            convert_tree_node(usd_node, parent, extra_transform);
            --m_prototype_depth;
            return;
        }
        convert_tree_node(usd_node, parent, extra_transform);
    }

    void convert_tree_node(
        const Tydra_node&                       usd_node,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::mat4&                        extra_transform
    )
    {
        // A `class` prim defines nothing: it is a style, and the caller makes
        // the Style item from the record read_layer_composition made
        // (doc/usd-compatibility-plan.md X3). Tydra reports it as a transform
        // node all the same, so the class prim itself is left out here - its
        // `def` descendants are the prototypes it holds, and they are prims.
        if (m_class_paths.count(usd_node.abs_path) != 0) {
            convert_class_prototypes(usd_node, parent, extra_transform);
            return;
        }
        // A `Brush` prim is editor state, not scene content
        // (doc/usd-compatibility-plan.md E4a): the caller makes the Brush item
        // from the record read_layer_composition made, and its child `Mesh`
        // holds the brush geometry rather than a mesh of the scene, so the
        // whole subtree is left out here.
        if (m_brush_paths.count(usd_node.abs_path) != 0) {
            return;
        }
        // A marked `NodeGraph` prim is a texture graph asset
        // (doc/usd-texture-graphs-plan.md R1): the caller rebuilds it from the
        // record, and its `Shader` children are the graph's nodes, so the
        // whole subtree is left out here.
        if (m_node_graph_paths.count(usd_node.abs_path) != 0) {
            return;
        }
        const lightusd::Prim* prim      = find_prim(usd_node.abs_path);
        const std::string     type_name = (prim != nullptr) ? get_usd_type_name(*prim) : std::string{"Xform"};
        // A `Skeleton` prim carries a transform and holds one `Xform` prim
        // per joint, which no other prim type does
        // (doc/usd-compatibility-plan.md K1).
        if ((type_name == c_skeleton_prim_type_name) && (m_skeleton_index_by_path.count(usd_node.abs_path) != 0)) {
            convert_skeleton(usd_node, parent, extra_transform);
            return;
        }
        if (is_contentless_prim_type(type_name) && !subtree_has_scene_content(usd_node)) {
            if (type_name == "Material") {
                place_material(usd_node, parent);
            }
            return;
        }
        if (!is_xformable_prim_type(type_name) && !imports_as_arc_carrier(usd_node, type_name)) {
            convert_prim(usd_node, type_name, parent, extra_transform);
            return;
        }
        const std::string node_name = usd_node.prim_name.empty()
            ? fmt::format("node_{}", m_result.data.nodes.size())
            : usd_node.prim_name;
        // A `Mesh`, `Camera` or UsdLux prim IS the erhe prim of that class
        // (doc/usd-compatibility-plan.md C5): its own xformOps are its
        // transform, and a prim under an `Xform` composes with it.
        std::shared_ptr<erhe::Item_base> content             = take_node_content(usd_node);
        Composed_transform               composed_transform  = Composed_transform::evaluated;
        // A primitive-schema prim the render scene does not carry - the `_1`
        // schema variants - is the mesh its schema attributes describe, built
        // here from the raw prim (S1). Tydra evaluated no transform for such
        // a prim either, so the authored stack is what it has.
        if (!content && is_primitive_schema_prim_type(type_name)) {
            content            = make_primitive_schema_mesh(usd_node.abs_path, type_name, node_name);
            composed_transform = Composed_transform::unevaluated;
        }
        // A typeless arc carrier is a LightUSD `Model`, for which Tydra
        // evaluates no transform (the identity): its authored stack is its
        // transform (read_xform_op_stack).
        if (type_name.empty()) {
            composed_transform = Composed_transform::unevaluated;
        }
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(content);
        if (!node) {
            node = (type_name == c_point_instancer_prim_type_name)
                ? std::static_pointer_cast<erhe::scene::Node>(std::make_shared<erhe::scene::Point_instancer>(node_name))
                : std::static_pointer_cast<erhe::scene::Node>(std::make_shared<erhe::scene::Xform>(node_name));
        } else {
            node->set_name(node_name);
        }
        node->set_source_path(m_arguments.path);
        apply_prim_flags(*node.get());
        node->Hierarchy::set_parent(parent);
        apply_local_transform(*node.get(), usd_node, extra_transform, composed_transform);
        node->update_world_from_node();
        node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        m_result.data.nodes.push_back(node);

        // The prim's own opinions: `visibility` and `purpose` on the node
        // that holds its place in the scene graph, and every `erhe:` custom
        // attribute on the item the name resolves against. Recorded rather
        // than applied here - see apply_authored_opinions().
        m_authored_opinions.push_back(
            Authored_opinions{
                .absolute_path     = usd_node.abs_path,
                .visibility_target = node.get(),
                .primary           = content.get(),
                .secondary         = node.get()
            }
        );
        record_inherits(usd_node.abs_path, node);
        record_variant_sets(usd_node.abs_path, node);
        record_mesh_prim(usd_node, content);

        if (record_references(usd_node, node)) {
            return; // the prims below came from the arcs; the targets supply them
        }

        // A `PointInstancer` converts its children itself: the prototypes
        // among them are held abstract, and the instances are prims it adds
        // (doc/usd-compatibility-plan.md S1).
        if (type_name == c_point_instancer_prim_type_name) {
            convert_point_instancer(usd_node, node);
            return;
        }

        const glm::mat4 child_transform{1.0f};
        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, node, child_transform);
        }
    }

    // The paths one relationship names, in the order it spells them.
    static void read_relationship_paths(
        const nonstd::optional<lightusd::Relationship>& relationship,
        std::vector<std::string>&                       out_paths
    )
    {
        if (!relationship.has_value()) {
            return;
        }
        if (relationship.value().is_path()) {
            out_paths.push_back(relationship.value().targetPath.full_path_name());
            return;
        }
        if (relationship.value().is_pathvector()) {
            for (const lightusd::Path& path : relationship.value().targetPathVector) {
                out_paths.push_back(path.full_path_name());
            }
        }
    }

    // The prototype paths of one instancer in the order the prims sit below
    // it, depth first.
    static void collect_prototype_tree_order(
        const Tydra_node&               usd_node,
        const std::vector<std::string>& prototype_paths,
        std::vector<std::string>&       out_paths
    )
    {
        for (const Tydra_node& usd_child : usd_node.children) {
            if (std::find(prototype_paths.begin(), prototype_paths.end(), usd_child.abs_path) != prototype_paths.end()) {
                out_paths.push_back(usd_child.abs_path);
                continue;
            }
            collect_prototype_tree_order(usd_child, prototype_paths, out_paths);
        }
    }

    // True when `path` is `prefix` or a prim below it.
    [[nodiscard]] static auto is_descendant_path(const std::string& path, const std::string& prefix) -> bool
    {
        if (path == prefix) {
            return true;
        }
        return (path.size() > prefix.size()) &&
               (path.compare(0, prefix.size(), prefix) == 0) &&
               (path[prefix.size()] == '/');
    }

    // One `PointInstancer` prim as prims (doc/usd-compatibility-plan.md S1).
    // The prototypes are held abstract where the file put them - the `class`
    // abstraction of X3, applied to the same effect - and every instance
    // becomes a child `Xform` carrying an internal reference to its
    // prototype, so the caller instantiates it exactly as it instantiates
    // every other arc and every instance of one prototype shares one
    // template.
    //
    // Time-sampled arrays, `velocities` and per-instance primvars are not
    // read: the arrays are sampled at the default time
    // (src/erhe/usd/notes.md, PointInstancer).
    void convert_point_instancer(const Tydra_node& usd_node, const std::shared_ptr<erhe::scene::Node>& node)
    {
        const lightusd::Prim*               prim      = find_prim(usd_node.abs_path);
        const lightusd::GeomPointInstancer* instancer = (prim != nullptr)
            ? prim->as<lightusd::GeomPointInstancer>()
            : nullptr;

        Usd_point_instancer record{};
        record.item       = node;
        record.stage_path = usd_node.abs_path;
        if (instancer != nullptr) {
            read_relationship_paths(instancer->prototypes, record.prototype_paths);
        }
        // The prototype subtrees convert with `Item_flags::content` clear, so
        // nothing of them renders where the instancer sits; a reference that
        // names one gives its clone the flag back.
        for (const std::string& prototype_path : record.prototype_paths) {
            m_point_instancer_prototype_paths.insert(prototype_path);
        }

        const glm::mat4 child_transform{1.0f};
        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, node, child_transform);
        }

        // The prototypes are put in the order they sit in the tree, which is
        // the order a save writes `rel prototypes` back in: the writer has
        // the tree and not the relationship, so the two agree only if the
        // load adopts the tree's order. `protoIndices` is remapped onto it
        // below.
        std::vector<std::string> authored_prototype_paths = record.prototype_paths;
        std::vector<std::string> tree_order;
        collect_prototype_tree_order(usd_node, authored_prototype_paths, tree_order);
        for (const std::string& prototype_path : authored_prototype_paths) {
            if (std::find(tree_order.begin(), tree_order.end(), prototype_path) == tree_order.end()) {
                tree_order.push_back(prototype_path); // not held by the instancer; warned below
            }
        }
        record.prototype_paths = tree_order;

        if (instancer == nullptr) {
            add_warning(
                fmt::format(
                    "USD prim '{}' of type 'PointInstancer' carries no readable schema attributes - it instances nothing",
                    usd_node.abs_path
                )
            );
            m_result.data.point_instancers.push_back(std::move(record));
            return;
        }

        // The authored `protoIndices` index the authored `prototypes`; the
        // record indexes the tree order, and so does the writer.
        std::vector<int32_t> proto_indices = instancer->get_protoIndices();
        for (int32_t& proto_index : proto_indices) {
            const std::size_t authored = static_cast<std::size_t>(std::max(proto_index, 0));
            if (authored >= authored_prototype_paths.size()) {
                continue;
            }
            const std::vector<std::string>& tree_paths = record.prototype_paths;
            const std::vector<std::string>::const_iterator i = std::find(
                tree_paths.cbegin(),
                tree_paths.cend(),
                authored_prototype_paths[authored]
            );
            proto_index = static_cast<int32_t>(std::distance(tree_paths.cbegin(), i));
        }

        std::vector<lightusd::value::matrix4d> matrices;
        std::string                            error;
        if (!lightusd::ComputeInstanceTransformsAtTime(
                *instancer,
                lightusd::value::TimeCode::Default(),
                lightusd::value::TimeSampleInterpolationType::Linear,
                &matrices,
                &error
            )
        ) {
            add_warning(
                fmt::format(
                    "USD prim '{}': the instance transforms could not be computed ({}) - it instances nothing",
                    usd_node.abs_path,
                    error
                )
            );
            m_result.data.point_instancers.push_back(std::move(record));
            return;
        }
        std::vector<bool> mask;
        if (!lightusd::ComputeMaskAtTime(*instancer, lightusd::value::TimeCode::Default(), &mask, &error)) {
            mask.clear();
        }
        const std::vector<int64_t> ids = instancer->get_ids();

        // A prototype the stage does not answer for names nothing to
        // instance: one warning per prototype, and its instances are left
        // out.
        std::vector<bool> prototype_ok;
        prototype_ok.reserve(record.prototype_paths.size());
        for (const std::string& prototype_path : record.prototype_paths) {
            const bool resolved = (find_prim(prototype_path) != nullptr);
            if (!resolved) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': prototype '{}' is not a prim of the stage - its instances are left out",
                        usd_node.abs_path,
                        prototype_path
                    )
                );
            } else if (!is_descendant_path(prototype_path, usd_node.abs_path)) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': prototype '{}' is not held by the instancer - a save writes it as a child of the instancer",
                        usd_node.abs_path,
                        prototype_path
                    )
                );
            }
            prototype_ok.push_back(resolved);
        }

        for (std::size_t instance = 0, end = matrices.size(); instance < end; ++instance) {
            if ((instance < mask.size()) && !mask[instance]) {
                continue; // `invisibleIds` / `inactiveIds`
            }
            const std::size_t proto_index = (instance < proto_indices.size())
                ? static_cast<std::size_t>(std::max(proto_indices[instance], 0))
                : 0;
            if ((proto_index >= prototype_ok.size()) || !prototype_ok[proto_index]) {
                continue;
            }
            const std::string& prototype_path = record.prototype_paths[proto_index];
            const std::size_t  separator      = prototype_path.rfind('/');
            const std::string  prototype_name = (separator == std::string::npos)
                ? prototype_path
                : prototype_path.substr(separator + 1);
            const std::string  instance_name  = (instance < ids.size())
                ? fmt::format("{}_{}", prototype_name, ids[instance])
                : fmt::format("{}_{}", prototype_name, instance);

            std::shared_ptr<erhe::scene::Node> instance_node = std::make_shared<erhe::scene::Xform>(instance_name);
            instance_node->set_source_path(m_arguments.path);
            apply_prim_flags(*instance_node.get());
            instance_node->Hierarchy::set_parent(node);
            const glm::mat4 transform = to_glm(matrices[instance]);
            instance_node->set_parent_from_node(transform);
            instance_node->update_world_from_node();
            instance_node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
            m_result.data.nodes.push_back(instance_node);

            // The instance is a referencing prim: an internal reference - an
            // empty asset path is the same layer - that names the prototype.
            m_result.data.references.push_back(
                Usd_prim_references{
                    .item       = instance_node,
                    .stage_path = fmt::format("{}/{}", usd_node.abs_path, instance_node->get_name()),
                    .references = std::vector<Usd_reference>{
                        Usd_reference{
                            .asset_path = std::string{},
                            .prim_path  = prototype_path,
                            .kind       = Usd_reference_kind::reference
                        }
                    },
                    .overrides  = std::vector<erhe::scene::Instance_override>{}
                }
            );
            record.instances.push_back(Usd_point_instance{.proto_index = proto_index, .transform = transform});
            record.instance_items.push_back(instance_node);
        }
        m_result.data.point_instancers.push_back(std::move(record));
    }

    // The prototypes one `class` prim holds (doc/usd-compatibility-plan.md
    // X3): every `def` descendant is an ordinary prim, converted where the
    // class prim's own holder is - the class prim becomes a Style item, which
    // is the caller's to make, and the caller moves the prototype under it. A
    // `class` descendant is a class of its own and is walked for the
    // prototypes IT holds.
    void convert_class_prototypes(
        const Tydra_node&                       usd_node,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::mat4&                        extra_transform
    )
    {
        for (const Tydra_node& usd_child : usd_node.children) {
            if (m_class_paths.count(usd_child.abs_path) != 0) {
                convert_class_prototypes(usd_child, parent, extra_transform);
                continue;
            }
            if (m_class_prototype_paths.count(usd_child.abs_path) == 0) {
                continue;
            }
            const std::size_t child_count_before = parent->get_children().size();
            ++m_prototype_depth;
            convert_node(usd_child, parent, extra_transform);
            --m_prototype_depth;
            if (parent->get_children().size() == child_count_before) {
                continue; // the prim contributed no item of its own
            }
            m_result.data.class_prototypes.push_back(
                Usd_class_prototype{
                    .item       = parent->get_children().back(),
                    .stage_path = usd_child.abs_path,
                    .class_path = usd_node.abs_path
                }
            );
        }
    }

    // The item flags every imported prim carries. A prototype held by a
    // `class` prim is out of the render, the pick and the shadow filters -
    // that is what USD's class abstraction means - so it carries no
    // `content`; a reference that clones it puts the flag back
    // (doc/usd-compatibility-plan.md X3). The content a prim carries (a mesh,
    // a camera, a light) is built by an earlier pass that gave it the flag,
    // so a prototype prim has it taken away here rather than never given.
    void apply_prim_flags(erhe::Item_base& item) const
    {
        item.enable_flag_bits(erhe::Item_flags::show_in_ui);
        if (m_prototype_depth > 0) {
            item.disable_flag_bits(erhe::Item_flags::content);
        } else {
            item.enable_flag_bits(erhe::Item_flags::content);
        }
    }

    // The material a `Material` prim became, parented where the stage puts
    // that prim (doc/usd-compatibility-plan.md U4): a material is a prim of
    // the tree, so a stage that keeps its materials in `/Looks` gives erhe a
    // `Scope` named `Looks` holding them. The prim name is the material's
    // name, so two materials of one name in two scopes stay apart by their
    // place; the shading network below the prim stays namespace.
    void place_material(const Tydra_node& usd_node, const std::shared_ptr<erhe::Hierarchy>& parent)
    {
        const std::map<std::string, std::size_t>::const_iterator i = m_material_by_path.find(usd_node.abs_path);
        if (i == m_material_by_path.end()) {
            log_usd->warn(
                "USD material prim '{}' has no converted material - it is not placed in the tree",
                usd_node.abs_path
            );
            return;
        }
        const std::shared_ptr<erhe::primitive::Material>& material = m_result.data.materials[i->second];
        if (!material) {
            return;
        }
        if (!usd_node.prim_name.empty()) {
            material->set_name(usd_node.prim_name);
        }
        material->set_parent(parent);
        record_inherits(usd_node.abs_path, material);
        record_variant_sets(usd_node.abs_path, material);
    }

    // The `Skeleton` prims Tydra converted, by their stage path, and the
    // per-skeleton slots the conversion fills as it walks the tree
    // (doc/usd-compatibility-plan.md K1).
    void index_skeletons()
    {
        const std::size_t skeleton_count = m_scene->skeletons.size();
        m_skeleton_nodes.resize(skeleton_count);
        m_skeleton_joint_nodes.resize(skeleton_count);
        for (std::size_t skeleton_index = 0; skeleton_index < skeleton_count; ++skeleton_index) {
            const lightusd::tydra::SkelHierarchy& skeleton = m_scene->skeletons[skeleton_index];
            m_skeleton_joint_nodes[skeleton_index].resize(skeleton.num_joints());
            m_skeleton_index_by_path.emplace(skeleton.abs_path, skeleton_index);
        }
    }

    // One `Skeleton` prim as a transformable prim of the tree holding one
    // `Xform` prim per joint (doc/usd-compatibility-plan.md K1). USD has no
    // erhe class for a skeleton, so the prim carries the authored `Skeleton`
    // token the way a generic `Model` prim carries its own (C5), and the
    // joints - which are USD paths rather than prims - become the prims that
    // give erhe's GPU skinning a `world_from_joint` per joint.
    void convert_skeleton(
        const Tydra_node&                       usd_node,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::mat4&                        extra_transform
    )
    {
        const std::size_t skeleton_index = m_skeleton_index_by_path.at(usd_node.abs_path);
        const std::string node_name      = usd_node.prim_name.empty()
            ? fmt::format("node_{}", m_result.data.nodes.size())
            : usd_node.prim_name;

        std::shared_ptr<erhe::scene::Xformable> node = std::make_shared<erhe::scene::Xformable>(node_name);
        node->set_prim_type_name(c_skeleton_prim_type_name);
        node->set_source_path(m_arguments.path);
        apply_prim_flags(*node.get());
        node->Hierarchy::set_parent(parent);
        apply_local_transform(*node.get(), usd_node, extra_transform, Composed_transform::evaluated);
        node->update_world_from_node();
        node->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        m_result.data.nodes.push_back(node);
        m_skeleton_nodes[skeleton_index] = node;

        m_authored_opinions.push_back(
            Authored_opinions{
                .absolute_path     = usd_node.abs_path,
                .visibility_target = node.get(),
                .primary           = nullptr,
                .secondary         = node.get()
            }
        );
        record_inherits(usd_node.abs_path, node);
        record_variant_sets(usd_node.abs_path, node);

        add_skeleton_joint(m_scene->skeletons[skeleton_index].root_node, node, skeleton_index);

        if (record_references(usd_node, node)) {
            return; // the prims below came from the arcs; the targets supply them
        }
        const glm::mat4 child_transform{1.0f};
        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, node, child_transform);
        }
    }

    // One joint of a skeleton, and the joints below it. The joint's name is
    // the last segment of its `joints` path, so `Bone_1/Bone_001_1` is
    // `Bone_001_1` under `Bone_1`, and its local transform is the joint's
    // `restTransforms` entry - which is what makes the joint prim's world
    // transform the `skelLocalToWorld * jointSkelSpace` UsdSkel poses a bound
    // point with (doc/usd-compatibility-plan.md K1).
    void add_skeleton_joint(
        const lightusd::tydra::SkelNode&          skel_node,
        const std::shared_ptr<erhe::scene::Node>& parent,
        const std::size_t                         skeleton_index
    )
    {
        const std::string::size_type separator = skel_node.joint_path.rfind('/');
        const std::string            joint_name = (separator == std::string::npos)
            ? skel_node.joint_path
            : skel_node.joint_path.substr(separator + 1);

        std::shared_ptr<erhe::scene::Xform> joint = std::make_shared<erhe::scene::Xform>(
            joint_name.empty() ? fmt::format("joint_{}", skel_node.joint_id) : joint_name
        );
        joint->set_source_path(m_arguments.path);
        apply_prim_flags(*joint.get());
        joint->Hierarchy::set_parent(parent);
        joint->node_data.transforms.parent_from_node.set(to_glm(skel_node.rest_transform));
        joint->update_world_from_node();
        joint->handle_transform_update(erhe::scene::Node_transforms::get_next_serial());
        m_result.data.nodes.push_back(joint);

        std::vector<std::shared_ptr<erhe::scene::Node>>& joint_nodes = m_skeleton_joint_nodes[skeleton_index];
        if ((skel_node.joint_id >= 0) && (static_cast<std::size_t>(skel_node.joint_id) < joint_nodes.size())) {
            joint_nodes[static_cast<std::size_t>(skel_node.joint_id)] = joint;
        }
        for (const lightusd::tydra::SkelNode& child : skel_node.children) {
            add_skeleton_joint(child, joint, skeleton_index);
        }
    }

    // The skins the file's skinned meshes bind
    // (doc/usd-compatibility-plan.md K1). UsdSkel poses a bound point as
    // `skelLocalToWorld * jointSkelSpace_j * inverse(bind_j) *
    // geomBindTransform * p` and erhe's Joint_buffer poses it as
    // `world_from_joint_j * inverse_bind_j * p`, so with the joint prims
    // above supplying the first factor the inverse bind matrix erhe needs is
    // `inverse(bind_j) * geomBindTransform`. That depends on the mesh's own
    // `geomBindTransform` alone, so the meshes of one skeleton sharing a bind
    // transform share a skin.
    void build_skins()
    {
        if (m_scene->skeletons.empty()) {
            return;
        }
        // The skin of every Tydra mesh, by mesh index: what the prims the
        // mesh became - the original and every clone take_mesh made - name.
        std::vector<std::shared_ptr<erhe::scene::Skin>> skin_by_mesh;
        skin_by_mesh.resize(m_scene->meshes.size());
        for (std::size_t mesh_index = 0, end = m_scene->meshes.size(); mesh_index < end; ++mesh_index) {
            const Tydra_mesh& usd_mesh = m_scene->meshes[mesh_index];
            if (usd_mesh.skel_id < 0) {
                continue;
            }
            const std::size_t skeleton_index = static_cast<std::size_t>(usd_mesh.skel_id);
            if (skeleton_index >= m_scene->skeletons.size()) {
                continue;
            }
            const std::shared_ptr<erhe::scene::Node>& skeleton_node = m_skeleton_nodes[skeleton_index];
            if (!skeleton_node) {
                log_usd->warn(
                    "USD prim '{}': the skeleton it binds is not a prim of the tree - the mesh is not skinned",
                    usd_mesh.abs_path
                );
                continue;
            }
            const glm::mat4 geometry_from_bind = to_glm(usd_mesh.joint_and_weights.geomBindTransform);
            skin_by_mesh[mesh_index] = get_or_make_skin(skeleton_index, geometry_from_bind);
        }

        for (std::size_t mesh_index = 0, end = m_result.data.meshes.size(); mesh_index < end; ++mesh_index) {
            if ((mesh_index < skin_by_mesh.size()) && skin_by_mesh[mesh_index] && m_result.data.meshes[mesh_index]) {
                m_result.data.meshes[mesh_index]->skin = skin_by_mesh[mesh_index];
            }
        }
        for (const std::pair<const std::string, Mesh_prim>& entry : m_mesh_by_path) {
            if ((entry.second.template_index < skin_by_mesh.size()) && skin_by_mesh[entry.second.template_index] && entry.second.mesh) {
                entry.second.mesh->skin = skin_by_mesh[entry.second.template_index];
            }
        }
    }

    [[nodiscard]] auto get_or_make_skin(
        const std::size_t skeleton_index,
        const glm::mat4&  geometry_from_bind
    ) -> std::shared_ptr<erhe::scene::Skin>
    {
        for (const Skin_key& key : m_skins) {
            if ((key.skeleton_index == skeleton_index) && is_near_matrix(key.geometry_from_bind, geometry_from_bind)) {
                return key.skin;
            }
        }
        const lightusd::tydra::SkelHierarchy&           skeleton    = m_scene->skeletons[skeleton_index];
        const std::vector<std::shared_ptr<erhe::scene::Node>>& joint_nodes = m_skeleton_joint_nodes[skeleton_index];

        const std::string skin_name = skeleton.prim_name.empty()
            ? fmt::format("skin_{}", m_result.data.skins.size())
            : skeleton.prim_name;
        std::shared_ptr<erhe::scene::Skin> skin = std::make_shared<erhe::scene::Skin>(skin_name);
        skin->set_source_path(m_arguments.path);
        skin->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
        skin->skin_data.skeleton = m_skeleton_nodes[skeleton_index];
        skin->skin_data.joints.reserve(joint_nodes.size());
        skin->skin_data.inverse_bind_matrices.reserve(joint_nodes.size());
        for (std::size_t joint_index = 0; joint_index < joint_nodes.size(); ++joint_index) {
            const glm::mat4 skeleton_from_bind = (joint_index < skeleton.bind_transforms.size())
                ? to_glm(skeleton.bind_transforms[joint_index])
                : glm::mat4{1.0f};
            skin->skin_data.joints.push_back(joint_nodes[joint_index]);
            skin->skin_data.inverse_bind_matrices.push_back(glm::inverse(skeleton_from_bind) * geometry_from_bind);
        }
        m_skins.push_back(
            Skin_key{
                .skeleton_index     = skeleton_index,
                .geometry_from_bind = geometry_from_bind,
                .skin               = skin
            }
        );
        m_result.data.skins.push_back(skin);
        return skin;
    }

    // A prim whose class carries no transform: a `Scope`, and the `Typed`
    // prim every other `typeName` becomes so that its name, its place in the
    // tree and its children survive the round trip. The transform that
    // reached the prim composes with its children instead (C5), and an
    // authored transform on such a prim has nowhere to go: it is dropped,
    // with one warning naming the prim (plan section 5).
    void convert_prim(
        const Tydra_node&                       usd_node,
        const std::string&                      type_name,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::mat4&                        extra_transform
    )
    {
        const std::string prim_name = usd_node.prim_name.empty()
            ? fmt::format("prim_{}", m_result.data.prims.size())
            : usd_node.prim_name;
        std::shared_ptr<erhe::Typed> prim = (type_name == "Scope")
            ? std::static_pointer_cast<erhe::Typed>(std::make_shared<erhe::Scope>(prim_name))
            : std::make_shared<erhe::Typed>(prim_name, type_name);
        prim->set_source_path(m_arguments.path);
        apply_prim_flags(*prim.get());
        prim->set_parent(parent);
        m_result.data.prims.push_back(prim);

        if (!is_identity_matrix(to_glm(usd_node.local_matrix))) {
            log_usd->warn(
                "USD prim '{}' of type '{}' authors a transform, which a prim of this class does not carry - the transform is dropped",
                usd_node.abs_path,
                type_name
            );
        }

        m_authored_opinions.push_back(
            Authored_opinions{
                .absolute_path     = usd_node.abs_path,
                .visibility_target = prim.get(),
                .primary           = nullptr,
                .secondary         = prim.get()
            }
        );
        record_inherits(usd_node.abs_path, prim);
        record_variant_sets(usd_node.abs_path, prim);

        if (record_references(usd_node, prim)) {
            return; // the prims below came from the arcs; the targets supply them
        }

        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, prim, extra_transform);
        }
    }

    // The `references` / `payload` arcs a prim authors, in the order USD
    // composes the list-edited ops into: `references` first and `payload`
    // after it, the arc order of LIVRPS. LightUSD keeps its own list-op
    // resolution private (a static helper of composition.cc), so the rule is
    // repeated here: an unqualified op replaces the list, `prepend` inserts at
    // the front, `append` and the deprecated `add` at the back, `delete`
    // removes every entry naming the same target, and `order` is ignored.
    template <typename T>
    static void resolve_reference_list_ops(
        const std::vector<std::pair<lightusd::ListEditQual, std::vector<T>>>& list_ops,
        const Usd_reference_kind                                              kind,
        std::vector<Usd_reference>&                                           out_references
    )
    {
        const auto key = [](const Usd_reference& reference) -> std::string {
            return reference.asset_path + "|" + reference.prim_path;
        };
        std::vector<Usd_reference> resolved;
        for (const std::pair<lightusd::ListEditQual, std::vector<T>>& list_op : list_ops) {
            std::vector<Usd_reference> items;
            items.reserve(list_op.second.size());
            for (const T& entry : list_op.second) {
                Usd_reference reference{};
                reference.asset_path = entry.asset_path.GetAssetPath();
                reference.prim_path  = entry.prim_path.is_valid() ? entry.prim_path.full_path_name() : std::string{};
                reference.kind       = kind;
                if (reference.asset_path.empty() && reference.prim_path.empty()) {
                    continue; // `references = None` / `payload = None`
                }
                items.push_back(std::move(reference));
            }
            switch (list_op.first) {
                case lightusd::ListEditQual::ResetToExplicit: {
                    resolved = std::move(items);
                    break;
                }
                case lightusd::ListEditQual::Prepend: {
                    resolved.insert(resolved.begin(), items.begin(), items.end());
                    break;
                }
                case lightusd::ListEditQual::Append:
                case lightusd::ListEditQual::Add: {
                    resolved.insert(resolved.end(), items.begin(), items.end());
                    break;
                }
                case lightusd::ListEditQual::Delete: {
                    std::set<std::string> deleted_keys;
                    for (const Usd_reference& item : items) {
                        deleted_keys.insert(key(item));
                    }
                    resolved.erase(
                        std::remove_if(
                            resolved.begin(),
                            resolved.end(),
                            [&deleted_keys, &key](const Usd_reference& candidate) {
                                return deleted_keys.count(key(candidate)) != 0;
                            }
                        ),
                        resolved.end()
                    );
                    break;
                }
                default: {
                    break;
                }
            }
        }
        out_references.insert(out_references.end(), resolved.begin(), resolved.end());
    }

    [[nodiscard]] auto read_prim_references(const std::string& absolute_path) -> std::vector<Usd_reference>
    {
        std::vector<Usd_reference> references;
        const lightusd::Prim* prim = find_prim(absolute_path);
        if (prim == nullptr) {
            return references;
        }
        const lightusd::PrimMetas& metas = prim->metas();
        if (metas.references.has_value()) {
            resolve_reference_list_ops(metas.references.value(), Usd_reference_kind::reference, references);
        }
        if (metas.payload.has_value()) {
            resolve_reference_list_ops(metas.payload.value(), Usd_reference_kind::payload, references);
        }
        return references;
    }

    // A prim that authors composition arcs is a carrier: the arcs are reported
    // and the composed prims below the carrier are left out, because the
    // targets are what supply them (doc/usd-compatibility-plan.md X1). True
    // means the caller stops there. Only the arcs the prim itself authors
    // count - an arc authored inside a referenced layer is composed into that
    // target and stays flattened in the instance.
    [[nodiscard]] auto record_references(
        const Tydra_node&                       usd_node,
        const std::shared_ptr<erhe::Item_base>& item
    ) -> bool
    {
        std::vector<Usd_reference> references = read_prim_references(usd_node.abs_path);
        if (references.empty()) {
            return false;
        }
        m_result.data.references.push_back(
            Usd_prim_references{
                .item       = item,
                .stage_path = usd_node.abs_path,
                .references = std::move(references),
                .overrides  = read_instance_overrides(usd_node.abs_path)
            }
        );
        return true;
    }

    // The opinions the referencing layer authors over the prims a reference
    // contributed (doc/usd-compatibility-plan.md X2): an `over` prim below
    // the referencing prim is the sparse override of one instance item, at
    // the path it has below the carrier. LightUSD does not report which layer
    // an opinion on a composed prim came from, so the composed layer's own prim
    // specs are what is asked. A typeless `def` below a referencing prim names
    // no type and so adds nothing the target does not already have: it is
    // the override of the child of that name, exactly as an `over` is (the
    // usd-wg inherit_and_specialize.usda authors `def "source"` over a
    // referenced Cube to recolor it). A typed `def` adds structure to a
    // reference, which is out of scope (plan section 5), so it is named in a
    // warning and dropped.
    [[nodiscard]] auto read_instance_overrides(const std::string& absolute_path) -> std::vector<erhe::scene::Instance_override>
    {
        std::vector<erhe::scene::Instance_override> overrides;
        const lightusd::PrimSpec* spec = find_layer_primspec(absolute_path);
        if (spec == nullptr) {
            return overrides;
        }
        read_override_children(absolute_path, *spec, std::string{}, overrides);
        return overrides;
    }

    void read_override_children(
        const std::string&                           absolute_path,
        const lightusd::PrimSpec&                    spec,
        const std::string&                           relative_path,
        std::vector<erhe::scene::Instance_override>& overrides
    )
    {
        std::string defined_names;
        for (const lightusd::PrimSpec& child : spec.children()) {
            const bool is_override =
                (child.specifier() == lightusd::Specifier::Over) ||
                ((child.specifier() == lightusd::Specifier::Def) && child.typeName().empty());
            if (!is_override) {
                if (!defined_names.empty()) {
                    defined_names += ", ";
                }
                defined_names += child.name();
                continue;
            }
            const std::string child_path = relative_path.empty()
                ? child.name()
                : (relative_path + "/" + child.name());
            erhe::scene::Instance_override entry{};
            entry.relative_path = child_path;
            read_override_spec(absolute_path, child, entry);
            if (!entry.values.empty() || entry.transform_overridden || !entry.material_path.empty()) {
                overrides.push_back(std::move(entry));
            }
            read_override_children(absolute_path, child, child_path, overrides);
        }
        if (!defined_names.empty()) {
            log_usd->warn(
                "USD prim '{}': the referencing layer defines typed prims over the reference ({}) - a reference protects its structure, so they are dropped",
                absolute_path,
                defined_names
            );
        }
    }

    // One `over` prim spec as the override of one instance item: the
    // `erhe:Owner:name` custom attributes, `visibility` and `purpose`, the
    // `active` metadatum, the authored xformOps and the `material:binding`
    // relationship. A prim spec is what the layer authored, so every property
    // it carries is an authored opinion - no `authored()` test is needed
    // here. The `MaterialBindingAPI` the `over` applies is what the
    // relationship needs to be read as a binding, so the schema itself
    // carries no value of its own.
    void read_override_spec(
        const std::string&              absolute_path,
        const lightusd::PrimSpec&       spec,
        erhe::scene::Instance_override& entry
    )
    {
        read_spec_values(spec, entry.values);
        read_override_xform_ops(absolute_path, spec, entry);
        entry.material_path = read_spec_material_binding(spec);
    }

    // The authored opinions of one prim spec in the neutral name / text form:
    // the `erhe:Owner:name` custom attributes as `Owner.name`, `visibility`
    // and `purpose` as the erhe properties they map onto, and the `active`
    // metadatum. Shared by the `over` prims of X2 and the `class` prims of X3.
    static void read_spec_values(
        const lightusd::PrimSpec&                          spec,
        std::vector<erhe::scene::Instance_override_value>& out_values
    )
    {
        static constexpr std::string_view prefix{"erhe:"};
        for (const std::pair<const std::string, lightusd::Property>& property : spec.props()) {
            const std::string& name = property.first;
            if (!property.second.is_attribute()) {
                continue;
            }
            if (name.compare(0, prefix.size(), prefix) == 0) {
                std::string       qualified_name = name.substr(prefix.size());
                const std::size_t separator      = qualified_name.find(':');
                if (separator != std::string::npos) {
                    qualified_name[separator] = '.';
                }
                out_values.push_back(
                    erhe::scene::Instance_override_value{
                        .name = std::move(qualified_name),
                        .text = attribute_text(property.second.get_attribute())
                    }
                );
                continue;
            }
            if (name == "visibility") {
                const std::string text = attribute_text(property.second.get_attribute());
                out_values.push_back(
                    erhe::scene::Instance_override_value{.name = "visible", .text = (text == "invisible") ? "false" : "true"}
                );
                continue;
            }
            if (name == "purpose") {
                out_values.push_back(
                    erhe::scene::Instance_override_value{
                        .name = "purpose",
                        .text = to_erhe_purpose_text(attribute_text(property.second.get_attribute()))
                    }
                );
                continue;
            }
        }
        if (spec.metas().has_active()) {
            out_values.push_back(
                erhe::scene::Instance_override_value{.name = "active", .text = spec.metas().get_active() ? "true" : "false"}
            );
        }
    }

    // The authored xformOps of an `over`. A prim spec holds them as the
    // `xformOp:*` attributes and the `xformOpOrder` they were authored as, so
    // LightUSD's own reconstruction is what turns them into ops; from there
    // the M8 reader is the same one a typed prim goes through.
    void read_override_xform_ops(
        const std::string&              absolute_path,
        const lightusd::PrimSpec&       spec,
        erhe::scene::Instance_override& entry
    )
    {
        if (spec.props().find("xformOpOrder") == spec.props().end()) {
            return;
        }
        std::map<std::string, lightusd::Property> properties = spec.props();
        std::set<std::string>                     table;
        std::vector<lightusd::XformOp>            usd_ops;
        std::string                               error;
        if (!lightusd::prim::ReconstructXformOpsFromProperties(spec.specifier(), table, properties, &usd_ops, &error)) {
            log_usd->warn("USD prim '{}': the override at '{}' has unreadable xformOps: {}", absolute_path, entry.relative_path, error);
            return;
        }
        erhe::scene::Xform_op_stack stack{};
        for (const lightusd::XformOp& usd_op : usd_ops) {
            if (usd_op.op_type == lightusd::XformOp::OpType::ResetXformStack) {
                stack.reset_xform_stack = true;
                continue;
            }
            erhe::scene::Xform_op op{};
            if (!read_xform_op(usd_op, m_import_time_code, op)) {
                log_usd->warn(
                    "USD prim '{}': the override at '{}' has an xformOp of value type '{}' with no erhe counterpart - the transform is dropped",
                    absolute_path,
                    entry.relative_path,
                    usd_op.get_value_type_name()
                );
                return;
            }
            stack.ops.push_back(std::move(op));
        }
        entry.transform_overridden = true;
        entry.transform            = glm::mat4{stack.compose()};
        entry.xform_op_stack       = std::move(stack);
    }

    // A USDA attribute value in erhe's property text form (D16), the way an
    // `erhe:` custom attribute of a composed prim is read.
    [[nodiscard]] static auto attribute_text(const lightusd::Attribute& attribute) -> std::string
    {
        const std::string                literal    = lightusd::value::pprint_value(attribute.get_var().value_raw());
        const std::optional<std::string> asset_path = usd_asset_literal_path(literal);
        return asset_path.has_value() ? asset_path.value() : usd_literal_to_property_text(literal);
    }

    // The USD `purpose` token as the label erhe's Purpose enumeration parses
    // (doc/usd-compatibility-plan.md M3): the same vocabulary, capitalized.
    [[nodiscard]] static auto to_erhe_purpose_text(const std::string& token) -> std::string
    {
        if (token == "render") { return "Render"; }
        if (token == "proxy" ) { return "Proxy";  }
        if (token == "guide" ) { return "Guide";  }
        return "Default";
    }

    // The layer the stage was built from - the root layer composed with its
    // `subLayers`, variant prims hoisted in - read once by load_stage. A
    // composed prim does not say which layer an opinion came from and Tydra's
    // render-scene conversion never walks a `class` prim, so the layer's own
    // prim specs are what the reader asks for the `over` prims of X2 and the
    // `class` prims and `inherits` arcs of X3. The stage keeps it, so nothing
    // here parses the file again.
    [[nodiscard]] auto has_layer() const -> bool
    {
        return (m_impl != nullptr) && m_impl->layer_ok;
    }

    // The layer's prim spec at the given path, or null.
    [[nodiscard]] auto find_layer_primspec(const std::string& absolute_path) -> const lightusd::PrimSpec*
    {
        if (!has_layer()) {
            return nullptr;
        }
        const lightusd::PrimSpec* spec = nullptr;
        std::string               error;
        if (!m_impl->layer.find_primspec_at(lightusd::Path{absolute_path, ""}, &spec, &error)) {
            return nullptr;
        }
        return spec;
    }

    // The `class` prims and the `inherits` arcs the layer authors
    // (doc/usd-compatibility-plan.md X3), read before the prims are converted
    // so a prim's arcs are one map lookup once its item exists. The layer
    // holds its top-level prim specs in a hash map and the ascii reader fills
    // no ordering metadatum for a layer, so the top level is walked in name
    // order; the children of a prim spec keep the order the layer spells.
    void read_layer_composition()
    {
        ERHE_PROFILE_FUNCTION();
        if (!has_layer()) {
            return;
        }
        std::vector<std::string> root_names;
        root_names.reserve(m_impl->layer.primspecs().size());
        for (const std::pair<const std::string, lightusd::PrimSpec>& entry : m_impl->layer.primspecs()) {
            root_names.push_back(entry.first);
        }
        std::sort(root_names.begin(), root_names.end());
        for (const std::string& root_name : root_names) {
            const lightusd::PrimSpec& spec = m_impl->layer.primspecs().at(root_name);
            collect_layer_composition("/" + root_name, spec);
        }
    }

    // One prim spec of the layer: a `class` prim is recorded whole (its
    // descendants are classes of their own), and every other prim contributes
    // its `inherits` arcs and is walked for the classes below it.
    void collect_layer_composition(const std::string& path, const lightusd::PrimSpec& spec)
    {
        if (spec.specifier() == lightusd::Specifier::Class) {
            m_result.data.classes.push_back(read_class_prim(path, spec));
            return;
        }
        if (spec.typeName() == c_brush_prim_type_name) {
            read_brush_prim(path, spec);
            return;
        }
        // A marked `NodeGraph` prim is an erhe texture graph
        // (doc/usd-texture-graphs-plan.md R1): its `Shader` children are the
        // graph's nodes rather than a shading network of the scene, so the
        // walk stops here the way it stops at a `Brush` prim. An unmarked
        // `NodeGraph` is a foreign network and is walked like any other prim
        // (R5).
        if ((spec.typeName() == c_node_graph_prim_type_name) && spec_has_node_graph_marker(spec)) {
            read_node_graph_prim(path, spec);
            return;
        }
        record_spec_inherits(path, spec);
        record_spec_variant_sets(path, spec);
        for (const lightusd::PrimSpec& child : spec.children()) {
            collect_layer_composition(path + "/" + child.name(), child);
        }
    }

    // One `class` prim as the record the caller turns into a Style item: its
    // path, its `inherits` targets, its authored opinions and the classes it
    // holds. A `class` descendant is a class of its own; a `def` descendant is
    // a prototype - a prim the class holds abstract
    // (doc/usd-compatibility-plan.md X3) - and is converted as an ordinary
    // prim with `Item_flags::content` clear.
    [[nodiscard]] auto read_class_prim(const std::string& path, const lightusd::PrimSpec& spec) -> Usd_class_prim
    {
        m_class_paths.insert(path);
        Usd_class_prim record{};
        record.stage_path = path;
        record.name       = spec.name();
        read_inherit_paths(spec, record.inherits);
        read_spec_values(spec, record.values);
        for (const lightusd::PrimSpec& child : spec.children()) {
            const std::string child_path = path + "/" + child.name();
            if (child.specifier() == lightusd::Specifier::Class) {
                record.children.push_back(read_class_prim(child_path, child));
            } else {
                m_class_prototype_paths.insert(child_path);
            }
        }
        return record;
    }

    // One `Brush` prim as the record the caller turns into a Brush item
    // (doc/usd-compatibility-plan.md E4a): its path and name, the density and
    // the normal-style token it authors, the material its `material:binding`
    // names, and every other authored opinion in the neutral form. The
    // geometry follows once the meshes are converted (resolve_brush_geometry).
    void read_brush_prim(const std::string& path, const lightusd::PrimSpec& spec)
    {
        m_brush_paths.insert(path);
        Usd_brush_prim record{};
        record.stage_path = path;
        record.name       = spec.name();
        read_spec_values(spec, record.values);
        std::vector<erhe::scene::Instance_override_value> other_values;
        other_values.reserve(record.values.size());
        for (erhe::scene::Instance_override_value& value : record.values) {
            if (value.name == c_brush_density_value_name) {
                record.density = static_cast<float>(std::strtod(value.text.c_str(), nullptr));
                continue;
            }
            if (value.name == c_brush_normal_style_value_name) {
                record.normal_style = value.text;
                continue;
            }
            other_values.push_back(std::move(value));
        }
        record.values        = std::move(other_values);
        record.material_path = read_spec_material_binding(spec);
        m_result.data.brushes.push_back(std::move(record));
    }

    // Whether a `NodeGraph` prim spec carries the marker that says it is an
    // erhe texture graph (doc/usd-texture-graphs-plan.md 2.1).
    [[nodiscard]] static auto spec_has_node_graph_marker(const lightusd::PrimSpec& spec) -> bool
    {
        const std::map<std::string, lightusd::Property>::const_iterator i =
            spec.props().find(std::string{c_node_graph_format_attribute});
        return (i != spec.props().end()) && i->second.is_attribute();
    }

    // The scalar text of an attribute exactly as USD spells it, the quotes of
    // a string or a token kept: what a node parameter travels as
    // (doc/usd-texture-graphs-plan.md 2.2).
    [[nodiscard]] static auto attribute_literal(const lightusd::Attribute& attribute) -> std::string
    {
        return lightusd::value::pprint_value(attribute.get_var().value_raw());
    }

    // The same text with a leading and trailing quote removed: the token of
    // the graph marker and of a node's `info:id`.
    [[nodiscard]] static auto unquote(const std::string& text) -> std::string
    {
        if ((text.size() >= 2) && (text.front() == '"') && (text.back() == '"')) {
            return text.substr(1, text.size() - 2);
        }
        return text;
    }

    // The node name and output pin one connection names, given the path of
    // the graph the connection has to stay inside. A connection leaving the
    // graph is one warning and no link.
    [[nodiscard]] auto read_node_graph_link(
        const std::string&                 graph_path,
        const std::string&                 owner,
        const std::vector<lightusd::Path>& connections,
        Usd_node_graph_pin&                pin
    ) -> bool
    {
        if (connections.empty()) {
            return false;
        }
        const lightusd::tstring_view prim_part = connections[0].prim_part();
        const lightusd::tstring_view prop_part = connections[0].prop_part();
        const std::string            source_prim{prim_part.data(), prim_part.size()};
        const std::string            source_prop{prop_part.data(), prop_part.size()};
        const std::string            prefix = graph_path + "/";
        if (
            (source_prim.size() <= prefix.size())                ||
            (source_prim.compare(0, prefix.size(), prefix) != 0) ||
            (source_prim.find('/', prefix.size()) != std::string::npos)
        ) {
            log_usd->warn(
                "USD node graph '{}': '{}' connects to '{}', which is no node of the graph - the link is dropped",
                graph_path, owner, source_prim
            );
            return false;
        }
        if (source_prop.compare(0, c_node_graph_output_prefix.size(), c_node_graph_output_prefix) != 0) {
            log_usd->warn(
                "USD node graph '{}': '{}' connects to '{}', which is no output pin - the link is dropped",
                graph_path, owner, source_prop
            );
            return false;
        }
        pin.source_node = source_prim.substr(prefix.size());
        pin.source_pin  = source_prop.substr(c_node_graph_output_prefix.size());
        return true;
    }

    // One `Shader` child of a marked `NodeGraph` as one node of the graph
    // (doc/usd-texture-graphs-plan.md 2.3). An `inputs:` attribute carrying a
    // value is a parameter, one carrying a connection or nothing at all is an
    // input pin, and every `outputs:` attribute is an output pin. A shader
    // whose `info:id` is not under the prefix the graph's format names is one
    // warning and no node.
    [[nodiscard]] auto read_node_graph_node(
        const std::string&        graph_path,
        const std::string_view    node_id_prefix,
        const lightusd::PrimSpec& spec,
        Usd_node_graph_node&      node
    ) -> bool
    {
        const std::map<std::string, lightusd::Property>&                props   = spec.props();
        const std::map<std::string, lightusd::Property>::const_iterator info_id =
            props.find(std::string{c_node_graph_info_id_attribute});
        const std::string type_id = ((info_id != props.end()) && info_id->second.is_attribute())
            ? unquote(attribute_literal(info_id->second.get_attribute()))
            : std::string{};
        if (
            node_id_prefix.empty()                    ||
            (type_id.size() <= node_id_prefix.size()) ||
            (type_id.compare(0, node_id_prefix.size(), node_id_prefix) != 0)
        ) {
            log_usd->warn(
                "USD node graph '{}': the Shader '{}' has info:id '{}', which is no erhe '{}' node - it becomes no node",
                graph_path, spec.name(), type_id, node_id_prefix
            );
            return false;
        }
        node.name      = spec.name();
        node.type_name = type_id.substr(node_id_prefix.size());
        for (const std::pair<const std::string, lightusd::Property>& property : props) {
            if (!property.second.is_attribute()) {
                continue;
            }
            const std::string&         name      = property.first;
            const lightusd::Attribute& attribute = property.second.get_attribute();
            if (name == c_node_graph_position_attribute) {
                lightusd::value::float2 position{0.0f, 0.0f};
                if (attribute.get_value<lightusd::value::float2>(&position)) {
                    node.has_position = true;
                    node.position_x   = position[0];
                    node.position_y   = position[1];
                }
                continue;
            }
            if (name.compare(0, c_node_graph_output_prefix.size(), c_node_graph_output_prefix) == 0) {
                node.outputs.push_back(
                    Usd_node_graph_pin{
                        .name       = name.substr(c_node_graph_output_prefix.size()),
                        .value_type = attribute.type_name()
                    }
                );
                continue;
            }
            if (name.compare(0, c_node_graph_input_prefix.size(), c_node_graph_input_prefix) != 0) {
                continue;
            }
            const std::string pin_name = name.substr(c_node_graph_input_prefix.size());
            if (attribute.has_value()) {
                node.parameters.push_back(
                    Usd_node_graph_parameter{
                        .name     = pin_name,
                        .usd_type = attribute.type_name(),
                        .value    = attribute_literal(attribute)
                    }
                );
                continue;
            }
            Usd_node_graph_pin pin{.name = pin_name, .value_type = attribute.type_name()};
            static_cast<void>(read_node_graph_link(graph_path, spec.name() + "." + name, attribute.connections(), pin));
            node.inputs.push_back(std::move(pin));
        }
        return true;
    }

    // A link into a node the graph does not hold - a `Shader` the reader
    // rejected - goes with that node, and an interface output that named it
    // goes with it too (doc/usd-texture-graphs-plan.md 2.1).
    static void drop_dangling_node_graph_links(Usd_node_graph& record)
    {
        std::set<std::string> node_names;
        for (const Usd_node_graph_node& node : record.nodes) {
            node_names.insert(node.name);
        }
        for (Usd_node_graph_node& node : record.nodes) {
            for (Usd_node_graph_pin& pin : node.inputs) {
                if (!pin.source_node.empty() && (node_names.count(pin.source_node) == 0)) {
                    pin.source_node.clear();
                    pin.source_pin.clear();
                }
            }
        }
        std::vector<Usd_node_graph_pin> outputs;
        outputs.reserve(record.outputs.size());
        for (Usd_node_graph_pin& pin : record.outputs) {
            if (node_names.count(pin.source_node) != 0) {
                outputs.push_back(std::move(pin));
            }
        }
        record.outputs = std::move(outputs);
    }

    // One marked `NodeGraph` prim as the record the caller rebuilds the graph
    // asset from (doc/usd-texture-graphs-plan.md 2.3, section 4). The marker
    // is read before the children: the format is what says which `info:id`
    // prefix the graph's nodes are under.
    void read_node_graph_prim(const std::string& path, const lightusd::PrimSpec& spec)
    {
        m_node_graph_paths.insert(path);
        Usd_node_graph record{};
        record.stage_path = path;
        record.name       = spec.name();
        const std::map<std::string, lightusd::Property>::const_iterator format_property =
            spec.props().find(std::string{c_node_graph_format_attribute});
        if ((format_property != spec.props().end()) && format_property->second.is_attribute()) {
            record.format = unquote(attribute_literal(format_property->second.get_attribute()));
        }
        const std::string_view node_id_prefix = node_graph_node_id_prefix(record.format);
        for (const lightusd::PrimSpec& child : spec.children()) {
            if (child.typeName() != c_node_graph_shader_prim_type_name) {
                continue;
            }
            Usd_node_graph_node node{};
            if (read_node_graph_node(path, node_id_prefix, child, node)) {
                record.nodes.push_back(std::move(node));
            }
        }
        // The evaluated geometry of a geometry graph follows once the meshes
        // are converted (resolve_node_graph_geometry).
        if (record.format == c_geometry_graph_format) {
            m_geometry_graph_index_by_path.emplace(path, m_result.data.node_graphs.size());
        }
        for (const std::pair<const std::string, lightusd::Property>& property : spec.props()) {
            if (!property.second.is_attribute()) {
                continue;
            }
            const std::string&         name      = property.first;
            const lightusd::Attribute& attribute = property.second.get_attribute();
            if (name == c_node_graph_format_attribute) {
                continue;
            }
            if (name.compare(0, c_node_graph_output_prefix.size(), c_node_graph_output_prefix) != 0) {
                continue;
            }
            Usd_node_graph_pin pin{
                .name       = name.substr(c_node_graph_output_prefix.size()),
                .value_type = attribute.type_name()
            };
            if (read_node_graph_link(path, name, attribute.connections(), pin)) {
                record.outputs.push_back(std::move(pin));
            }
        }
        drop_dangling_node_graph_links(record);
        m_result.data.node_graphs.push_back(std::move(record));
    }

    // The material slots a marked `NodeGraph` feeds
    // (doc/usd-texture-graphs-plan.md R2). Tydra leaves such a slot unset -
    // the connection targets no `UsdUVTexture` - so the surface shader's own
    // prim spec is where the connection is read from.
    void read_material_graph_bindings(const std::size_t material_index, const std::string& shader_path)
    {
        if (shader_path.empty() || m_node_graph_paths.empty()) {
            return;
        }
        const lightusd::PrimSpec* spec = find_layer_primspec(shader_path);
        if (spec == nullptr) {
            return;
        }
        static constexpr std::pair<std::string_view, Usd_material_texture_slot> inputs[] = {
            {std::string_view{"inputs:diffuseColor"},  Usd_material_texture_slot::base_color},
            {std::string_view{"inputs:emissiveColor"}, Usd_material_texture_slot::emissive},
            {std::string_view{"inputs:normal"},        Usd_material_texture_slot::normal},
            {std::string_view{"inputs:occlusion"},     Usd_material_texture_slot::occlusion},
            {std::string_view{"inputs:metallic"},      Usd_material_texture_slot::metallic_roughness},
            {std::string_view{"inputs:roughness"},     Usd_material_texture_slot::metallic_roughness}
        };
        for (const std::pair<std::string_view, Usd_material_texture_slot>& input : inputs) {
            const std::map<std::string, lightusd::Property>::const_iterator i = spec->props().find(std::string{input.first});
            if ((i == spec->props().end()) || !i->second.is_attribute()) {
                continue;
            }
            const std::vector<lightusd::Path>& connections = i->second.get_attribute().connections();
            if (connections.empty()) {
                continue;
            }
            const lightusd::tstring_view prim_part = connections[0].prim_part();
            const std::string            graph_path{prim_part.data(), prim_part.size()};
            if (m_node_graph_paths.count(graph_path) == 0) {
                continue;
            }
            bool already_bound = false;
            for (const Usd_material_graph_binding& binding : m_result.data.material_graph_bindings) {
                if ((binding.material_index == material_index) && (binding.slot == input.second)) {
                    already_bound = true;
                    break;
                }
            }
            if (!already_bound) {
                m_result.data.material_graph_bindings.push_back(
                    Usd_material_graph_binding{
                        .material_index = material_index,
                        .slot           = input.second,
                        .graph_path     = graph_path
                    }
                );
            }
        }
    }

    // The absolute path a prim spec's `material:binding` names, empty when the
    // spec binds nothing.
    [[nodiscard]] static auto read_spec_material_binding(const lightusd::PrimSpec& spec) -> std::string
    {
        return read_material_binding(spec.props());
    }

    // The absolute path the `material:binding` of one property map names,
    // empty when the map binds nothing. A prim spec and a composed prim both
    // hold their properties in a map of this shape.
    [[nodiscard]] static auto read_material_binding(const std::map<std::string, lightusd::Property>& props) -> std::string
    {
        const std::map<std::string, lightusd::Property>::const_iterator i = props.find("material:binding");
        if ((i == props.end()) || !i->second.is_relationship()) {
            return std::string{};
        }
        const lightusd::Relationship& relationship = i->second.get_relationship();
        if (relationship.is_path()) {
            return relationship.targetPath.full_path_name();
        }
        if (relationship.is_pathvector() && !relationship.targetPathVector.empty()) {
            return relationship.targetPathVector.front().full_path_name();
        }
        return std::string{};
    }

    // The `inherits` arcs of one non-class prim spec, kept by path so the
    // conversion can hand them the item the prim became.
    void record_spec_inherits(const std::string& path, const lightusd::PrimSpec& spec)
    {
        std::vector<std::string> paths;
        read_inherit_paths(spec, paths);
        if (!paths.empty()) {
            m_inherits_by_path.emplace(path, std::move(paths));
        }
    }

    // The `inherits` targets a prim spec authors, in the order USD composes
    // the list-edited ops into. The list-edit rule is the one X1's reference
    // reader repeats, over target paths rather than arcs.
    static void read_inherit_paths(const lightusd::PrimSpec& spec, std::vector<std::string>& out_paths)
    {
        if (!spec.metas().inherits.has_value()) {
            return;
        }
        std::vector<std::string> resolved;
        for (const std::pair<lightusd::ListEditQual, std::vector<lightusd::Path>>& list_op : spec.metas().inherits.value()) {
            std::vector<std::string> items;
            items.reserve(list_op.second.size());
            for (const lightusd::Path& target : list_op.second) {
                if (!target.is_valid()) {
                    continue;
                }
                items.push_back(target.full_path_name());
            }
            switch (list_op.first) {
                case lightusd::ListEditQual::ResetToExplicit: {
                    resolved = std::move(items);
                    break;
                }
                case lightusd::ListEditQual::Prepend: {
                    resolved.insert(resolved.begin(), items.begin(), items.end());
                    break;
                }
                case lightusd::ListEditQual::Append:
                case lightusd::ListEditQual::Add: {
                    resolved.insert(resolved.end(), items.begin(), items.end());
                    break;
                }
                case lightusd::ListEditQual::Delete: {
                    const std::set<std::string> deleted{items.begin(), items.end()};
                    resolved.erase(
                        std::remove_if(
                            resolved.begin(),
                            resolved.end(),
                            [&deleted](const std::string& candidate) { return deleted.count(candidate) != 0; }
                        ),
                        resolved.end()
                    );
                    break;
                }
                default: {
                    break;
                }
            }
        }
        out_paths.insert(out_paths.end(), resolved.begin(), resolved.end());
    }

    // ---------------------------------------------------------------------
    // Variant sets (doc/usd-compatibility-plan.md X4)
    // ---------------------------------------------------------------------

    // Tydra converts the materials the composed stage's meshes bind and no
    // others, so a `Material` prim only a variant binds - or one no mesh of
    // this file binds at all, which is every material of a file whose meshes
    // live in another layer - would reach the tree as nothing at all
    // ("has no converted material"). Every `Material` prim of the stage is
    // wanted: a material is a prim of the erhe tree (U4), and a reference
    // into this file is what gives it its meshes. The extra `Material` prims
    // are converted one by one and appended to the render scene, which is
    // what makes them ordinary materials for everything below.
    //
    // The converter moved its own texture and image lists into the render
    // scene, so an extra conversion fills them again from index zero: the new
    // entries are appended and the ids shifted by what was already there. The
    // six texture slots erhe reads are the ones shifted; the render material's
    // other slots are never read.
    // Every `Material` prim of one stage subtree, by absolute path.
    static void collect_material_prim_paths(
        const lightusd::Prim&  prim,
        const std::string&     absolute_path,
        std::set<std::string>& out_paths
    )
    {
        if (prim.as<lightusd::Material>() != nullptr) {
            out_paths.insert(absolute_path);
        }
        for (const lightusd::Prim& child : prim.children()) {
            collect_material_prim_paths(child, absolute_path + "/" + std::string{child.element_name()}, out_paths);
        }
    }

    void append_unconverted_materials(
        const lightusd::tydra::RenderSceneConverterEnv& env,
        lightusd::tydra::RenderSceneConverter&          converter,
        Tydra_scene&                                    scene
    )
    {
        std::set<std::string> wanted_material_paths;
        for (const std::pair<const std::string, std::vector<Usd_variant_set>>& entry : m_variant_sets_by_path) {
            for (const Usd_variant_set& set : entry.second) {
                for (const Usd_variant& variant : set.variants) {
                    for (const Usd_variant_binding& binding : variant.bindings) {
                        wanted_material_paths.insert(binding.material_path);
                    }
                }
            }
        }
        for (const lightusd::Prim& prim : m_stage->root_prims()) {
            collect_material_prim_paths(prim, "/" + std::string{prim.element_name()}, wanted_material_paths);
        }
        if (wanted_material_paths.empty()) {
            return;
        }
        std::set<std::string> converted_material_paths;
        for (const Tydra_material& material : scene.materials) {
            converted_material_paths.insert(material.abs_path);
        }
        for (const std::string& material_path : wanted_material_paths) {
            if (converted_material_paths.count(material_path) != 0) {
                continue;
            }
            const lightusd::Prim* prim = find_prim(material_path);
            if (prim == nullptr) {
                continue; // apply_variant_bindings reports the dropped binding
            }
            const lightusd::Material* usd_material = prim->as<lightusd::Material>();
            if (usd_material == nullptr) {
                continue;
            }
            const std::size_t texture_offset = scene.textures.size();
            const std::size_t image_offset   = scene.images.size();
            Tydra_material    render_material;
            if (!converter.ConvertMaterial(env, lightusd::Path{material_path, ""}, *usd_material, &render_material)) {
                add_warning(
                    fmt::format(
                        "USD material '{}' is bound by no mesh of this file and could not be converted: {}",
                        material_path,
                        converter.GetError()
                    )
                );
                continue;
            }
            for (lightusd::tydra::UVTexture& texture : converter.textures) {
                if (texture.texture_image_id >= 0) {
                    texture.texture_image_id += static_cast<std::int64_t>(image_offset);
                }
                scene.textures.push_back(texture);
            }
            for (const lightusd::tydra::TextureImage& image : converter.images) {
                scene.images.push_back(image);
            }
            converter.textures.clear();
            converter.images.clear();
            shift_texture_ids(render_material, texture_offset);
            scene.materials.push_back(std::move(render_material));
        }
    }

    // The texture ids of the seven UsdPreviewSurface inputs erhe reads
    // (apply_preview_surface), moved by what the render scene already held.
    // Every input apply_preview_surface reads is listed: an input left out
    // keeps an id into the textures of the material appended before it.
    static void shift_texture_ids(Tydra_material& material, const std::size_t texture_offset)
    {
        if (!material.surfaceShader.has_value() || (texture_offset == 0)) {
            return;
        }
        lightusd::tydra::PreviewSurfaceShader& shader = material.surfaceShader.value();
        const std::array<std::int32_t*, 7> texture_ids{
            &shader.diffuseColor.texture_id,
            &shader.emissiveColor.texture_id,
            &shader.normal.texture_id,
            &shader.occlusion.texture_id,
            &shader.roughness.texture_id,
            &shader.metallic.texture_id,
            &shader.opacity.texture_id
        };
        for (std::int32_t* texture_id : texture_ids) {
            if (*texture_id >= 0) {
                *texture_id += static_cast<std::int32_t>(texture_offset);
            }
        }
    }

    // The `variantSet` blocks one prim spec authors. LightUSD composes
    // nothing, so a variant contributes no property to the composed prim: the
    // layer's own spec is where the blocks are, and applying the selection is
    // the reader's job (apply_variant_bindings). The prims a variant adds are
    // already in the tree - load_stage hoisted them there - and this is where
    // each variant learns which of them are its own.
    void record_spec_variant_sets(const std::string& path, const lightusd::PrimSpec& spec)
    {
        if (spec.variantSets().empty()) {
            return;
        }
        const lightusd::VariantSelectionMap& selection = spec.get_variant_selection_map();
        std::vector<Usd_variant_set>         sets;
        for (const std::pair<const std::string, lightusd::VariantSetSpec>& entry : spec.variantSets()) {
            Usd_variant_set set{};
            set.stage_path = path;
            set.set_name   = entry.first;
            for (const std::pair<const std::string, lightusd::PrimSpec>& variant_entry : entry.second.variantSet) {
                Usd_variant variant{};
                variant.name = variant_entry.first;
                read_variant_prims(path, entry.first, variant);
                read_variant_opinions(path, variant_entry.second, std::string{}, variant, set.unsupported_opinion_count);
                set.variants.push_back(std::move(variant));
            }
            const lightusd::VariantSelectionMap::const_iterator i = selection.find(entry.first);
            if (i != selection.end()) {
                set.selected = i->second;
            } else if (!set.variants.empty()) {
                set.selected = set.variants.front().name;
            }
            sets.push_back(std::move(set));
        }
        m_variant_sets_by_path.emplace(path, std::move(sets));
    }

    // The prims of one variant, as load_stage hoisted them into the tree
    // below the prim carrying the set (doc/usd-compatibility-plan.md X4).
    void read_variant_prims(const std::string& path, const std::string& set_name, Usd_variant& variant)
    {
        if (m_impl == nullptr) {
            return;
        }
        for (const Variant_prim_record& record : m_impl->variant_prims) {
            if ((record.carrier_path == path) && (record.set_name == set_name) && (record.variant_name == variant.name)) {
                variant.prims.push_back(
                    Usd_variant_prim{
                        .relative_path = record.prim_name,
                        .authored_name = record.authored_name
                    }
                );
            }
        }
    }

    // One variant block: the `material:binding` relationships it authors and
    // the property opinions it authors, on the prim carrying the set (an empty
    // relative path) and on the `over` prims below it. An opinion is recorded
    // exactly the way an `over` below a reference carrier is (X2), so both
    // travel through the same apply. A property the value reader cannot
    // express is counted for the set; a `def` child of the variant is a prim
    // of the tree carrying its own attributes, so its opinions are not the
    // variant's, and a path that reaches no prim of the tree is dropped as
    // structure when the base values are captured.
    void read_variant_opinions(
        const std::string&        stage_path,
        const lightusd::PrimSpec& spec,
        const std::string&        relative_path,
        Usd_variant&              variant,
        std::size_t&              unsupported_opinion_count
    )
    {
        for (const std::pair<const std::string, lightusd::Property>& property : spec.props()) {
            if ((property.first == "material:binding") && property.second.is_relationship()) {
                const lightusd::Relationship& relationship = property.second.get_relationship();
                if (relationship.is_path()) {
                    variant.bindings.push_back(
                        Usd_variant_binding{
                            .relative_path = relative_path,
                            .material_path = relationship.targetPath.full_path_name()
                        }
                    );
                    continue;
                }
                if (relationship.is_pathvector() && !relationship.targetPathVector.empty()) {
                    variant.bindings.push_back(
                        Usd_variant_binding{
                            .relative_path = relative_path,
                            .material_path = relationship.targetPathVector.front().full_path_name()
                        }
                    );
                    continue;
                }
            }
            if (!is_carried_spec_property(property.first, property.second)) {
                ++unsupported_opinion_count;
            }
        }
        erhe::scene::Instance_override entry{};
        entry.relative_path = relative_path;
        read_spec_values(spec, entry.values);
        read_override_xform_ops(stage_path, spec, entry);
        if (!entry.values.empty() || entry.transform_overridden) {
            variant.overrides.push_back(std::move(entry));
        }
        for (const lightusd::PrimSpec& child : spec.children()) {
            if (child.specifier() == lightusd::Specifier::Def) {
                // A prim the variant adds. It is in the tree with its own
                // attributes when the hoist reached it, and an opinion the
                // set does not carry when it did not - a `def` below an
                // `over` child, or a variant block inside a `.usdz` archive.
                if (!is_hoisted_variant_prim(variant, child.name())) {
                    ++unsupported_opinion_count;
                }
                continue;
            }
            const std::string child_path = relative_path.empty()
                ? child.name()
                : (relative_path + "/" + child.name());
            read_variant_opinions(stage_path, child, child_path, variant, unsupported_opinion_count);
        }
    }

    // Whether the variant's prim of that authored name is in the tree.
    [[nodiscard]] static auto is_hoisted_variant_prim(const Usd_variant& variant, const std::string& authored_name) -> bool
    {
        for (const Usd_variant_prim& prim : variant.prims) {
            if (prim.authored_name == authored_name) {
                return true;
            }
        }
        return false;
    }

    // Whether one property of a prim spec reaches erhe at all: the material
    // binding, the xformOps of a transform, the `erhe:Owner:name` custom
    // attributes and the two native tokens read_spec_values reads. Everything
    // else is an opinion the reader has no place for.
    [[nodiscard]] static auto is_carried_spec_property(const std::string& name, const lightusd::Property& property) -> bool
    {
        if (property.is_relationship()) {
            return name == "material:binding";
        }
        if (!property.is_attribute()) {
            return false;
        }
        if (name == "xformOpOrder") {
            return true;
        }
        static constexpr std::string_view xform_op_prefix{"xformOp:"};
        static constexpr std::string_view erhe_prefix    {"erhe:"};
        if (name.compare(0, xform_op_prefix.size(), xform_op_prefix) == 0) {
            return true;
        }
        if (name.compare(0, erhe_prefix.size(), erhe_prefix) == 0) {
            return true;
        }
        return (name == "visibility") || (name == "purpose");
    }

    // The variant sets of the prim at `absolute_path`, on the item the prim
    // became.
    void record_variant_sets(const std::string& absolute_path, const std::shared_ptr<erhe::Item_base>& item)
    {
        const std::map<std::string, std::vector<Usd_variant_set>>::const_iterator i = m_variant_sets_by_path.find(absolute_path);
        if (i == m_variant_sets_by_path.end()) {
            return;
        }
        for (const Usd_variant_set& set : i->second) {
            Usd_variant_set recorded = set;
            recorded.prim = item;
            m_result.data.variant_sets.push_back(std::move(recorded));
        }
    }

    // The mesh a `Mesh` prim became, by the prim's absolute path: what a
    // variant binding names.
    void record_mesh_prim(const Tydra_node& usd_node, const std::shared_ptr<erhe::Item_base>& content)
    {
        const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(content);
        if (!mesh || (usd_node.id < 0)) {
            return;
        }
        m_mesh_by_path.emplace(
            usd_node.abs_path,
            Mesh_prim{.mesh = mesh, .template_index = static_cast<std::size_t>(usd_node.id)}
        );
    }

    // The selected variant of every set, applied to the imported result: USD
    // resolves a variant selection in composition, and LightUSD composes
    // nothing, so what a variant authors only reaches the scene through this.
    // The base values are captured first, before any opinion of the selected
    // variant is applied, so a switch to another variant has what the file
    // authored outside the variant blocks to go back to.
    void apply_variant_bindings()
    {
        for (Usd_variant_set& set : m_result.data.variant_sets) {
            capture_variant_base_values(set);
            if (set.unsupported_opinion_count != 0) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': variant set '{}' authors {} opinion(s) that erhe has no place for - they are not carried",
                        set.stage_path,
                        set.set_name,
                        set.unsupported_opinion_count
                    )
                );
            }
            const Usd_variant* variant = nullptr;
            for (const Usd_variant& candidate : set.variants) {
                if (candidate.name == set.selected) {
                    variant = &candidate;
                    break;
                }
            }
            if (variant == nullptr) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': variant set '{}' selects '{}', which the set does not hold - nothing of the set is applied",
                        set.stage_path,
                        set.set_name,
                        set.selected
                    )
                );
                continue;
            }
            apply_variant_overrides(set, *variant);
            apply_variant(set, *variant);
        }
    }

    // The prim one relative path of a variant names: the prim carrying the set
    // for the empty path, and the item at that path below it otherwise.
    [[nodiscard]] static auto find_variant_target(erhe::Hierarchy& carrier, const std::string& relative_path) -> erhe::Hierarchy*
    {
        return relative_path.empty() ? &carrier : erhe::find_by_path(carrier, relative_path);
    }

    // What the prims of one set hold before any opinion of the selected
    // variant reaches them, for every path and property name any variant of
    // the set authors. A property without a local value is a `cleared` entry,
    // so restoring it clears rather than writes. An override whose path
    // reaches no prim of the tree is a prim a variant adds - structure, which
    // this slice does not create - so it is dropped and counted for the set.
    void capture_variant_base_values(Usd_variant_set& set)
    {
        erhe::Hierarchy* carrier = dynamic_cast<erhe::Hierarchy*>(set.prim.get());
        if (carrier == nullptr) {
            for (Usd_variant& variant : set.variants) {
                set.unsupported_opinion_count += variant.overrides.size();
                variant.overrides.clear();
            }
            return;
        }
        std::vector<erhe::scene::Instance_override> base_values;
        for (Usd_variant& variant : set.variants) {
            std::vector<erhe::scene::Instance_override> kept;
            for (erhe::scene::Instance_override& entry : variant.overrides) {
                erhe::Hierarchy* target = find_variant_target(*carrier, entry.relative_path);
                if (target == nullptr) {
                    ++set.unsupported_opinion_count;
                    continue;
                }
                capture_variant_base_entry(*target, entry, base_values);
                kept.push_back(std::move(entry));
            }
            variant.overrides = std::move(kept);
        }
        set.base_values = std::move(base_values);
    }

    // The base entry for one override: every property name it authors that no
    // earlier variant of the set already recorded, plus the item's transform
    // when any variant authors one for it.
    static void capture_variant_base_entry(
        erhe::Hierarchy&                             target,
        const erhe::scene::Instance_override&        entry,
        std::vector<erhe::scene::Instance_override>& base_values
    )
    {
        erhe::scene::Instance_override* base = nullptr;
        for (erhe::scene::Instance_override& candidate : base_values) {
            if (candidate.relative_path == entry.relative_path) {
                base = &candidate;
                break;
            }
        }
        if (base == nullptr) {
            erhe::scene::Instance_override new_base{};
            new_base.relative_path = entry.relative_path;
            base_values.push_back(std::move(new_base));
            base = &base_values.back();
        }
        for (const erhe::scene::Instance_override_value& value : entry.values) {
            bool already_recorded = false;
            for (const erhe::scene::Instance_override_value& recorded : base->values) {
                if (recorded.name == value.name) {
                    already_recorded = true;
                    break;
                }
            }
            if (already_recorded) {
                continue;
            }
            const erhe::property::Dependency_property* property = erhe::scene::find_override_property(target, value.name);
            if (property == nullptr) {
                continue; // apply_property_values warns about the name once
            }
            if (target.has_local_value(*property)) {
                base->values.push_back(
                    erhe::scene::Instance_override_value{
                        .name  = value.name,
                        .text  = erhe::property::to_string(*property, target.get_value(*property)),
                        .state = erhe::scene::Instance_override_value_state::supplied
                    }
                );
            } else {
                base->values.push_back(
                    erhe::scene::Instance_override_value{
                        .name  = value.name,
                        .text  = std::string{},
                        .state = erhe::scene::Instance_override_value_state::cleared
                    }
                );
            }
        }
        if (entry.transform_overridden && !base->transform_overridden) {
            const erhe::scene::Xformable* xformable = dynamic_cast<const erhe::scene::Xformable*>(&target);
            if (xformable != nullptr) {
                base->transform_overridden = true;
                base->transform            = xformable->parent_from_node_transform().get_matrix();
                base->xform_op_stack       = xformable->copy_xform_op_stack();
            }
        }
    }

    // The property opinions of one variant, on the prims they name.
    void apply_variant_overrides(const Usd_variant_set& set, const Usd_variant& variant)
    {
        erhe::Hierarchy* carrier = dynamic_cast<erhe::Hierarchy*>(set.prim.get());
        if (carrier == nullptr) {
            return;
        }
        const std::string owner = fmt::format("variant '{}' of set '{}' on '{}'", variant.name, set.set_name, set.stage_path);
        for (const erhe::scene::Instance_override& entry : variant.overrides) {
            erhe::Hierarchy* target = find_variant_target(*carrier, entry.relative_path);
            if (target == nullptr) {
                continue; // dropped as structure when the base values were captured
            }
            erhe::scene::apply_property_values(*target, entry.values, owner);
            if (!entry.transform_overridden) {
                continue;
            }
            erhe::scene::Xformable* xformable = dynamic_cast<erhe::scene::Xformable*>(target);
            if (xformable == nullptr) {
                add_warning(fmt::format("{}: '{}' carries no transform - the xformOps are dropped", owner, entry.relative_path));
                continue;
            }
            if (entry.xform_op_stack.has_value()) {
                xformable->set_xform_op_stack(entry.xform_op_stack.value());
            } else {
                xformable->set_parent_from_node(entry.transform);
            }
        }
    }

    void apply_variant(const Usd_variant_set& set, const Usd_variant& variant)
    {
        // A binding on a mesh prim covers the mesh's primitives the same
        // variant does not bind by subset, the way a USD binding on a prim is
        // the fallback for the descendants that author none.
        std::set<std::string> bound_paths;
        for (const Usd_variant_binding& binding : variant.bindings) {
            bound_paths.insert(binding_absolute_path(set.stage_path, binding.relative_path));
        }
        for (const Usd_variant_binding& binding : variant.bindings) {
            const std::string                                absolute_path = binding_absolute_path(set.stage_path, binding.relative_path);
            const std::shared_ptr<erhe::primitive::Material> material      = find_material_by_path(binding.material_path);
            if (!material) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': variant '{}' of set '{}' binds '{}' to material '{}', which the file has no prim for - the binding is dropped",
                        set.stage_path,
                        variant.name,
                        set.set_name,
                        absolute_path,
                        binding.material_path
                    )
                );
                continue;
            }
            if (!apply_binding(absolute_path, bound_paths, material)) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': variant '{}' of set '{}' binds '{}', which is no mesh or GeomSubset of the file - the binding is dropped",
                        set.stage_path,
                        variant.name,
                        set.set_name,
                        absolute_path
                    )
                );
            }
        }
    }

    [[nodiscard]] static auto binding_absolute_path(const std::string& stage_path, const std::string& relative_path) -> std::string
    {
        return relative_path.empty() ? stage_path : (stage_path + "/" + relative_path);
    }

    [[nodiscard]] auto find_material_by_path(const std::string& material_path) const -> std::shared_ptr<erhe::primitive::Material>
    {
        const std::map<std::string, std::size_t>::const_iterator i = m_material_by_path.find(material_path);
        if (i == m_material_by_path.end()) {
            return {};
        }
        return m_result.data.materials[i->second];
    }

    // One binding of the selected variant: `absolute_path` is a mesh prim or
    // a GeomSubset of one. True when the binding reached a primitive.
    [[nodiscard]] auto apply_binding(
        const std::string&                                absolute_path,
        const std::set<std::string>&                      bound_paths,
        const std::shared_ptr<erhe::primitive::Material>& material
    ) -> bool
    {
        const std::map<std::string, Mesh_prim>::const_iterator mesh_entry = m_mesh_by_path.find(absolute_path);
        if (mesh_entry != m_mesh_by_path.end()) {
            const std::vector<std::string>& group_names = mesh_group_names(mesh_entry->second.template_index);
            bool                            applied     = false;
            for (std::size_t index = 0, end = group_names.size(); index < end; ++index) {
                const std::string& group_name = group_names[index];
                if (!group_name.empty() && (bound_paths.count(absolute_path + "/" + group_name) != 0)) {
                    continue;
                }
                mesh_entry->second.mesh->set_primitive_material(index, material);
                applied = true;
            }
            return applied;
        }
        const std::size_t separator = absolute_path.rfind('/');
        if (separator == std::string::npos) {
            return false;
        }
        const std::string                                      parent_path = absolute_path.substr(0, separator);
        const std::string                                      subset_name = absolute_path.substr(separator + 1);
        const std::map<std::string, Mesh_prim>::const_iterator parent      = m_mesh_by_path.find(parent_path);
        if (parent == m_mesh_by_path.end()) {
            return false;
        }
        const std::vector<std::string>& group_names = mesh_group_names(parent->second.template_index);
        for (std::size_t index = 0, end = group_names.size(); index < end; ++index) {
            if (group_names[index] == subset_name) {
                parent->second.mesh->set_primitive_material(index, material);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto mesh_group_names(const std::size_t template_index) const -> const std::vector<std::string>&
    {
        static const std::vector<std::string> s_empty;
        return (template_index < m_mesh_group_names.size()) ? m_mesh_group_names[template_index] : s_empty;
    }

    // One line of Usd_load_result::warning, and the same line in the log: a
    // load reports what it had to leave out both to the caller and to the
    // run's log.
    void add_warning(const std::string& text)
    {
        log_usd->warn("{}", text);
        if (!m_result.warning.empty()) {
            m_result.warning += "\n";
        }
        m_result.warning += text;
    }

    // The `inherits` arcs of the prim at `absolute_path`, on the item the prim
    // became (doc/usd-compatibility-plan.md X3). The caller resolves the
    // targets: erhe::usd creates no Style item.
    void record_inherits(const std::string& absolute_path, const std::shared_ptr<erhe::Item_base>& item)
    {
        const std::map<std::string, std::vector<std::string>>::const_iterator i = m_inherits_by_path.find(absolute_path);
        if (i == m_inherits_by_path.end()) {
            return;
        }
        m_result.data.prim_inherits.push_back(
            Usd_prim_inherits{
                .item       = item,
                .stage_path = absolute_path,
                .inherits   = i->second
            }
        );
    }

    // The Mesh prim of a `Mesh` prim of the stage; null for every other prim
    // type and for content the conversion skipped.
    [[nodiscard]] auto take_mesh(const Tydra_node& usd_node) -> std::shared_ptr<erhe::scene::Mesh>
    {
        if ((usd_node.id < 0) || (usd_node.nodeType != lightusd::tydra::NodeType::Mesh)) {
            return {};
        }
        const std::size_t content_index = static_cast<std::size_t>(usd_node.id);
        if (content_index >= m_result.data.meshes.size()) {
            return {};
        }
        // A USD mesh several prims reference becomes one erhe mesh per prim,
        // the way a glTF mesh shared by several nodes does.
        const erhe::scene::Mesh& template_mesh = *m_result.data.meshes[content_index].get();
        std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh_attached[content_index]
            ? std::make_shared<erhe::scene::Mesh>(template_mesh, erhe::for_clone{true})
            : m_result.data.meshes[content_index];
        m_mesh_attached[content_index] = true;
        return mesh;
    }

    // The prim the stage prim's own type is: a Mesh, a Camera or a Light.
    // Null for a plain Xform prim and for content the conversion skipped -
    // the caller makes an `Xform` then.
    [[nodiscard]] auto take_node_content(const Tydra_node& usd_node) -> std::shared_ptr<erhe::Item_base>
    {
        if (usd_node.id < 0) {
            return {};
        }
        const std::size_t content_index = static_cast<std::size_t>(usd_node.id);
        switch (usd_node.nodeType) {
            case lightusd::tydra::NodeType::Mesh: {
                return take_mesh(usd_node);
            }
            case lightusd::tydra::NodeType::Camera: {
                if (content_index < m_result.data.cameras.size()) {
                    return m_result.data.cameras[content_index];
                }
                return {};
            }
            case lightusd::tydra::NodeType::PointLight:
            case lightusd::tydra::NodeType::DirectionalLight:
            case lightusd::tydra::NodeType::RectLight:
            case lightusd::tydra::NodeType::DiskLight:
            case lightusd::tydra::NodeType::CylinderLight: {
                if ((content_index < m_result.data.lights.size()) && m_result.data.lights[content_index]) {
                    return m_result.data.lights[content_index];
                }
                return {};
            }
            default: {
                return {};
            }
        }
    }

    // One prim's authored opinions, and the items they resolve against.
    // `visibility_target` is the node a scene-graph prim became, and is null
    // for a prim that is not in the scene graph (a Material).
    class Authored_opinions final
    {
    public:
        std::string                        absolute_path;
        erhe::Item_base*                   visibility_target{nullptr};
        erhe::property::Dependency_object* primary          {nullptr};
        erhe::property::Dependency_object* secondary        {nullptr};
    };

    // One `Mesh` prim of the stage: the erhe mesh it became and the index of
    // the Tydra mesh it was converted from, which is what names its subsets.
    class Mesh_prim final
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        template_index{0};
    };

    // One skin the conversion made, and the (skeleton, geometry bind
    // transform) pair it answers for (doc/usd-compatibility-plan.md K1).
    class Skin_key final
    {
    public:
        std::size_t                        skeleton_index{0};
        glm::mat4                          geometry_from_bind{1.0f};
        std::shared_ptr<erhe::scene::Skin> skin;
    };

    const Usd_load_arguments&      m_arguments;
    Usd_load_result&               m_result;
    // The time code the whole stage is evaluated at, and whether any prim
    // samples a transform at all (read_time_codes).
    double                         m_import_time_code{0.0};
    bool                           m_has_time_samples{false};
    std::vector<Authored_opinions> m_authored_opinions;
    const lightusd::Stage*       m_stage{nullptr};
    const Tydra_scene*           m_scene{nullptr};
    std::map<std::size_t, bool>  m_mesh_attached;
    // The material each `Material` prim of the stage became, by the prim's
    // absolute path: what place_material puts into the tree.
    std::map<std::string, std::size_t> m_material_by_path;
    // Authored property names per prim path, see authored_property_names.
    std::map<std::string, std::set<std::string>> m_authored_property_names;
    // The absolute path of every `class` prim of the layer, nested ones
    // included, filled by read_layer_composition: what convert_node skips.
    std::set<std::string>                          m_class_paths;
    // The `def` descendants of the class prims: the prototypes they hold (X3).
    std::set<std::string>                          m_class_prototype_paths;
    // The prototypes the file's `PointInstancer` prims name (S1), filled as
    // each instancer is reached and before its children convert: what makes
    // the subtree abstract.
    std::set<std::string>                          m_point_instancer_prototype_paths;
    // Non-zero while a prototype subtree is converted, which is what clears
    // `Item_flags::content` on the prims it makes.
    int                                            m_prototype_depth{0};
    // The absolute path of every `Brush` prim of the layer, filled by
    // read_layer_composition: what convert_node skips
    // (doc/usd-compatibility-plan.md E4a).
    std::set<std::string>                          m_brush_paths;

    // The absolute path of every marked `NodeGraph` prim of the layer, filled
    // by read_layer_composition: what the scene conversion stops at, and what
    // a material connection is recognized as a graph binding by.
    std::set<std::string>                          m_node_graph_paths;
    std::map<std::string, std::size_t>             m_geometry_graph_index_by_path;
    // The converted mesh of every `Mesh` prim of the stage, by the prim's
    // absolute path, as an index into Usd_data::meshes: what a brush prim's
    // geometry child is looked up by. Filled by convert_meshes, which runs
    // before any prim is placed.
    std::map<std::string, std::size_t>             m_mesh_index_by_path;
    // The `inherits` targets of every non-class prim spec of the layer,
    // by absolute path, filled by read_layer_composition.
    std::map<std::string, std::vector<std::string>> m_inherits_by_path;
    // The variant sets of every non-class prim spec of the layer, by
    // absolute path, filled by read_layer_composition
    // (doc/usd-compatibility-plan.md X4).
    std::map<std::string, std::vector<Usd_variant_set>> m_variant_sets_by_path;
    // The subset name of every primitive of every converted mesh, in the
    // order the primitives were added, indexed by the Tydra mesh index. An
    // empty name is the primitive holding the facets no subset claims.
    std::vector<std::vector<std::string>>           m_mesh_group_names;
    // The mesh each `Mesh` prim of the stage became, by the prim's absolute
    // path: what a variant binding resolves against.
    std::map<std::string, Mesh_prim>               m_mesh_by_path;
    // The `Skeleton` prims Tydra converted, by stage path, and the prims the
    // conversion made for each of them: the prim the `Skeleton` became and
    // one prim per joint, in the skeleton's `joints` order
    // (doc/usd-compatibility-plan.md K1). Filled by index_skeletons and
    // convert_skeleton, read by build_skins and build_animation.
    std::map<std::string, std::size_t>             m_skeleton_index_by_path;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_skeleton_nodes;
    std::vector<std::vector<std::shared_ptr<erhe::scene::Node>>> m_skeleton_joint_nodes;
    // The skins build_skins made, by the pair they answer for.
    std::vector<Skin_key>                          m_skins;
    // The stage being converted, with the composed layer load_stage kept and
    // the prims it hoisted out of the variant blocks.
    const Stage::Impl*                           m_impl{nullptr};
    // The archive directory of the `.usdz` the stage was loaded from, read
    // lazily by ensure_usdz_asset: what packed texture bytes are taken from.
    lightusd::USDZAsset                          m_usdz_asset;
    bool                                         m_usdz_read{false};
    bool                                         m_usdz_ok  {false};
};

} // anonymous namespace

auto convert_stage(const Stage& stage, const Usd_load_arguments& arguments) -> Usd_load_result
{
    ERHE_PROFILE_FUNCTION();

    Usd_load_result result{};
    Importer        importer{arguments, result};
    importer.convert(stage.get_impl());
    return result;
}

auto load_usd(const Usd_load_arguments& arguments) -> Usd_load_result
{
    ERHE_PROFILE_FUNCTION();

    Load_stage_result load_stage_result = load_stage(arguments.path);
    if (!load_stage_result.stage) {
        Usd_load_result result{};
        result.error   = load_stage_result.error;
        result.warning = load_stage_result.warning;
        return result;
    }

    Usd_load_result result = convert_stage(*load_stage_result.stage.get(), arguments);
    if (result.warning.empty()) {
        result.warning = load_stage_result.warning;
    }
    return result;
}

} // namespace erhe::usd
