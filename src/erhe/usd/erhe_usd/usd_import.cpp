#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/shapes/capsule.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/xform.hpp"
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
#include "usdLux.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-converter.hh"
#include "tydra/scene-access.hh"
#include "value-pprint.hh"
#include "timesamples.hh"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

// The authored value of one op. An op that carries time samples rather than a
// default uses its first sample: erhe has no animated transform to put the
// rest in yet (doc/usd-compatibility-plan.md section 6, future work).
template <typename T>
[[nodiscard]] auto get_xform_op_value(const lightusd::XformOp& op, T& out_value) -> bool
{
    if (op.has_default()) {
        const nonstd::optional<T> value = op.get_value<T>();
        if (!value) {
            return false;
        }
        out_value = value.value();
        return true;
    }
    const nonstd::optional<lightusd::value::TimeSamples> samples = op.get_timesamples();
    if (!samples) {
        return false;
    }
    const nonstd::optional<double> time = samples.value().get_time(0);
    if (!time) {
        return false;
    }
    const nonstd::optional<T> value = op.get_value<T>(time.value());
    if (!value) {
        return false;
    }
    out_value = value.value();
    return true;
}

template <typename T>
[[nodiscard]] auto get_xform_op_vector(const lightusd::XformOp& op, glm::dvec3& out_value) -> bool
{
    std::array<T, 3> value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = glm::dvec3{
        static_cast<double>(value[0]),
        static_cast<double>(value[1]),
        static_cast<double>(value[2])
    };
    return true;
}

[[nodiscard]] auto get_xform_op_half_vector(const lightusd::XformOp& op, glm::dvec3& out_value) -> bool
{
    lightusd::value::half3 value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = glm::dvec3{
        static_cast<double>(lightusd::value::half_to_float(value[0])),
        static_cast<double>(lightusd::value::half_to_float(value[1])),
        static_cast<double>(lightusd::value::half_to_float(value[2]))
    };
    return true;
}

template <typename T>
[[nodiscard]] auto get_xform_op_scalar(const lightusd::XformOp& op, double& out_value) -> bool
{
    T value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = static_cast<double>(value);
    return true;
}

[[nodiscard]] auto get_xform_op_half_scalar(const lightusd::XformOp& op, double& out_value) -> bool
{
    lightusd::value::half value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = static_cast<double>(lightusd::value::half_to_float(value));
    return true;
}

template <typename T>
[[nodiscard]] auto get_xform_op_quaternion(const lightusd::XformOp& op, glm::dquat& out_value) -> bool
{
    T value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = glm::dquat{
        static_cast<double>(value.real),
        static_cast<double>(value.imag[0]),
        static_cast<double>(value.imag[1]),
        static_cast<double>(value.imag[2])
    };
    return true;
}

[[nodiscard]] auto get_xform_op_half_quaternion(const lightusd::XformOp& op, glm::dquat& out_value) -> bool
{
    lightusd::value::quath value{};
    if (!get_xform_op_value(op, value)) {
        return false;
    }
    out_value = glm::dquat{
        static_cast<double>(lightusd::value::half_to_float(value.real)),
        static_cast<double>(lightusd::value::half_to_float(value.imag[0])),
        static_cast<double>(lightusd::value::half_to_float(value.imag[1])),
        static_cast<double>(lightusd::value::half_to_float(value.imag[2]))
    };
    return true;
}

// One authored `xformOp:<type>[:<suffix>]` as an erhe Xform_op: the type, the
// suffix and the invert flag as authored, and the value in double precision
// with the authored value type kept as the op's precision, so the op is
// written back as the type it was authored with.
[[nodiscard]] auto read_xform_op(const lightusd::XformOp& usd_op, erhe::scene::Xform_op& out_op) -> bool
{
    using Precision = erhe::scene::Xform_op_precision;
    if (!to_erhe_xform_op_type(usd_op.op_type, out_op.type)) {
        return false;
    }
    out_op.suffix   = usd_op.suffix;
    out_op.inverted = usd_op.inverted;

    const std::string type_name = usd_op.get_value_type_name();
    if (type_name == lightusd::value::kMatrix4d) {
        lightusd::value::matrix4d matrix{};
        if (!get_xform_op_value(usd_op, matrix)) {
            return false;
        }
        out_op.precision = Precision::double_;
        out_op.value     = to_glm_double(matrix);
        return true;
    }
    if ((type_name == lightusd::value::kDouble3) || (type_name == lightusd::value::kFloat3) || (type_name == lightusd::value::kHalf3)) {
        glm::dvec3 value{0.0};
        const bool read =
            (type_name == lightusd::value::kDouble3) ? get_xform_op_vector<double>(usd_op, value) :
            (type_name == lightusd::value::kFloat3)  ? get_xform_op_vector<float >(usd_op, value) :
                                                       get_xform_op_half_vector   (usd_op, value);
        if (!read) {
            return false;
        }
        out_op.precision =
            (type_name == lightusd::value::kDouble3) ? Precision::double_ :
            (type_name == lightusd::value::kFloat3)  ? Precision::float_  :
                                                       Precision::half_;
        out_op.value = value;
        return true;
    }
    if ((type_name == lightusd::value::kDouble) || (type_name == lightusd::value::kFloat) || (type_name == lightusd::value::kHalf)) {
        double     value = 0.0;
        const bool read =
            (type_name == lightusd::value::kDouble) ? get_xform_op_scalar<double>(usd_op, value) :
            (type_name == lightusd::value::kFloat)  ? get_xform_op_scalar<float >(usd_op, value) :
                                                      get_xform_op_half_scalar   (usd_op, value);
        if (!read) {
            return false;
        }
        out_op.precision =
            (type_name == lightusd::value::kDouble) ? Precision::double_ :
            (type_name == lightusd::value::kFloat)  ? Precision::float_  :
                                                      Precision::half_;
        out_op.value = value;
        return true;
    }
    if ((type_name == lightusd::value::kQuatd) || (type_name == lightusd::value::kQuatf) || (type_name == lightusd::value::kQuath)) {
        glm::dquat value{1.0, 0.0, 0.0, 0.0};
        const bool read =
            (type_name == lightusd::value::kQuatd) ? get_xform_op_quaternion<lightusd::value::quatd>(usd_op, value) :
            (type_name == lightusd::value::kQuatf) ? get_xform_op_quaternion<lightusd::value::quatf>(usd_op, value) :
                                                     get_xform_op_half_quaternion                   (usd_op, value);
        if (!read) {
            return false;
        }
        out_op.precision =
            (type_name == lightusd::value::kQuatd) ? Precision::double_ :
            (type_name == lightusd::value::kQuatf) ? Precision::float_  :
                                                     Precision::half_;
        out_op.value = value;
        return true;
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

class Importer final
{
public:
    Importer(const Usd_load_arguments& arguments, Usd_load_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    void convert(const lightusd::Stage& stage)
    {
        ERHE_PROFILE_FUNCTION();

        report_skipped_physics(stage);

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
        // Time samples are read at the stage's default time code
        // (doc/usd-compatibility-plan.md I1); an animated attribute
        // contributes its default-time value and nothing else.
        env.timecode = lightusd::value::TimeCode::Default();
        // The search path for texture assets is the file's own directory.
        const std::string directory = m_arguments.path.parent_path().generic_string();
        if (!directory.empty()) {
            env.set_search_paths({directory});
        }

        lightusd::tydra::RenderSceneConverter converter;
        Tydra_scene                           scene;
        const bool converted = converter.ConvertToRenderScene(env, &scene);
        if (!converter.GetWarning().empty()) {
            m_result.warning = converter.GetWarning();
            log_usd->warn("USD '{}': {}", env.usd_filename, converter.GetWarning());
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
        append_unconverted_materials(env, converter, scene);
        convert_images();
        convert_materials();
        convert_meshes();
        resolve_brush_geometry();
        convert_cameras();
        convert_lights();
        convert_nodes();
        apply_variant_bindings();
        elide_default_local_values();
        apply_authored_opinions();

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
        if (xformable == nullptr) {
            return false;
        }
        for (const lightusd::XformOp& usd_op : xformable->xformOps) {
            if (usd_op.op_type == lightusd::XformOp::OpType::ResetXformStack) {
                out_stack.reset_xform_stack = true;
                continue;
            }
            erhe::scene::Xform_op op{};
            if (!read_xform_op(usd_op, op)) {
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
                if ((composed_transform == Composed_transform::unevaluated) || is_near_matrix(stack_matrix, composed)) {
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

    [[nodiscard]] static auto read_gprim_property_names(const lightusd::Prim& prim, std::set<std::string>& names) -> bool
    {
        return
            read_typed_gprim_property_names<lightusd::GeomCube      >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomSphere    >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCone      >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCylinder  >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCylinder_1>(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCapsule   >(prim, names) ||
            read_typed_gprim_property_names<lightusd::GeomCapsule_1 >(prim, names);
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
    // makes a node for is listed: the GPrim-derived geometry and camera
    // types, and the UsdLux types, which carry their own copies of the two
    // attributes rather than deriving from GPrim.
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
            read_visibility_and_purpose<lightusd::GeomCapsule_1 >(prim, visibility, purpose);
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

    // The bytes of one archive entry, empty when the stage is no `.usdz` or
    // the archive holds no such entry. USD writes a packaged asset path
    // relative to the archive root, which is the key of the archive's own
    // directory; a leading `./` is not part of that key.
    [[nodiscard]] auto usdz_entry_bytes(const std::string& asset_identifier) -> std::vector<std::uint8_t>
    {
        if (asset_identifier.empty() || !ensure_usdz_asset()) {
            return {};
        }
        std::string key = asset_identifier;
        if (key.compare(0, 2, "./") == 0) {
            key = key.substr(2);
        }
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
            usd_image.srgb = is_srgb_color_space(image.usdColorSpace);
            m_result.data.images.push_back(usd_image);
        }
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
            warn_about_texel_transform(material_name, "diffuseColor", *diffuse_texture, true);
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
            warn_about_texel_transform(material_name, "emissiveColor", *emissive_texture, true);
        } else if (is_authored(shader_path, "inputs:emissiveColor")) {
            material.set_value(
                Material::emissive_property,
                glm::vec3{shader.emissiveColor.value[0], shader.emissiveColor.value[1], shader.emissiveColor.value[2]}
            );
        }
        if (metallic_texture != nullptr) {
            set_or_clear_value(material, Material::metallic_property, connected_scale(*metallic_texture));
            warn_about_texel_transform(material_name, "metallic", *metallic_texture, true);
        } else if (is_authored(shader_path, "inputs:metallic")) {
            material.set_value(Material::metallic_property, shader.metallic.value);
        }
        if (roughness_texture != nullptr) {
            const float factor = connected_scale(*roughness_texture);
            set_or_clear_value(material, Material::roughness_property, glm::vec2{factor, factor});
            warn_about_texel_transform(material_name, "roughness", *roughness_texture, true);
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

        bind_texture(material_index, Usd_material_texture_slot::base_color, shader.diffuseColor.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::emissive,   shader.emissiveColor.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::normal,     shader.normal.texture_id);
        bind_texture(material_index, Usd_material_texture_slot::occlusion,  shader.occlusion.texture_id);
        // erhe has one metallic-roughness slot; UsdPreviewSurface reads the
        // two channels through separate texture inputs that a glTF-derived
        // file points at one image. The roughness input names it when both
        // are textured.
        const std::int32_t metallic_roughness_texture_id = (shader.roughness.texture_id >= 0)
            ? shader.roughness.texture_id
            : shader.metallic.texture_id;
        bind_texture(material_index, Usd_material_texture_slot::metallic_roughness, metallic_roughness_texture_id);

        // How each bound texture is sampled: the wrap modes and the
        // UsdTransform2d of its UsdUVTexture, on the erhe slot the texture
        // was bound to.
        erhe::primitive::Material_texture_samplers& slots = material.data.texture_samplers;
        if (diffuse_texture != nullptr) {
            apply_texture_sampling(material, slots.base_color, *diffuse_texture);
        }
        if (emissive_texture != nullptr) {
            apply_texture_sampling(material, slots.emissive, *emissive_texture);
        }
        const lightusd::tydra::UVTexture* occlusion_texture = uv_texture_of(shader.occlusion.texture_id);
        if (occlusion_texture != nullptr) {
            apply_texture_sampling(material, slots.occlusion, *occlusion_texture);
            warn_about_texel_transform(material_name, "occlusion", *occlusion_texture, false);
        }
        const lightusd::tydra::UVTexture* metallic_roughness_texture = uv_texture_of(metallic_roughness_texture_id);
        if (metallic_roughness_texture != nullptr) {
            apply_texture_sampling(material, slots.metallic_roughness, *metallic_roughness_texture);
        }
        // Which channel of the bound texture each scalar input reads. USD
        // names it in the connection; erhe's defaults are glTF's packing, so
        // a file that follows glTF writes no local value here.
        if (metallic_texture != nullptr) {
            set_or_clear_value(
                material, Material::metallic_channel_property,
                connected_channel(*metallic_texture, erhe::primitive::Texture_channel::b, material_name, "metallic")
            );
        }
        if (roughness_texture != nullptr) {
            set_or_clear_value(
                material, Material::roughness_channel_property,
                connected_channel(*roughness_texture, erhe::primitive::Texture_channel::g, material_name, "roughness")
            );
        }
        if (occlusion_texture != nullptr) {
            set_or_clear_value(
                material, Material::occlusion_channel_property,
                connected_channel(*occlusion_texture, erhe::primitive::Texture_channel::r, material_name, "occlusion")
            );
        }
        // erhe's fragment alpha comes from the base color texture, so an
        // opacity input is carried only when it reads that same image; an
        // opacity map of its own has no erhe slot to live in.
        const lightusd::tydra::UVTexture* opacity_texture = uv_texture_of(shader.opacity.texture_id);
        if (opacity_texture != nullptr) {
            if (image_of(shader.opacity.texture_id) == image_of(shader.diffuseColor.texture_id)) {
                set_or_clear_value(
                    material, Material::opacity_channel_property,
                    connected_channel(*opacity_texture, erhe::primitive::Texture_channel::a, material_name, "opacity")
                );
            } else {
                log_usd->warn(
                    "USD material '{}': inputs:opacity reads an image of its own - erhe takes the alpha of the base color texture",
                    material_name
                );
            }
        }

        const lightusd::tydra::UVTexture* normal_texture = uv_texture_of(shader.normal.texture_id);
        if (normal_texture != nullptr) {
            apply_texture_sampling(material, slots.normal, *normal_texture);
            // The normal slot is the one erhe carries the texel transform
            // for: the shader decodes `texel * scale + bias`.
            set_or_clear_value(
                material,
                Material::normal_texture_decode_scale_property,
                glm::vec4{normal_texture->scale[0], normal_texture->scale[1], normal_texture->scale[2], normal_texture->scale[3]}
            );
            set_or_clear_value(
                material,
                Material::normal_texture_decode_bias_property,
                glm::vec4{normal_texture->bias[0], normal_texture->bias[1], normal_texture->bias[2], normal_texture->bias[3]}
            );
        }
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

            if (usd_material.surfaceShader.has_value()) {
                apply_preview_surface(
                    usd_material.surfaceShader.value(),
                    find_surface_shader_path(usd_material.abs_path),
                    material_index,
                    *material.get()
                );
            } else {
                log_usd->warn(
                    "USD material '{}' has no UsdPreviewSurface shader - erhe material defaults are used",
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

    [[nodiscard]] auto make_facet_groups(const Tydra_mesh& usd_mesh) const -> std::vector<Facet_group>
    {
        const std::size_t facet_count = usd_mesh.faceVertexCounts().size();
        std::vector<Facet_group> groups;
        std::vector<bool>        claimed(facet_count, false);

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
        if (!usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3)) {
            const glm::vec4 color = read_attribute(
                usd_mesh.vertex_colors,
                attribute_element_index(usd_mesh.vertex_colors, usd_vertex, usd_facet, usd_corner)
            );
            float alpha = 1.0f;
            if (!usd_mesh.vertex_opacities.empty() && (attribute_component_count(usd_mesh.vertex_opacities.format) >= 1)) {
                const glm::vec4 opacity = read_attribute(
                    usd_mesh.vertex_opacities,
                    attribute_element_index(usd_mesh.vertex_opacities, usd_vertex, usd_facet, usd_corner)
                );
                alpha = opacity.x;
            }
            attributes.corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, alpha});
        }
    }

    // Geometry-normative build: the authored facet counts and indices go
    // straight into geogram and the primvars become corner attributes per
    // the element table of doc/usd_compatibility.md.
    [[nodiscard]] auto build_geometry(
        const Tydra_mesh&                 usd_mesh,
        const Facet_group&                group,
        const std::vector<std::uint32_t>& facet_corner_offsets,
        const std::string&                name
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
        const std::vector<std::uint32_t>& facet_corner_offsets
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
        soup->vertex_format.streams.front().emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color, 0);
        soup->vertex_format.streams.front().finalize_stride();

        std::size_t corner_total = 0;
        for (const std::uint32_t facet : group.facets) {
            corner_total += counts[facet];
        }
        const std::size_t stride = soup->vertex_format.streams.front().stride;
        soup->vertex_data.resize(corner_total * stride);

        const bool has_color = !usd_mesh.vertex_colors.empty() && (attribute_component_count(usd_mesh.vertex_colors.format) >= 3);

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
                glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
                if (has_color) {
                    color = read_attribute(
                        usd_mesh.vertex_colors,
                        attribute_element_index(usd_mesh.vertex_colors, usd_vertex, facet, usd_corner)
                    );
                    color.w = 1.0f;
                }
                const float color_values[4] = {color.x, color.y, color.z, color.w};
                std::memcpy(destination + offset, color_values, sizeof(color_values));

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
                        build_geometry(usd_mesh, group, facet_corner_offsets, name)
                    );
                } else {
                    primitive = std::make_shared<erhe::primitive::Primitive>(
                        build_triangle_soup(usd_mesh, group, facet_corner_offsets)
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
        if (type_name == "Cube") {
            const lightusd::GeomCube* cube = prim.as<lightusd::GeomCube>();
            if (cube == nullptr) {
                return {};
            }
            const float size = static_cast<float>(read_schema_double(cube->size, 2.0));
            erhe::geometry::shapes::make_box(geo_mesh, size, size, size);
        } else if (type_name == "Sphere") {
            const lightusd::GeomSphere* sphere = prim.as<lightusd::GeomSphere>();
            if (sphere == nullptr) {
                return {};
            }
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
    [[nodiscard]] auto make_stage_transform() const -> glm::mat4
    {
        glm::mat4 transform{1.0f};
        if (m_result.data.up_axis == "Z") {
            transform = glm::rotate(transform, -glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        } else if (m_result.data.up_axis == "X") {
            log_usd->warn("USD stage up axis 'X' has no erhe counterpart - imported as Y-up");
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
        const lightusd::Prim* prim      = find_prim(usd_node.abs_path);
        const std::string     type_name = (prim != nullptr) ? get_usd_type_name(*prim) : std::string{"Xform"};
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
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(content);
        if (!node) {
            node = std::make_shared<erhe::scene::Xform>(node_name);
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

        const glm::mat4 child_transform{1.0f};
        for (const Tydra_node& usd_child : usd_node.children) {
            convert_node(usd_child, node, child_transform);
        }
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
    // an opinion on a composed prim came from, so the root layer's own prim
    // specs are what is asked; a `def` below a referencing prim adds structure
    // to a reference, which is out of scope (plan section 5), so it is named
    // in a warning and dropped.
    [[nodiscard]] auto read_instance_overrides(const std::string& absolute_path) -> std::vector<erhe::scene::Instance_override>
    {
        std::vector<erhe::scene::Instance_override> overrides;
        const lightusd::PrimSpec* spec = find_root_layer_primspec(absolute_path);
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
            if (child.specifier() != lightusd::Specifier::Over) {
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
                "USD prim '{}': the referencing layer defines prims over the reference ({}) - a reference protects its structure, so they are dropped",
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
            if (!read_xform_op(usd_op, op)) {
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

    // The root layer, read once. A composed prim does not say which layer an
    // opinion came from and Tydra's render-scene conversion never walks a
    // `class` prim, so the layer's own prim specs are what the reader asks for
    // the `over` prims of X2 and the `class` prims and `inherits` arcs of X3.
    [[nodiscard]] auto ensure_root_layer() -> bool
    {
        if (!m_root_layer_read) {
            m_root_layer_read = true;
            std::string warning;
            std::string error;
            m_root_layer_ok = lightusd::LoadLayerFromFile(
                m_arguments.path.generic_string(),
                &m_root_layer,
                &warning,
                &error
            );
            if (!m_root_layer_ok) {
                log_usd->info(
                    "USD '{}': the root layer could not be re-read for composition reporting: {}",
                    m_arguments.path.generic_string(),
                    error
                );
            }
        }
        return m_root_layer_ok;
    }

    // The root layer's prim spec at the given path, or null.
    [[nodiscard]] auto find_root_layer_primspec(const std::string& absolute_path) -> const lightusd::PrimSpec*
    {
        if (!ensure_root_layer()) {
            return nullptr;
        }
        const lightusd::PrimSpec* spec = nullptr;
        std::string               error;
        if (!m_root_layer.find_primspec_at(lightusd::Path{absolute_path, ""}, &spec, &error)) {
            return nullptr;
        }
        return spec;
    }

    // The `class` prims and the `inherits` arcs the root layer authors
    // (doc/usd-compatibility-plan.md X3), read before the prims are converted
    // so a prim's arcs are one map lookup once its item exists. The layer
    // holds its top-level prim specs in a hash map and the ascii reader fills
    // no ordering metadatum for a layer, so the top level is walked in name
    // order; the children of a prim spec keep the order the layer spells.
    void read_layer_composition()
    {
        ERHE_PROFILE_FUNCTION();
        if (!ensure_root_layer()) {
            return;
        }
        std::vector<std::string> root_names;
        root_names.reserve(m_root_layer.primspecs().size());
        for (const std::pair<const std::string, lightusd::PrimSpec>& entry : m_root_layer.primspecs()) {
            root_names.push_back(entry.first);
        }
        std::sort(root_names.begin(), root_names.end());
        for (const std::string& root_name : root_names) {
            const lightusd::PrimSpec& spec = m_root_layer.primspecs().at(root_name);
            collect_layer_composition("/" + root_name, spec);
        }
    }

    // One prim spec of the root layer: a `class` prim is recorded whole (its
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

    // The texture ids of the six UsdPreviewSurface inputs erhe reads
    // (apply_preview_surface), moved by what the render scene already held.
    static void shift_texture_ids(Tydra_material& material, const std::size_t texture_offset)
    {
        if (!material.surfaceShader.has_value() || (texture_offset == 0)) {
            return;
        }
        lightusd::tydra::PreviewSurfaceShader& shader = material.surfaceShader.value();
        const std::array<std::int32_t*, 6> texture_ids{
            &shader.diffuseColor.texture_id,
            &shader.emissiveColor.texture_id,
            &shader.normal.texture_id,
            &shader.occlusion.texture_id,
            &shader.roughness.texture_id,
            &shader.metallic.texture_id
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
    // the reader's job (apply_variant_bindings). Only material bindings are
    // carried in this slice; every other opinion of the set is counted and
    // reported once.
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
                read_variant_opinions(variant_entry.second, std::string{}, variant, set.unsupported_opinion_count);
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

    // One variant block: the `material:binding` relationships it authors, on
    // the prim carrying the set (an empty relative path) and on the prims it
    // holds. Anything else it authors is an opinion this slice does not carry.
    static void read_variant_opinions(
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
            ++unsupported_opinion_count;
        }
        for (const lightusd::PrimSpec& child : spec.children()) {
            const std::string child_path = relative_path.empty()
                ? child.name()
                : (relative_path + "/" + child.name());
            read_variant_opinions(child, child_path, variant, unsupported_opinion_count);
        }
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
            if (recorded.unsupported_opinion_count != 0) {
                add_warning(
                    fmt::format(
                        "USD prim '{}': variant set '{}' authors {} opinion(s) that are not material bindings - only material bindings are carried",
                        absolute_path,
                        recorded.set_name,
                        recorded.unsupported_opinion_count
                    )
                );
            }
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
    // nothing, so a material a variant binds only reaches a mesh through this.
    void apply_variant_bindings()
    {
        for (const Usd_variant_set& set : m_result.data.variant_sets) {
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
                        "USD prim '{}': variant set '{}' selects '{}', which the set does not hold - no binding of the set is applied",
                        set.stage_path,
                        set.set_name,
                        set.selected
                    )
                );
                continue;
            }
            apply_variant(set, *variant);
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

    const Usd_load_arguments&      m_arguments;
    Usd_load_result&               m_result;
    std::vector<Authored_opinions> m_authored_opinions;
    const lightusd::Stage*       m_stage{nullptr};
    const Tydra_scene*           m_scene{nullptr};
    std::map<std::size_t, bool>  m_mesh_attached;
    // The material each `Material` prim of the stage became, by the prim's
    // absolute path: what place_material puts into the tree.
    std::map<std::string, std::size_t> m_material_by_path;
    // Authored property names per prim path, see authored_property_names.
    std::map<std::string, std::set<std::string>> m_authored_property_names;
    // The absolute path of every `class` prim of the root layer, nested ones
    // included, filled by read_layer_composition: what convert_node skips.
    std::set<std::string>                          m_class_paths;
    // The `def` descendants of the class prims: the prototypes they hold (X3).
    std::set<std::string>                          m_class_prototype_paths;
    // Non-zero while a prototype subtree is converted, which is what clears
    // `Item_flags::content` on the prims it makes.
    int                                            m_prototype_depth{0};
    // The absolute path of every `Brush` prim of the root layer, filled by
    // read_layer_composition: what convert_node skips
    // (doc/usd-compatibility-plan.md E4a).
    std::set<std::string>                          m_brush_paths;
    // The converted mesh of every `Mesh` prim of the stage, by the prim's
    // absolute path, as an index into Usd_data::meshes: what a brush prim's
    // geometry child is looked up by. Filled by convert_meshes, which runs
    // before any prim is placed.
    std::map<std::string, std::size_t>             m_mesh_index_by_path;
    // The `inherits` targets of every non-class prim spec of the root layer,
    // by absolute path, filled by read_layer_composition.
    std::map<std::string, std::vector<std::string>> m_inherits_by_path;
    // The variant sets of every non-class prim spec of the root layer, by
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
    // The root layer, read lazily by ensure_root_layer.
    lightusd::Layer                              m_root_layer;
    bool                                         m_root_layer_read{false};
    bool                                         m_root_layer_ok  {false};
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
    importer.convert(stage.get_impl().stage);
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
