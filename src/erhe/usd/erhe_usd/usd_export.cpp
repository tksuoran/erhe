#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/owner_type.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform_op.hpp"

// LightUSD headers. Together with usd.cpp and usd_import.cpp this is the
// only place in erhe that includes them; everything the rest of erhe sees is
// declared in usd.hpp.
#include "lightusd.hh"
#include "core/composition-types.hh"
#include "core/prim.hh"
#include "core/model-scope.hh"
#include "core/prim-metas.hh"
#include "stage.hh"
#include "usda-writer.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "pprint-enum.hh"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace erhe::usd {

namespace {

// The name of the wrapper prim created when the scene has more than one
// top-level prim: a stage names one `defaultPrim` and an erhe scene has one
// root (doc/usd_compatibility.md, stage-level constants).
constexpr const char* c_world_prim_name = "World";

// The class token erhe::Typed fixes for an editor style item
// (doc/style-library.md D2). erhe::usd depends on no editor type, so a style
// is recognized by its token; a style prim is written as a USD `class` prim
// (doc/usd-compatibility-plan.md X3).
constexpr std::string_view c_style_class_type_name{"Style"};

// The focal length every exported perspective camera gets. USD is
// physical-camera-first and erhe stores the two field-of-view angles, so one
// of the three is free; fixing the focal length puts the angles in the
// apertures, which `2 * atan(0.5 * aperture / focalLength)` reads back
// exactly (doc/usd_compatibility.md, cameras).
constexpr float c_export_focal_length = 50.0f;

// The USD `purpose` vocabulary, which erhe's Purpose enumeration mirrors
// token for token; the inverse of the importer's to_erhe_purpose.
[[nodiscard]] auto to_usd_purpose(const erhe::Purpose purpose) -> lightusd::Purpose
{
    switch (purpose) {
        case erhe::Purpose::default_: return lightusd::Purpose::Default;
        case erhe::Purpose::render:   return lightusd::Purpose::Render;
        case erhe::Purpose::proxy:    return lightusd::Purpose::Proxy;
        case erhe::Purpose::guide:    return lightusd::Purpose::Guide;
        default:                      return lightusd::Purpose::Default;
    }
}

// A glm mat4 is column-major with the column-vector convention; a USD
// matrix4d is row-major with the row-vector convention. The two store the
// same coefficients in the same order, so an element-for-element copy is the
// correct conversion - the importer's to_glm read the same way.
[[nodiscard]] auto to_usd(const glm::mat4& matrix) -> lightusd::value::matrix4d
{
    lightusd::value::matrix4d result{};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            result.m[j][i] = static_cast<double>(matrix[j][i]);
        }
    }
    return result;
}

[[nodiscard]] auto is_near(const glm::mat4& lhs, const glm::mat4& rhs) -> bool
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

// The USD counterpart of an erhe xformOp type. `Xform_op_type` is USD's own
// vocabulary, so this is a name-for-name mapping; `ResetXformStack` is a flag
// of the stack rather than an op and has no `Xform_op_type`.
[[nodiscard]] auto to_usd_xform_op_type(const erhe::scene::Xform_op_type type) -> lightusd::XformOp::OpType
{
    using Usd_type  = lightusd::XformOp::OpType;
    using Erhe_type = erhe::scene::Xform_op_type;
    switch (type) {
        case Erhe_type::translate:  return Usd_type::Translate;
        case Erhe_type::scale:      return Usd_type::Scale;
        case Erhe_type::rotate_x:   return Usd_type::RotateX;
        case Erhe_type::rotate_y:   return Usd_type::RotateY;
        case Erhe_type::rotate_z:   return Usd_type::RotateZ;
        case Erhe_type::rotate_xyz: return Usd_type::RotateXYZ;
        case Erhe_type::rotate_xzy: return Usd_type::RotateXZY;
        case Erhe_type::rotate_yxz: return Usd_type::RotateYXZ;
        case Erhe_type::rotate_yzx: return Usd_type::RotateYZX;
        case Erhe_type::rotate_zxy: return Usd_type::RotateZXY;
        case Erhe_type::rotate_zyx: return Usd_type::RotateZYX;
        case Erhe_type::orient:     return Usd_type::Orient;
        default:                    return Usd_type::Transform;
    }
}

[[nodiscard]] auto to_usd_half(const double value) -> lightusd::value::half
{
    return lightusd::value::float_to_half_full(static_cast<float>(value));
}

[[nodiscard]] auto is_three_component_op(const erhe::scene::Xform_op_type type) -> bool
{
    using Erhe_type = erhe::scene::Xform_op_type;
    switch (type) {
        case Erhe_type::translate:
        case Erhe_type::scale:
        case Erhe_type::rotate_xyz:
        case Erhe_type::rotate_xzy:
        case Erhe_type::rotate_yxz:
        case Erhe_type::rotate_yzx:
        case Erhe_type::rotate_zxy:
        case Erhe_type::rotate_zyx: return true;
        default:                    return false;
    }
}

// The op's value in the value type its authored precision names, which is
// what the writer prints the type name of: a `float3` op comes back a
// `float3` op. erhe keeps every value in double precision, so a `half` op
// round-trips through the nearest half - the same value it was read from.
void set_xform_op_value(lightusd::XformOp& usd_op, const erhe::scene::Xform_op& op)
{
    using Precision = erhe::scene::Xform_op_precision;
    if (op.type == erhe::scene::Xform_op_type::transform) {
        // matrix4d is the only matrix type erhe keeps a value in, and the op
        // holds it in double precision, so it is written without narrowing.
        const glm::dmat4          value = std::get<glm::dmat4>(op.value);
        lightusd::value::matrix4d matrix{};
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                matrix.m[j][i] = value[j][i];
            }
        }
        usd_op.set_value(matrix);
        return;
    }
    if (op.type == erhe::scene::Xform_op_type::orient) {
        const glm::dquat value = std::get<glm::dquat>(op.value);
        switch (op.precision) {
            case Precision::double_: {
                usd_op.set_value(lightusd::value::quatd{{value.x, value.y, value.z}, value.w});
                return;
            }
            case Precision::float_: {
                usd_op.set_value(
                    lightusd::value::quatf{
                        {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)},
                        static_cast<float>(value.w)
                    }
                );
                return;
            }
            default: {
                usd_op.set_value(
                    lightusd::value::quath{
                        {to_usd_half(value.x), to_usd_half(value.y), to_usd_half(value.z)},
                        to_usd_half(value.w)
                    }
                );
                return;
            }
        }
    }
    if (is_three_component_op(op.type)) {
        const glm::dvec3 value = std::get<glm::dvec3>(op.value);
        switch (op.precision) {
            case Precision::double_: {
                usd_op.set_value(lightusd::value::double3{value.x, value.y, value.z});
                return;
            }
            case Precision::float_: {
                usd_op.set_value(
                    lightusd::value::float3{
                        static_cast<float>(value.x),
                        static_cast<float>(value.y),
                        static_cast<float>(value.z)
                    }
                );
                return;
            }
            default: {
                usd_op.set_value(lightusd::value::half3{to_usd_half(value.x), to_usd_half(value.y), to_usd_half(value.z)});
                return;
            }
        }
    }
    // A single-axis rotate: one angle in degrees.
    const double value = std::get<double>(op.value);
    switch (op.precision) {
        case Precision::double_: usd_op.set_value(value);                       return;
        case Precision::float_:  usd_op.set_value(static_cast<float>(value));   return;
        default:                 usd_op.set_value(to_usd_half(value));          return;
    }
}

[[nodiscard]] auto is_identity(const glm::mat4& matrix) -> bool
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

// Sibling-unique prim names: the names a scope has already handed out plus
// the suffix rule of erhe::Hierarchy::make_sibling_unique_name (M2) - the
// base is the wanted name without a trailing `_<digits>`, and the first free
// `<base>_<n>` from 1 wins. Sanitizing can map two distinct item names onto
// one spelling, so the writer needs the rule even though erhe names are
// already sibling-unique.
class Name_scope final
{
public:
    [[nodiscard]] auto make_unique(const std::string_view wanted_name) -> std::string
    {
        const std::string sanitized = sanitize_usd_identifier(wanted_name);
        if (m_taken.find(sanitized) == m_taken.end()) {
            m_taken.insert(sanitized);
            return sanitized;
        }
        std::string_view base         = sanitized;
        std::size_t      digits_begin = base.size();
        while ((digits_begin > 0) && (base[digits_begin - 1] >= '0') && (base[digits_begin - 1] <= '9')) {
            --digits_begin;
        }
        if ((digits_begin < base.size()) && (digits_begin >= 2) && (base[digits_begin - 1] == '_')) {
            base = base.substr(0, digits_begin - 1);
        }
        for (std::size_t number = 1; ; ++number) {
            std::string candidate = std::string{base} + "_" + std::to_string(number);
            if (m_taken.find(candidate) == m_taken.end()) {
                m_taken.insert(candidate);
                return candidate;
            }
        }
    }

private:
    std::set<std::string> m_taken;
};

// The asset path an arc gets in the file being written: the target file
// relative to that file's directory, and an empty string when the two are the
// same file - USD spells an internal reference as a prim path alone. Both
// paths are compared after weakly_canonical, so a differently spelled path to
// the same file is still recognized as internal. A target on another volume,
// which no relative path can name, is written absolute.
[[nodiscard]] auto to_reference_asset_path(
    const std::filesystem::path& source_path,
    const std::filesystem::path& written_path
) -> std::string
{
    if (source_path.empty()) {
        return std::string{};
    }
    std::error_code             error_code{};
    const std::filesystem::path canonical_source  = std::filesystem::weakly_canonical(source_path,  error_code);
    const std::filesystem::path canonical_written = std::filesystem::weakly_canonical(written_path, error_code);
    const std::filesystem::path source            = error_code ? source_path  : canonical_source;
    const std::filesystem::path written           = error_code ? written_path : canonical_written;
    if (source == written) {
        return std::string{}; // internal reference: a prim of this same layer
    }
    std::error_code             relative_error{};
    const std::filesystem::path relative = std::filesystem::relative(source, written.parent_path(), relative_error);
    if (relative_error || relative.empty()) {
        return source.generic_string();
    }
    return relative.generic_string();
}

// One prim of the stage, as the first pass decides it: which erhe prim it
// is, the transform that reached it, its sanitized sibling-unique name and
// the stage path that name gives it. The writer needs the paths before it
// writes anything, because a mesh binds a material by the path the material
// prim ends up at (doc/usd-compatibility-plan.md U4).
class Plan_prim final
{
public:
    const erhe::Typed*               item         {nullptr};
    // The same prim as `item` when its class carries a transform, and as
    // `material` when it is a material; null otherwise.
    const erhe::scene::Node*         node         {nullptr};
    const erhe::primitive::Material* material     {nullptr};
    glm::mat4                        pre_transform{1.0f};
    std::string                      name;
    std::string                      path;
    // The composition arcs this prim carries, null when it carries none. A
    // prim that carries arcs is written as the referencing prim it is and
    // `children` stays empty: the arcs' targets supply the prims below it
    // (doc/usd-compatibility-plan.md X1).
    const std::vector<Usd_save_reference>* references{nullptr};
    // The overrides the instances below a carrier hold
    // (doc/usd-compatibility-plan.md X2). `override_root` is the item at the
    // relative path of the arc's target clone, whose values are authored on
    // the carrier prim itself - the clone and the carrier are one prim in USD
    // - and `overrides` holds every deeper item, each an `over` prim below
    // the carrier.
    const erhe::Item_base*                 override_root{nullptr};
    std::vector<erhe::scene::Instance_override_item> overrides;
    std::vector<Plan_prim>           children;
};

// One texture shader of one material's shading network: the material and
// the slot the shader reads. A material is identified by the object rather
// than by its place in the caller's list, because the scene tree - not that
// list - decides which materials are written.
class Texture_shader_key final
{
public:
    const erhe::primitive::Material* material{nullptr};
    Usd_material_texture_slot        slot    {Usd_material_texture_slot::base_color};

    [[nodiscard]] auto operator<(const Texture_shader_key& other) const -> bool
    {
        if (material != other.material) {
            return material < other.material;
        }
        return slot < other.slot;
    }
};

// The erhe properties this writer carries in a USD attribute of the schema -
// the exact inverse of what the importer reads back
// (doc/usd-compatibility-plan.md I2, and the per-domain tables of
// doc/usd_compatibility.md). Every other local value travels as an `erhe:`
// custom attribute, so a property is listed here exactly once: writing both
// forms would author one value twice.
// Whether the prim being written carries the schema attributes a value could
// travel in. A `Material`, `Camera` or UsdLux prim does - its own schema
// spells `roughness`, `focalLength`, `intensity` - while a typeless prim (a
// `class` prim of X3) has no schema at all, so every value of it travels as an
// `erhe:Owner:name` custom attribute. `visible`, `purpose` and `active` are
// carried natively either way: they are prim metadata and plain tokens, which
// every prim has.
enum class Native_property_form : unsigned int {
    schema_attributes = 0,
    custom_attributes = 1
};

[[nodiscard]] auto is_native_usd_property(
    const std::string_view     owner,
    const std::string_view     name,
    const Native_property_form form
) -> bool
{
    if (owner == "Item_base") {
        return (name == "visible") || (name == "purpose") || (name == "active");
    }
    if (form == Native_property_form::custom_attributes) {
        return false;
    }
    if (owner == "Material") {
        return
            (name == "base_color")                 || (name == "emissive")          ||
            (name == "metallic")                   || (name == "roughness")         ||
            (name == "opacity")                    || (name == "ior")               ||
            (name == "occlusion_texture_strength") || (name == "blending_mode")     ||
            (name == "alpha_cutoff")               || (name == "base_color_texture") ||
            (name == "metallic_roughness_texture") || (name == "normal_texture")    ||
            (name == "occlusion_texture")          || (name == "emissive_texture");
    }
    if (owner == "Camera") {
        return
            (name == "projection_type") || (name == "fov_x")       || (name == "fov_y")        ||
            (name == "ortho_left")      || (name == "ortho_width") || (name == "ortho_bottom") ||
            (name == "ortho_height")    || (name == "z_near")      || (name == "z_far")        ||
            (name == "infinite_z_far")  || (name == "exposure");
    }
    if (owner == "Light") {
        return
            (name == "light_type")  || (name == "color")            || (name == "intensity")        ||
            (name == "temperature") || (name == "inner_spot_angle") || (name == "outer_spot_angle") ||
            (name == "cast_shadow");
    }
    return false;
}

// One `erhe:` custom attribute value. The USD type follows the erhe property
// type and the value is the one the importer parses back with the D16
// `from_string` after stripping brackets, commas and quotes. A quaternion
// travels as a `float4` in erhe's `x y z w` component order, which is what
// that parse expects - a USD `quatf` prints its real part first.
[[nodiscard]] auto make_custom_attribute(
    const erhe::property::Dependency_property& property,
    const erhe::property::Property_value&      value
) -> lightusd::Attribute
{
    lightusd::Attribute        attribute;
    lightusd::primvar::PrimVar var;
    switch (property.get_type()) {
        case erhe::property::Property_type::boolean: {
            var.set_value(std::get<bool>(value));
            break;
        }
        case erhe::property::Property_type::integer: {
            var.set_value(static_cast<int32_t>(std::get<int>(value)));
            break;
        }
        case erhe::property::Property_type::floating: {
            var.set_value(std::get<float>(value));
            break;
        }
        case erhe::property::Property_type::vec2: {
            const glm::vec2 v = std::get<glm::vec2>(value);
            var.set_value(lightusd::value::float2{v.x, v.y});
            break;
        }
        case erhe::property::Property_type::vec3: {
            const glm::vec3 v = std::get<glm::vec3>(value);
            var.set_value(lightusd::value::float3{v.x, v.y, v.z});
            break;
        }
        case erhe::property::Property_type::vec4: {
            const glm::vec4 v = std::get<glm::vec4>(value);
            var.set_value(lightusd::value::float4{v.x, v.y, v.z, v.w});
            break;
        }
        case erhe::property::Property_type::quat: {
            const glm::quat q = std::get<glm::quat>(value);
            var.set_value(lightusd::value::float4{q.x, q.y, q.z, q.w});
            break;
        }
        case erhe::property::Property_type::ivec2: {
            const glm::ivec2 v = std::get<glm::ivec2>(value);
            var.set_value(lightusd::value::int2{v.x, v.y});
            break;
        }
        case erhe::property::Property_type::ivec3: {
            const glm::ivec3 v = std::get<glm::ivec3>(value);
            var.set_value(lightusd::value::int3{v.x, v.y, v.z});
            break;
        }
        case erhe::property::Property_type::ivec4: {
            const glm::ivec4 v = std::get<glm::ivec4>(value);
            var.set_value(lightusd::value::int4{v.x, v.y, v.z, v.w});
            break;
        }
        case erhe::property::Property_type::double_floating: {
            var.set_value(std::get<double>(value));
            break;
        }
        case erhe::property::Property_type::mat4: {
            var.set_value(to_usd(std::get<glm::mat4>(value)));
            break;
        }
        case erhe::property::Property_type::asset_path: {
            var.set_value(lightusd::value::AssetPath{std::get<erhe::property::Asset_path>(value).path});
            break;
        }
        case erhe::property::Property_type::float_array: {
            var.set_value(std::get<std::vector<float>>(value));
            break;
        }
        case erhe::property::Property_type::int_array: {
            const std::vector<int>& v = std::get<std::vector<int>>(value);
            var.set_value(std::vector<int32_t>{v.begin(), v.end()});
            break;
        }
        case erhe::property::Property_type::enumeration: {
            var.set_value(lightusd::value::token{erhe::property::to_string(property, value)});
            break;
        }
        default: {
            // string, and object: the D16 text form, which for an object
            // reference is the pointee's get_reference_path().
            var.set_value(erhe::property::to_string(property, value));
            break;
        }
    }
    attribute.set_var(std::move(var));
    return attribute;
}

class Exporter final
{
public:
    Exporter(const Usd_save_arguments& arguments, Usd_save_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    void write()
    {
        ERHE_PROFILE_FUNCTION();

        if (!m_arguments.root_node) {
            m_result.error = "no root node to write";
            return;
        }

        for (std::size_t index = 0, end = m_arguments.materials.size(); index < end; ++index) {
            if (m_arguments.materials[index]) {
                m_material_indices[m_arguments.materials[index].get()] = index;
            }
        }

        for (const Usd_save_prim_references& entry : m_arguments.references) {
            if (entry.item && !entry.references.empty()) {
                m_prim_references[entry.item.get()] = &entry.references;
            }
        }

        // Pass one: where every prim lands on the stage. A prim's path is
        // only known once its ancestors have their sanitized, sibling-unique
        // names and the wrapper decision below is made, and a mesh binds its
        // material by that path, so the whole tree is planned before
        // anything is written.
        std::vector<Plan_prim> plan;
        Name_scope             top_level_names;
        plan_children(*m_arguments.root_node.get(), glm::mat4{1.0f}, top_level_names, plan);

        // Several top-level prims are gathered under one Xform that plays the
        // erhe root's part, so the stage still names one defaultPrim.
        const bool        wrap              = (plan.size() != 1);
        const std::string default_prim_name = wrap ? std::string{c_world_prim_name} : plan.front().name;
        assign_paths(plan, wrap ? fmt::format("/{}", c_world_prim_name) : std::string{});
        record_resource_paths(plan);

        // Pass two: the prims themselves.
        lightusd::Stage             stage;
        std::vector<lightusd::Prim> content_prims;
        write_plan_prims(plan, content_prims);

        if (!wrap) {
            lightusd::Prim& prim = content_prims.front();
            add_collections(prim);
            add_root_prim(stage, std::move(prim));
        } else {
            lightusd::Xform world;
            world.name = c_world_prim_name;
            lightusd::Prim world_prim{world};
            for (lightusd::Prim& prim : content_prims) {
                std::string error;
                if (!world_prim.add_child(std::move(prim), false, &error)) {
                    add_warning(fmt::format("a top-level prim could not be added under '{}': {}", c_world_prim_name, error));
                }
            }
            add_collections(world_prim);
            add_root_prim(stage, std::move(world_prim));
        }

        stage.metas().defaultPrim = lightusd::value::token{default_prim_name};
        stage.metas().upAxis.set_value(
            (m_arguments.up_axis == "Z") ? lightusd::Axis::Z :
            (m_arguments.up_axis == "X") ? lightusd::Axis::X : lightusd::Axis::Y
        );
        stage.metas().metersPerUnit.set_value(m_arguments.meters_per_unit);
        for (const std::pair<const std::string, std::string>& entry : m_arguments.custom_layer_data) {
            stage.metas().customLayerData[entry.first] = lightusd::MetaVariable{entry.second};
        }
        if (!m_arguments.custom_layer_data.empty()) {
            stage.metas().customLayerDataAuthored = true;
        }
        stage.commit();

        const std::string filename = m_arguments.path.generic_string();
        std::string       warning;
        std::string       error;
        if (!lightusd::usda::SaveAsUSDA(filename, stage, &warning, &error)) {
            m_result.error = error.empty() ? std::string{"USDA write failed"} : error;
            log_usd->error("Writing USD stage '{}' failed: {}", filename, m_result.error);
            return;
        }
        if (!warning.empty()) {
            add_warning(warning);
        }
        log_usd->info(
            "USD '{}': {} node(s), {} mesh prim(s), {} material prim(s)",
            filename, m_node_count, m_mesh_count, m_material_paths.size()
        );
    }

private:
    void add_warning(const std::string& text)
    {
        if (!m_result.warning.empty()) {
            m_result.warning += "; ";
        }
        m_result.warning += text;
        log_usd->warn("USD '{}': {}", m_arguments.path.generic_string(), text);
    }

    void add_root_prim(lightusd::Stage& stage, lightusd::Prim&& prim)
    {
        if (!stage.add_root_prim(std::move(prim), false)) {
            add_warning("a root prim could not be added to the stage");
        }
    }

    // -------------------------------------------------------------------
    // Property values
    // -------------------------------------------------------------------

    [[nodiscard]] static auto is_local(
        const erhe::property::Dependency_object&   object,
        const erhe::property::Dependency_property& property
    ) -> bool
    {
        return object.get_value_source(property) == erhe::property::Value_source::local;
    }

    // Every local value of `object` that no schema attribute of the prim
    // already carries, as an `erhe:Owner:name` custom attribute
    // (doc/usd_compatibility.md, property system). A bridged property is the
    // object's own engineered representation - the node transform, the item
    // name and tags - and travels in the USD form that owns it; a property
    // without the serialize flag is session state, and so is an expression
    // (D14).
    template <typename T>
    void write_erhe_properties(
        const erhe::property::Dependency_object& object,
        T&                                       typed_prim,
        const Native_property_form               form = Native_property_form::schema_attributes
    )
    {
        const erhe::property::Owner_type object_owner_type = object.get_property_owner_type();
        object.for_each_local_value(
            [this, &object, object_owner_type, &typed_prim, form](
                const erhe::property::Dependency_property& property,
                const erhe::property::Property_value&      value
            ) {
                const erhe::property::Property_metadata& metadata = property.get_metadata(object_owner_type);
                if ((metadata.flags & erhe::property::Property_flags::serialize) == 0u) {
                    return;
                }
                if (metadata.bridge.is_bound()) {
                    return;
                }
                const std::string_view owner_name = erhe::property::get_owner_type_name(property.get_owner_type());
                if (is_native_usd_property(owner_name, property.get_name(), form)) {
                    return;
                }
                if (object.get_expression(property).has_value()) {
                    return;
                }
                const std::string attribute_name = fmt::format("erhe:{}:{}", owner_name, property.get_name());
                if (typed_prim.props.find(attribute_name) != typed_prim.props.end()) {
                    add_warning(
                        fmt::format("'{}' is authored by more than one item of one prim - the later value is dropped", attribute_name)
                    );
                    return;
                }
                typed_prim.props.emplace(attribute_name, lightusd::Property{make_custom_attribute(property, value), true});
            }
        );
    }

    // `visible` and `purpose` of the item that holds the prim's place in the
    // scene graph. The importer reads both from the prim onto the node, so
    // the node is what the writer reads them from.
    template <typename T>
    void write_visibility_and_purpose(const erhe::Item_base& item, T& typed_prim)
    {
        if (is_local(item, erhe::Item_base::visible_property.get())) {
            typed_prim.visibility.set_value(
                item.get_value(erhe::Item_base::visible_property)
                    ? lightusd::Visibility::Inherited
                    : lightusd::Visibility::Invisible
            );
        }
        if (is_local(item, erhe::Item_base::purpose_property.get())) {
            typed_prim.purpose.set_value(to_usd_purpose(item.get_value(erhe::Item_base::purpose_property)));
        }
        write_active(item, typed_prim);
    }

    // The `active` prim metadatum (doc/usd-compatibility-plan.md X2): an
    // authored value only, so a prim of a scene erhe never deactivated
    // carries no `active` line. USD prunes the whole subtree of an inactive
    // prim, which is what the derived Item_flags::active bit does in erhe;
    // the bit is never written - the item's own opinion is.
    template <typename T>
    void write_active(const erhe::Item_base& item, T& typed_prim)
    {
        if (is_local(item, erhe::Item_base::active_property.get())) {
            typed_prim.meta.set_active(item.get_value(erhe::Item_base::active_property));
        }
    }

    // -------------------------------------------------------------------
    // Materials
    // -------------------------------------------------------------------

    // The caller's texture list is indexed by the material's place in
    // Usd_save_arguments::materials, which is the scene's own resource index;
    // the tree decides where the material prim goes, so the index is looked
    // up per material and a material the caller did not list has no texture.
    [[nodiscard]] auto texture_of(
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot
    ) const -> const Usd_save_texture*
    {
        const std::map<const erhe::primitive::Material*, std::size_t>::const_iterator i = m_material_indices.find(&material);
        if (i == m_material_indices.end()) {
            return nullptr;
        }
        for (const Usd_save_texture& texture : m_arguments.textures) {
            if ((texture.material_index == i->second) && (texture.slot == slot) && !texture.path.empty()) {
                return &texture;
            }
        }
        return nullptr;
    }

    // The image as the USDA asset identifier: relative to the written file's
    // own directory, so a scene and its images keep working when the pair is
    // moved together.
    [[nodiscard]] auto asset_path_of(const std::filesystem::path& image_path) const -> std::string
    {
        const std::filesystem::path directory = m_arguments.path.parent_path();
        if (!directory.empty()) {
            std::error_code             error_code{};
            const std::filesystem::path relative = std::filesystem::relative(image_path, directory, error_code);
            if (!error_code && !relative.empty()) {
                return relative.generic_string();
            }
        }
        return image_path.generic_string();
    }

    [[nodiscard]] static auto texture_shader_name(const Usd_material_texture_slot slot) -> const char*
    {
        switch (slot) {
            case Usd_material_texture_slot::base_color:         return "base_color_texture";
            case Usd_material_texture_slot::metallic_roughness: return "metallic_roughness_texture";
            case Usd_material_texture_slot::normal:             return "normal_texture";
            case Usd_material_texture_slot::occlusion:          return "occlusion_texture";
            case Usd_material_texture_slot::emissive:           return "emissive_texture";
            default:                                            return "texture";
        }
    }

    // One UsdUVTexture shader prim under the material prim, created once per
    // slot; the surface inputs connect to its outputs.
    [[nodiscard]] auto get_texture_shader(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot
    ) -> std::string
    {
        const Texture_shader_key key{&material, slot};
        const std::map<Texture_shader_key, std::string>::const_iterator i = m_texture_shader_names.find(key);
        if (i != m_texture_shader_names.end()) {
            return i->second;
        }
        const Usd_save_texture* texture = texture_of(material, slot);
        if (texture == nullptr) {
            return {};
        }
        // The primvar reader every texture of the material reads its UVs
        // from; a material without a texture gets no shading network beyond
        // its surface.
        if (m_uv_reader_materials.insert(&material).second) {
            add_uv_reader(material_prim);
        }

        lightusd::UsdUVTexture uv_texture;
        uv_texture.file.set_value(lightusd::value::AssetPath{asset_path_of(texture->path)});
        uv_texture.sourceColorSpace.set_value(
            texture->srgb
                ? lightusd::UsdUVTexture::SourceColorSpace::SRGB
                : lightusd::UsdUVTexture::SourceColorSpace::Raw
        );
        uv_texture.st.set_connection(lightusd::Path{material_path + "/uv_reader", "outputs:result"});
        uv_texture.outputsR.set_authored(true);
        uv_texture.outputsG.set_authored(true);
        uv_texture.outputsB.set_authored(true);
        uv_texture.outputsRGB.set_authored(true);

        lightusd::Shader shader;
        shader.name    = texture_shader_name(slot);
        shader.info_id = "UsdUVTexture";
        shader.value   = std::move(uv_texture);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("texture shader '{}' could not be added: {}", shader.name, error));
            return {};
        }
        m_texture_shader_names[key] = shader.name;
        return shader.name;
    }

    void add_uv_reader(lightusd::Prim& material_prim)
    {
        lightusd::UsdPrimvarReader_float2 reader;
        reader.varname.set_value(std::string{"st"});
        reader.result.set_authored(true);

        lightusd::Shader shader;
        shader.name    = "uv_reader";
        shader.info_id = "UsdPrimvarReader_float2";
        shader.value   = std::move(reader);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("uv reader shader could not be added: {}", error));
        }
    }

    // A texture slot with a local value whose image the caller could not
    // resolve to a file: a generated texture has no bytes on disk, so the
    // slot stays out of the shading network (commit 1, see
    // src/erhe/usd/notes.md).
    void warn_about_unresolved_textures(const erhe::primitive::Material& material)
    {
        using Slot_property = erhe::property::Property<erhe::property::Object_reference>;
        const std::pair<const Slot_property*, Usd_material_texture_slot> slots[] = {
            {&erhe::primitive::Material::base_color_texture_property,         Usd_material_texture_slot::base_color},
            {&erhe::primitive::Material::metallic_roughness_texture_property, Usd_material_texture_slot::metallic_roughness},
            {&erhe::primitive::Material::normal_texture_property,             Usd_material_texture_slot::normal},
            {&erhe::primitive::Material::occlusion_texture_property,          Usd_material_texture_slot::occlusion},
            {&erhe::primitive::Material::emissive_texture_property,           Usd_material_texture_slot::emissive}
        };
        for (const std::pair<const Slot_property*, Usd_material_texture_slot>& slot : slots) {
            if (!is_local(material, slot.first->get())) {
                continue;
            }
            if (material.get_value(*slot.first).object == nullptr) {
                continue;
            }
            if (texture_of(material, slot.second) == nullptr) {
                add_warning(
                    fmt::format(
                        "material '{}' texture slot '{}' has no source file - the slot is not written",
                        material.get_name(), texture_shader_name(slot.second)
                    )
                );
            }
        }
    }

    // One `Material` prim where the material sits in the scene tree
    // (doc/usd-compatibility-plan.md U4): the material's own name and place,
    // its `UsdPreviewSurface` shader and the texture shaders that feed it.
    // The shader prims occupy the material prim's namespace, so a prim the
    // user parented to a material is not written.
    [[nodiscard]] auto write_material_prim(const Plan_prim& plan_prim) -> lightusd::Prim
    {
        const erhe::primitive::Material& material = *plan_prim.material;

        lightusd::Material usd_material;
        usd_material.name = plan_prim.name;
        usd_material.surface.set(lightusd::Path{plan_prim.path + "/surface", "outputs:surface"});
        write_erhe_properties(material, usd_material);

        lightusd::Prim material_prim{usd_material};
        warn_about_unresolved_textures(material);
        write_surface_shader(material_prim, plan_prim.path, material);
        return material_prim;
    }

    void write_surface_shader(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material
    )
    {
        using erhe::primitive::Material;

        lightusd::UsdPreviewSurface surface;
        surface.outputsSurface.set_authored(true);

        if (is_local(material, Material::base_color_property.get())) {
            const glm::vec3 base_color = material.get_value(Material::base_color_property);
            surface.diffuseColor.set_value(lightusd::value::color3f{base_color.x, base_color.y, base_color.z});
        }
        if (is_local(material, Material::emissive_property.get())) {
            const glm::vec3 emissive = material.get_value(Material::emissive_property);
            surface.emissiveColor.set_value(lightusd::value::color3f{emissive.x, emissive.y, emissive.z});
        }
        if (is_local(material, Material::metallic_property.get())) {
            surface.metallic.set_value(material.get_value(Material::metallic_property));
        }
        if (is_local(material, Material::roughness_property.get())) {
            // UsdPreviewSurface has one roughness; erhe's is anisotropic and
            // its x component is the isotropic one.
            surface.roughness.set_value(material.get_value(Material::roughness_property).x);
        }
        if (is_local(material, Material::opacity_property.get())) {
            surface.opacity.set_value(material.get_value(Material::opacity_property));
        }
        if (is_local(material, Material::ior_property.get())) {
            surface.ior.set_value(material.get_value(Material::ior_property));
        }
        if (is_local(material, Material::occlusion_texture_strength_property.get())) {
            surface.occlusion.set_value(material.get_value(Material::occlusion_texture_strength_property));
        }
        // UsdPreviewSurface says a positive opacityThreshold is a cutout;
        // that threshold is the only form the erhe blending mode has, so it
        // is written for an alpha-test material and for nothing else.
        if (material.get_value(Material::blending_mode_property) == erhe::primitive::Material_blending_mode::alpha_test) {
            surface.opacityThreshold.set_value(material.get_value(Material::alpha_cutoff_property));
        }

        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::base_color, "outputs:rgb", surface.diffuseColor);
        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::emissive,   "outputs:rgb", surface.emissiveColor);
        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::normal,     "outputs:rgb", surface.normal);
        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::occlusion,  "outputs:r",   surface.occlusion);
        // erhe has one metallic-roughness slot; UsdPreviewSurface reads the
        // two channels through separate inputs of the one texture, in the
        // glTF channel layout the importer expects back.
        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness, "outputs:g", surface.roughness);
        connect_texture(material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness, "outputs:b", surface.metallic);

        lightusd::Shader shader;
        shader.name    = "surface";
        shader.info_id = "UsdPreviewSurface";
        shader.value   = std::move(surface);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("surface shader of '{}' could not be added: {}", material_path, error));
        }
    }

    template <typename T>
    void connect_texture(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot,
        const char*                      output_name,
        T&                               input
    )
    {
        const std::string shader_name = get_texture_shader(material_prim, material_path, material, slot);
        if (shader_name.empty()) {
            return;
        }
        input.set_connection(lightusd::Path{material_path + "/" + shader_name, output_name});
        input.set_value_empty();
    }

    void bind_material(lightusd::MaterialBinding& binding, const erhe::primitive::Material* material)
    {
        if (material == nullptr) {
            return;
        }
        const std::map<const erhe::primitive::Material*, std::string>::const_iterator i = m_material_paths.find(material);
        if (i == m_material_paths.end()) {
            add_warning(
                fmt::format("material '{}' is used but was not offered to the writer - the binding is dropped", material->get_name())
            );
            return;
        }
        lightusd::Relationship relationship;
        relationship.set(lightusd::Path{i->second, ""});
        binding.set_materialBinding(relationship);
    }

    // -------------------------------------------------------------------
    // Nodes
    // -------------------------------------------------------------------

    // Pass one over the children of `parent`. The filter is the glTF
    // exporter's - an import_root container is unwrapped with its transform
    // composed in, a render proxy is derived data rebuilt by its owner, and
    // anything without Item_flags::content is transient editor furniture
    // (tool visuals, controllers, rendertarget UI quads) recreated every
    // session - widened by the resources a USD file carries: a resource prim
    // carries show_in_ui rather than content, so a material and every prim on
    // the way down to one are planned as well
    // (doc/usd-compatibility-plan.md U4).
    void plan_children(
        const erhe::Hierarchy&  parent,
        const glm::mat4&        pre_transform,
        Name_scope&             names,
        std::vector<Plan_prim>& out_prims
    )
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            const erhe::Typed* child_prim = dynamic_cast<const erhe::Typed*>(child.get());
            if (child_prim == nullptr) {
                continue;
            }
            const uint64_t           flags      = child_prim->get_flag_bits();
            const erhe::scene::Node* child_node = dynamic_cast<const erhe::scene::Node*>(child.get());
            if ((child_node != nullptr) && ((flags & erhe::Item_flags::import_root) != 0)) {
                plan_children(
                    *child_node,
                    pre_transform * child_node->parent_from_node_transform().get_matrix(),
                    names,
                    out_prims
                );
                continue;
            }
            if ((flags & erhe::Item_flags::render_proxy) != 0) {
                continue;
            }
            if (((flags & erhe::Item_flags::content) == 0) && !holds_carried_resource(*child_prim)) {
                continue;
            }
            Plan_prim plan_prim{};
            plan_prim.item          = child_prim;
            plan_prim.node          = child_node;
            plan_prim.material      = dynamic_cast<const erhe::primitive::Material*>(child.get());
            plan_prim.pre_transform = pre_transform;
            plan_prim.name          = names.make_unique(child_prim->get_name());
            plan_prim.references    = find_prim_references(*child_prim);
            if (plan_prim.references != nullptr) {
                plan_instance_overrides(*child_prim, plan_prim);
            } else if (plan_prim.material == nullptr) {
                // A prim that carries a transform writes it on itself, so its
                // children start from identity; a prim without one passes the
                // transform that reached it through to them.
                Name_scope child_names;
                plan_children(
                    *child_prim,
                    (child_node != nullptr) ? glm::mat4{1.0f} : pre_transform,
                    child_names,
                    plan_prim.children
                );
            }
            out_prims.push_back(std::move(plan_prim));
        }
    }

    // The arcs the caller named for this prim, null when it named none.
    [[nodiscard]] auto find_prim_references(const erhe::Typed& prim) const -> const std::vector<Usd_save_reference>*
    {
        const std::map<const erhe::Item_base*, const std::vector<Usd_save_reference>*>::const_iterator i =
            m_prim_references.find(&prim);
        return (i != m_prim_references.end()) ? i->second : nullptr;
    }

    // A carrier's children are the instance content the arcs' targets supply,
    // so none of them is written as a prim of its own: what a carrier writes
    // of them is the overrides they hold (doc/usd-compatibility-plan.md X2).
    // The clone of an arc's target prim is the carrier prim itself - X1's one
    // extra level - so its overrides are authored on the carrier, and every
    // deeper item becomes an `over` prim at the path it has below the
    // carrier. A child that names no counterpart is a prim the user parented
    // under the carrier by hand: the structure inside a reference is not
    // written (plan section 5), so it is named in a warning and left out.
    void plan_instance_overrides(const erhe::Typed& carrier, Plan_prim& plan_prim)
    {
        warn_about_unreferenced_carrier_children(carrier, plan_prim.name);

        std::set<std::string> planned_paths;
        for (erhe::scene::Instance_override_item& entry : erhe::scene::collect_instance_override_items(carrier)) {
            if (!planned_paths.insert(entry.relative_path).second) {
                add_warning(
                    fmt::format(
                        "prim '{}': more than one instance holds overrides at '{}' - only the first arc's are written",
                        plan_prim.name,
                        entry.relative_path
                    )
                );
                continue;
            }
            if (entry.relative_path.empty()) {
                plan_prim.override_root = entry.item;
                if (entry.transform_overridden) {
                    add_warning(
                        fmt::format(
                            "prim '{}': the transform of an arc's target is the referencing prim's own transform - the override is not written",
                            plan_prim.name
                        )
                    );
                }
                continue;
            }
            plan_prim.overrides.push_back(std::move(entry));
        }
    }

    // A child of a carrier that names no counterpart in a template is not
    // instance content; saying so is the only notice the user gets that it is
    // not written.
    void warn_about_unreferenced_carrier_children(const erhe::Typed& carrier, const std::string& carrier_name)
    {
        std::string names;
        for (const std::shared_ptr<erhe::Hierarchy>& child : carrier.get_children()) {
            const erhe::Typed* child_prim = dynamic_cast<const erhe::Typed*>(child.get());
            if (child_prim == nullptr) {
                continue;
            }
            if (child_prim->get_reference()) {
                continue; // instance content: the arc's target supplies it
            }
            if (!names.empty()) {
                names += ", ";
            }
            names += child_prim->get_name();
        }
        if (!names.empty()) {
            add_warning(
                fmt::format(
                    "prim '{}' carries composition arcs and holds prims that are not instance content ({}) - they are not written",
                    carrier_name,
                    names
                )
            );
        }
    }

    // A style item (doc/style-library.md D2), which is a `class` prim on the
    // stage. erhe::usd depends on no editor type, so the class is recognized
    // by the token erhe::Typed fixes for it - the same token the importer
    // writes as the prim's typeName for every other class.
    [[nodiscard]] static auto is_style_prim(const erhe::Typed& prim) -> bool
    {
        return prim.get_class_type_name() == c_style_class_type_name;
    }

    // A resource prim a USD file carries, or a prim on the way down to one.
    // A resource is not content, so this is what widens the content filter;
    // today the file carries materials, and plan step E4 adds the other
    // kinds.
    [[nodiscard]] static auto holds_carried_resource(const erhe::Typed& prim) -> bool
    {
        if (erhe::is<erhe::primitive::Material>(&prim) || is_style_prim(prim)) {
            return true;
        }
        for (const std::shared_ptr<erhe::Hierarchy>& child : prim.get_children()) {
            const erhe::Typed* child_prim = dynamic_cast<const erhe::Typed*>(child.get());
            if (child_prim == nullptr) {
                continue;
            }
            if ((child_prim->get_flag_bits() & erhe::Item_flags::render_proxy) != 0) {
                continue;
            }
            if (holds_carried_resource(*child_prim)) {
                return true;
            }
        }
        return false;
    }

    // The stage path of every planned prim: its parent's path and its own
    // name. `parent_path` is empty at the top level, or the wrapper prim's
    // path when the writer adds one.
    static void assign_paths(std::vector<Plan_prim>& prims, const std::string& parent_path)
    {
        for (Plan_prim& prim : prims) {
            prim.path = parent_path + "/" + prim.name;
            assign_paths(prim.children, prim.path);
        }
    }

    // Where each material and each style of the scene ends up, so a mesh
    // written later binds its material by that path and a prim with a style
    // names its class prim by that path.
    void record_resource_paths(const std::vector<Plan_prim>& prims)
    {
        for (const Plan_prim& prim : prims) {
            if (prim.material != nullptr) {
                m_material_paths[prim.material] = prim.path;
            }
            if ((prim.item != nullptr) && is_style_prim(*prim.item)) {
                m_style_paths[prim.item] = prim.path;
            }
            record_resource_paths(prim.children);
        }
    }

    void write_plan_prims(const std::vector<Plan_prim>& plan, std::vector<lightusd::Prim>& out_prims)
    {
        out_prims.reserve(plan.size());
        for (const Plan_prim& plan_prim : plan) {
            out_prims.push_back(write_plan_prim(plan_prim));
        }
    }

    // Pass two: one planned prim as one prim of the stage, with the prims
    // planned below it as its children.
    [[nodiscard]] auto write_plan_prim(const Plan_prim& plan_prim) -> lightusd::Prim
    {
        record_tags(*plan_prim.item, plan_prim.path);

        lightusd::Prim prim =
            (plan_prim.material != nullptr) ? write_material_prim(plan_prim) :
            (plan_prim.node     != nullptr) ? write_node         (plan_prim) :
                                              write_prim         (plan_prim);

        if (plan_prim.references != nullptr) {
            write_references(prim, *plan_prim.references);
        }
        write_inherits(prim, *plan_prim.item);

        std::vector<lightusd::Prim> child_prims;
        write_plan_prims(plan_prim.children, child_prims);
        write_override_prims(plan_prim, child_prims);
        for (lightusd::Prim& child_prim : child_prims) {
            std::string error;
            if (!prim.add_child(std::move(child_prim), false, &error)) {
                add_warning(fmt::format("a child of '{}' could not be added: {}", plan_prim.name, error));
            }
        }
        return prim;
    }

    // The `over` prims of one carrier (doc/usd-compatibility-plan.md X2). One
    // entry per item that holds overrides, at the path it has below the
    // carrier; an item on the way down to one that holds none is still an
    // `over`, with no attributes, so the path exists.
    class Override_prim final
    {
    public:
        std::string                name;
        const erhe::Item_base*     item                {nullptr};
        bool                       transform_overridden{false};
        std::vector<Override_prim> children;
    };

    [[nodiscard]] static auto find_or_add_override_prim(std::vector<Override_prim>& prims, const std::string_view name) -> Override_prim&
    {
        for (Override_prim& prim : prims) {
            if (prim.name == name) {
                return prim;
            }
        }
        prims.push_back(Override_prim{.name = std::string{name}});
        return prims.back();
    }

    void write_override_prims(const Plan_prim& plan_prim, std::vector<lightusd::Prim>& out_prims)
    {
        if (plan_prim.overrides.empty()) {
            return;
        }
        std::vector<Override_prim> tree;
        for (const erhe::scene::Instance_override_item& entry : plan_prim.overrides) {
            std::vector<Override_prim>* level = &tree;
            Override_prim*              prim  = nullptr;
            std::size_t                 start = 0;
            while (start < entry.relative_path.size()) {
                const std::size_t      slash = entry.relative_path.find('/', start);
                const std::string_view name  = (slash == std::string::npos)
                    ? std::string_view{entry.relative_path}.substr(start)
                    : std::string_view{entry.relative_path}.substr(start, slash - start);
                start = (slash == std::string::npos) ? entry.relative_path.size() : (slash + 1);
                prim  = &find_or_add_override_prim(*level, name);
                level = &prim->children;
            }
            if (prim != nullptr) {
                prim->item                 = entry.item;
                prim->transform_overridden = entry.transform_overridden;
            }
        }
        for (const Override_prim& prim : tree) {
            out_prims.push_back(write_override_prim(prim));
        }
    }

    // One `over` prim: a prim with no typeName, so it contributes opinions
    // and defines nothing. Being typeless it carries no schema attribute, so
    // every value of it travels as an `erhe:Owner:name` custom attribute -
    // the form the X2 reader reads an `over` back in - and only `visibility`,
    // `purpose`, the `active` metadatum and the xformOps of an overridden
    // transform keep their native forms. That is what lets a schema-named
    // value overridden inside an instance travel: a `Material` item's
    // `roughness` is `erhe:Material:roughness` on its `over`, because the
    // `inputs:` of a `UsdPreviewSurface` are written on the def'd shader prim
    // below a `Material` prim, which an `over` of the material does not have.
    [[nodiscard]] auto write_override_prim(const Override_prim& override_prim) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name = override_prim.name;
        model.spec = lightusd::Specifier::Over;
        if (override_prim.item != nullptr) {
            write_token_visibility_and_purpose(*override_prim.item, model.props);
            write_active(*override_prim.item, model);
            write_erhe_properties(*override_prim.item, model, Native_property_form::custom_attributes);
            if (override_prim.transform_overridden) {
                write_override_xform_ops(*override_prim.item, model.props);
            }
        }
        lightusd::Prim prim{model};
        for (const Override_prim& child : override_prim.children) {
            std::string error;
            if (!prim.add_child(write_override_prim(child), false, &error)) {
                add_warning(fmt::format("an override of '{}' could not be added: {}", override_prim.name, error));
            }
        }
        return prim;
    }

    // `visibility` and `purpose` of a typeless prim - an `over` (X2) or a
    // `class` (X3): such a prim carries no schema attributes, so the two
    // travel as the plain token attributes they are on the stage.
    static void write_token_visibility_and_purpose(const erhe::Item_base& item, std::map<std::string, lightusd::Property>& props)
    {
        if (is_local(item, erhe::Item_base::visible_property.get())) {
            add_token_attribute(
                props,
                "visibility",
                lightusd::to_string(
                    item.get_value(erhe::Item_base::visible_property)
                        ? lightusd::Visibility::Inherited
                        : lightusd::Visibility::Invisible
                )
            );
        }
        if (is_local(item, erhe::Item_base::purpose_property.get())) {
            add_token_attribute(props, "purpose", lightusd::to_string(to_usd_purpose(item.get_value(erhe::Item_base::purpose_property))));
        }
    }

    static void add_token_attribute(std::map<std::string, lightusd::Property>& props, const std::string& name, const std::string& text)
    {
        lightusd::Attribute attribute;
        attribute.set_value(lightusd::value::token{text});
        props.emplace(name, lightusd::Property{std::move(attribute), false});
    }

    // The xformOps of an `over`, as the `xformOp:*` attributes and the
    // `xformOpOrder` a typed prim's writer emits from its `xformOps` member
    // (doc/usd-compatibility-plan.md M8).
    void write_override_xform_ops(const erhe::Item_base& item, std::map<std::string, lightusd::Property>& props)
    {
        const erhe::scene::Node* node = dynamic_cast<const erhe::scene::Node*>(&item);
        if (node == nullptr) {
            add_warning(fmt::format("the transform override of '{}' is on an item that carries none", item.get_name()));
            return;
        }
        std::vector<lightusd::XformOp> ops;
        set_transform(ops, *node, node->parent_from_node_transform().get_matrix());

        std::vector<lightusd::value::token> order;
        for (const lightusd::XformOp& op : ops) {
            std::string token_text;
            if (op.inverted) {
                token_text += "!invert!";
            }
            token_text += lightusd::to_string(op.op_type);
            if (!op.suffix.empty()) {
                token_text += ":";
                token_text += op.suffix;
            }
            order.push_back(lightusd::value::token{token_text});
            if (op.op_type == lightusd::XformOp::OpType::ResetXformStack) {
                continue;
            }
            std::string attribute_name = lightusd::to_string(op.op_type);
            if (!op.suffix.empty()) {
                attribute_name += ":";
                attribute_name += op.suffix;
            }
            lightusd::Attribute        attribute;
            lightusd::primvar::PrimVar var = op.get_var();
            attribute.set_var(var);
            props.emplace(attribute_name, lightusd::Property{std::move(attribute), false});
        }
        if (order.empty()) {
            return;
        }
        lightusd::Attribute        order_attribute;
        lightusd::primvar::PrimVar order_var;
        order_var.set_value(order);
        order_attribute.set_var(order_var);
        order_attribute.variability() = lightusd::Variability::Uniform;
        props.emplace("xformOpOrder", lightusd::Property{std::move(order_attribute), false});
    }

    // The style of a prim as the `inherits` arc it is
    // (doc/usd-compatibility-plan.md X3): one explicit list op naming the
    // class prim's path, the way a material binding names a material prim's
    // path. A style whose class prim is not in the written tree - a style of
    // another scene - is one warning and no arc.
    void write_inherits(lightusd::Prim& prim, const erhe::Item_base& item)
    {
        const std::shared_ptr<const erhe::property::Dependency_object>& style = item.get_style();
        if (!style) {
            return;
        }
        const std::map<const erhe::Item_base*, std::string>::const_iterator i = m_style_paths.find(
            dynamic_cast<const erhe::Item_base*>(style.get())
        );
        if (i == m_style_paths.end()) {
            add_warning(
                fmt::format("the style of '{}' is not a prim of the written scene - the inherits arc is dropped", item.get_name())
            );
            return;
        }
        prim.metas().inherits = std::vector<std::pair<lightusd::ListEditQual, std::vector<lightusd::Path>>>{
            std::make_pair(
                lightusd::ListEditQual::ResetToExplicit,
                std::vector<lightusd::Path>{lightusd::Path{i->second, ""}}
            )
        };
    }

    // The composition arcs of a carrier prim, as the `references` and
    // `payload` list ops USD reads them back from: one unqualified (explicit)
    // op per arc kind, holding the arcs in the order the caller named them,
    // which is the order USD composes them in
    // (doc/usd-compatibility-plan.md X1).
    void write_references(lightusd::Prim& prim, const std::vector<Usd_save_reference>& references)
    {
        std::vector<lightusd::Reference> usd_references;
        std::vector<lightusd::Payload>   usd_payloads;
        for (const Usd_save_reference& reference : references) {
            const std::string asset_path = to_reference_asset_path(reference.source_path, m_arguments.path);
            if (asset_path.empty() && reference.prim_path.empty()) {
                add_warning("a composition arc names neither a file nor a prim - it is not written");
                continue;
            }
            const lightusd::Path prim_path = reference.prim_path.empty()
                ? lightusd::Path{}
                : lightusd::Path{reference.prim_path, ""};
            if (reference.kind == Usd_reference_kind::payload) {
                lightusd::Payload payload;
                payload.asset_path = lightusd::value::AssetPath{asset_path};
                payload.prim_path  = prim_path;
                usd_payloads.push_back(std::move(payload));
            } else {
                lightusd::Reference usd_reference;
                usd_reference.asset_path = lightusd::value::AssetPath{asset_path};
                usd_reference.prim_path  = prim_path;
                usd_references.push_back(std::move(usd_reference));
            }
        }
        lightusd::PrimMetas& metas = prim.metas();
        if (!usd_references.empty()) {
            metas.references = std::vector<std::pair<lightusd::ListEditQual, std::vector<lightusd::Reference>>>{
                std::make_pair(lightusd::ListEditQual::ResetToExplicit, std::move(usd_references))
            };
        }
        if (!usd_payloads.empty()) {
            metas.payload = std::vector<std::pair<lightusd::ListEditQual, std::vector<lightusd::Payload>>>{
                std::make_pair(lightusd::ListEditQual::ResetToExplicit, std::move(usd_payloads))
            };
        }
    }

    // One prim of a class that carries no transform as one prim of the
    // stage: the `typeName` is the class's (doc/usd-compatibility-plan.md
    // C5). The transform that reached it composes with its children, which
    // is what the importer inverts.
    [[nodiscard]] auto write_prim(const Plan_prim& plan_prim) -> lightusd::Prim
    {
        if (is_style_prim(*plan_prim.item)) {
            return write_class_prim(*plan_prim.item, plan_prim.name);
        }
        return erhe::is<erhe::Scope>(plan_prim.item)
            ? write_scope_prim(*plan_prim.item, plan_prim.name)
            : write_typed_prim(*plan_prim.item, plan_prim.name);
    }

    // A style item as the `class` prim it is (doc/usd-compatibility-plan.md
    // X3): a typeless prim that defines nothing and holds the opinions its
    // `inherits` arcs hand on. It carries no schema, so every value of it -
    // `Material.roughness`, `Mesh.shadow_cast` - travels as an
    // `erhe:Owner:name` custom attribute, and `visibility` / `purpose` as the
    // plain token attributes a typeless prim spells them with.
    [[nodiscard]] auto write_class_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name = prim_name;
        model.spec = lightusd::Specifier::Class;
        write_token_visibility_and_purpose(item, model.props);
        write_active(item, model);
        write_erhe_properties(item, model, Native_property_form::custom_attributes);
        return lightusd::Prim{model};
    }

    [[nodiscard]] auto write_scope_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::Scope scope;
        scope.name = prim_name;
        write_visibility_and_purpose(item, scope);
        write_erhe_properties(item, scope);
        return lightusd::Prim{scope};
    }

    // A prim whose type erhe has no class for travels as its `typeName`, its
    // name and its children - the schema attributes of such a prim are not
    // carried (plan section 5). An empty token is the typeless `def` it was.
    [[nodiscard]] auto write_typed_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name           = prim_name;
        model.prim_type_name = std::string{item.get_prim_type_name()};
        write_active(item, model);
        return lightusd::Prim{model};
    }

    // One erhe node as one prim: a `Mesh`, `Camera` or UsdLux prim for a prim
    // of that class and an `Xform` for a plain one, each with its own xformOp
    // (doc/usd_compatibility.md, object model). The importer inverts it.
    [[nodiscard]] auto write_node(const Plan_prim& plan_prim) -> lightusd::Prim
    {
        ++m_node_count;
        const erhe::scene::Node& node      = *plan_prim.node;
        const std::string&       prim_name = plan_prim.name;
        const glm::mat4          matrix    = plan_prim.pre_transform * node.parent_from_node_transform().get_matrix();

        // A Mesh CHILD of this node is a prim of its own, planned as such.
        std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(
            const_cast<erhe::scene::Node&>(node).shared_from_this()
        );
        if (mesh && ((mesh->get_flag_bits() & erhe::Item_flags::rendertarget) != 0)) {
            // A rendertarget mesh is a UI quad rendered into every frame; it
            // has no serializable source image, so the node exports without it.
            mesh.reset();
        }
        const std::shared_ptr<erhe::scene::Camera> camera = std::dynamic_pointer_cast<erhe::scene::Camera>(
            const_cast<erhe::scene::Node&>(node).shared_from_this()
        );
        const std::shared_ptr<erhe::scene::Light> light = std::dynamic_pointer_cast<erhe::scene::Light>(
            const_cast<erhe::scene::Node&>(node).shared_from_this()
        );

        const erhe::Item_base* override_root = plan_prim.override_root;
        return
            mesh   ? write_mesh_prim  (node, *mesh.get(),   prim_name, matrix, override_root) :
            camera ? write_camera_prim(node, *camera.get(), prim_name, matrix, override_root) :
            light  ? write_light_prim (node, *light.get(),  prim_name, matrix, override_root) :
                     write_xform_prim (node,                prim_name, matrix, override_root);
    }

    // The prim's transform (doc/usd-compatibility-plan.md M8): the ops it was
    // authored with when it carries a stack and the transform being written
    // is the stack's own composition, else the composed matrix as one
    // `xformOp:transform`. The composition test is what keeps a stack out of
    // the two cases where the matrix is not the prim's own: an import_root
    // container whose transform plan_children pre-multiplies into the prim,
    // and a stage the importer applied its upAxis / metersPerUnit correction
    // to. A prim erhe created carries no stack and writes the single matrix
    // op, so nothing about an editor-authored file changes.
    static void set_transform(std::vector<lightusd::XformOp>& xform_ops, const erhe::scene::Node& node, const glm::mat4& matrix)
    {
        const erhe::scene::Xform_op_stack* stack = node.get_xform_op_stack();
        if ((stack != nullptr) && is_near(glm::mat4{stack->compose()}, matrix)) {
            write_xform_op_stack(xform_ops, *stack);
            return;
        }
        if (is_identity(matrix)) {
            return;
        }
        lightusd::XformOp op;
        op.op_type = lightusd::XformOp::OpType::Transform;
        op.set_value(to_usd(matrix));
        xform_ops.push_back(op);
    }

    static void write_xform_op_stack(std::vector<lightusd::XformOp>& xform_ops, const erhe::scene::Xform_op_stack& stack)
    {
        if (stack.reset_xform_stack) {
            // `!resetXformStack!` is the first token of xformOpOrder and has
            // no value of its own, which is exactly how LightUSD carries it.
            lightusd::XformOp reset_op;
            reset_op.op_type = lightusd::XformOp::OpType::ResetXformStack;
            xform_ops.push_back(reset_op);
        }
        for (const erhe::scene::Xform_op& op : stack.ops) {
            lightusd::XformOp usd_op;
            usd_op.op_type  = to_usd_xform_op_type(op.type);
            usd_op.inverted = op.inverted;
            usd_op.suffix   = op.suffix;
            set_xform_op_value(usd_op, op);
            xform_ops.push_back(usd_op);
        }
    }

    [[nodiscard]] auto write_xform_prim(
        const erhe::scene::Node& node,
        const std::string&       prim_name,
        const glm::mat4&         matrix,
        const erhe::Item_base*   override_root
    ) -> lightusd::Prim
    {
        lightusd::Xform xform;
        xform.name = prim_name;
        set_transform(xform.xformOps, node, matrix);
        write_visibility_and_purpose(node, xform);
        write_erhe_properties(node, xform);
        write_instance_root_override(node, override_root, xform);
        return lightusd::Prim{xform};
    }

    // The overrides of the clone of an arc's target prim
    // (doc/usd-compatibility-plan.md X2). The clone and the referencing prim
    // are one prim in USD - X1 gives the instance one level more than USD's
    // own composition - so the clone's local values are authored on the
    // carrier prim, below the carrier's own: an attribute both author is the
    // carrier's, and saying so is the only notice the user gets.
    template <typename T>
    void write_instance_root_override(const erhe::Item_base& carrier, const erhe::Item_base* override_root, T& typed_prim)
    {
        if (override_root == nullptr) {
            return;
        }
        if (is_override_available(carrier, *override_root, erhe::Item_base::visible_property.get())) {
            typed_prim.visibility.set_value(
                override_root->get_value(erhe::Item_base::visible_property)
                    ? lightusd::Visibility::Inherited
                    : lightusd::Visibility::Invisible
            );
        }
        if (is_override_available(carrier, *override_root, erhe::Item_base::purpose_property.get())) {
            typed_prim.purpose.set_value(to_usd_purpose(override_root->get_value(erhe::Item_base::purpose_property)));
        }
        if (is_override_available(carrier, *override_root, erhe::Item_base::active_property.get())) {
            typed_prim.meta.set_active(override_root->get_value(erhe::Item_base::active_property));
        }
        // A name an `erhe:` attribute of the carrier already carries is
        // reported and dropped by write_erhe_properties itself.
        write_erhe_properties(*override_root, typed_prim);
    }

    // True when the target clone's local value of `property` is what the
    // carrier prim carries: the carrier's own local value wins, and the
    // dropped one is reported.
    [[nodiscard]] auto is_override_available(
        const erhe::Item_base&                     carrier,
        const erhe::Item_base&                     override_root,
        const erhe::property::Dependency_property& property
    ) -> bool
    {
        if (!is_local(override_root, property)) {
            return false;
        }
        if (is_local(carrier, property)) {
            add_warning(
                fmt::format(
                    "prim '{}': '{}' is authored by the referencing prim and by the reference target itself - the referencing prim's value is kept",
                    carrier.get_name(),
                    property.get_name()
                )
            );
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------
    // Meshes
    // -------------------------------------------------------------------

    // The polygons of every primitive of one erhe mesh, concatenated into
    // the arrays of one USD Mesh prim. The primvars are faceVarying: one
    // value per polygon corner is the domain erhe's corner attributes live
    // in (doc/usd_compatibility.md, element table).
    class Mesh_accumulator final
    {
    public:
        std::vector<lightusd::value::point3f>    points;
        std::vector<int32_t>                     face_vertex_counts;
        std::vector<int32_t>                     face_vertex_indices;
        std::vector<lightusd::value::normal3f>   normals;
        std::vector<lightusd::value::texcoord2f> texcoords;
        std::vector<lightusd::value::color3f>    colors;
        std::vector<float>                       opacities;
        bool                                     has_normals  {false};
        bool                                     has_texcoords{false};
        bool                                     has_colors   {false};
    };

    static void append_geometry(Mesh_accumulator& out, const erhe::geometry::Geometry& geometry)
    {
        const GEO::Mesh&                       mesh       = geometry.get_mesh();
        const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        const std::size_t                      base       = out.points.size();

        for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
            const GEO::vec3f point = erhe::geometry::get_pointf(mesh.vertices, vertex);
            out.points.push_back(lightusd::value::point3f{point.x, point.y, point.z});
        }
        for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
            const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
            out.face_vertex_counts.push_back(static_cast<int32_t>(corner_count));
            for (GEO::index_t local = 0; local < corner_count; ++local) {
                const GEO::index_t corner = mesh.facets.corner(facet, local);
                out.face_vertex_indices.push_back(static_cast<int32_t>(base + mesh.facets.vertex(facet, local)));

                const std::optional<GEO::vec3f> normal = attributes.corner_normal.try_get(corner);
                out.normals.push_back(
                    normal.has_value()
                        ? lightusd::value::normal3f{normal.value().x, normal.value().y, normal.value().z}
                        : lightusd::value::normal3f{0.0f, 0.0f, 0.0f}
                );
                out.has_normals = out.has_normals || normal.has_value();

                const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
                out.texcoords.push_back(
                    texcoord.has_value()
                        ? lightusd::value::texcoord2f{texcoord.value().x, texcoord.value().y}
                        : lightusd::value::texcoord2f{0.0f, 0.0f}
                );
                out.has_texcoords = out.has_texcoords || texcoord.has_value();

                const std::optional<GEO::vec4f> color = attributes.corner_color_0.try_get(corner);
                out.colors.push_back(
                    color.has_value()
                        ? lightusd::value::color3f{color.value().x, color.value().y, color.value().z}
                        : lightusd::value::color3f{1.0f, 1.0f, 1.0f}
                );
                out.opacities.push_back(color.has_value() ? color.value().w : 1.0f);
                out.has_colors = out.has_colors || color.has_value();
            }
        }
    }

    // One vertex attribute of a triangle soup, read through the vertex
    // format and converted to four floats whatever its storage encoding is.
    [[nodiscard]] static auto read_soup_attribute(
        const erhe::primitive::Triangle_soup&     soup,
        const erhe::dataformat::Vertex_attribute* attribute,
        const std::size_t                         stream_stride,
        const std::size_t                         vertex_index,
        glm::vec4&                                out_value
    ) -> bool
    {
        if (attribute == nullptr) {
            return false;
        }
        const std::size_t offset = (vertex_index * stream_stride) + attribute->offset;
        const std::size_t size   = erhe::dataformat::get_format_size_bytes(attribute->format);
        if ((offset + size) > soup.vertex_data.size()) {
            return false;
        }
        out_value = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
        erhe::dataformat::convert(
            soup.vertex_data.data() + offset, attribute->format,
            &out_value.x, erhe::dataformat::Format::format_32_vec4_float,
            1.0f
        );
        return true;
    }

    void append_triangle_soup(Mesh_accumulator& out, const erhe::primitive::Triangle_soup& soup)
    {
        using erhe::dataformat::Vertex_attribute;
        using erhe::dataformat::Vertex_attribute_usage;

        if (soup.vertex_format.streams.empty()) {
            add_warning("a triangle soup has no vertex stream - the primitive is not written");
            return;
        }
        const erhe::dataformat::Vertex_stream& stream   = soup.vertex_format.streams.front();
        const Vertex_attribute*                position = stream.find_attribute(Vertex_attribute_usage::position);
        if (position == nullptr) {
            add_warning("a triangle soup has no position attribute - the primitive is not written");
            return;
        }
        const Vertex_attribute* normal   = stream.find_attribute(Vertex_attribute_usage::normal);
        const Vertex_attribute* texcoord = stream.find_attribute(Vertex_attribute_usage::tex_coord);
        const Vertex_attribute* color    = stream.find_attribute(Vertex_attribute_usage::color);

        const std::size_t base         = out.points.size();
        const std::size_t stride       = stream.stride;
        const std::size_t vertex_count = (stride > 0) ? (soup.vertex_data.size() / stride) : 0;
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            glm::vec4 value{0.0f, 0.0f, 0.0f, 1.0f};
            const bool has_position = read_soup_attribute(soup, position, stride, vertex_index, value);
            out.points.push_back(
                has_position
                    ? lightusd::value::point3f{value.x, value.y, value.z}
                    : lightusd::value::point3f{0.0f, 0.0f, 0.0f}
            );
        }
        for (std::size_t triangle = 0; ((triangle * 3) + 2) < soup.index_data.size(); ++triangle) {
            out.face_vertex_counts.push_back(3);
            for (std::size_t local = 0; local < 3; ++local) {
                const std::size_t vertex_index = soup.index_data[(triangle * 3) + local];
                out.face_vertex_indices.push_back(static_cast<int32_t>(base + vertex_index));

                glm::vec4  value{0.0f, 0.0f, 0.0f, 1.0f};
                const bool has_normal = read_soup_attribute(soup, normal, stride, vertex_index, value);
                out.normals.push_back(
                    has_normal
                        ? lightusd::value::normal3f{value.x, value.y, value.z}
                        : lightusd::value::normal3f{0.0f, 0.0f, 0.0f}
                );
                out.has_normals = out.has_normals || has_normal;

                value = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
                const bool has_texcoord = read_soup_attribute(soup, texcoord, stride, vertex_index, value);
                out.texcoords.push_back(
                    has_texcoord
                        ? lightusd::value::texcoord2f{value.x, value.y}
                        : lightusd::value::texcoord2f{0.0f, 0.0f}
                );
                out.has_texcoords = out.has_texcoords || has_texcoord;

                value = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
                const bool has_color = read_soup_attribute(soup, color, stride, vertex_index, value);
                out.colors.push_back(lightusd::value::color3f{value.x, value.y, value.z});
                out.opacities.push_back(value.w);
                out.has_colors = out.has_colors || has_color;
            }
        }
    }

    // The GeomSubset prim name a primitive gets. The importer names a
    // primitive's geometry `<mesh name>.<subset name>`, so that prefix is
    // dropped again here - otherwise every round trip would prepend the mesh
    // name once more.
    [[nodiscard]] static auto subset_name_of(const std::string& geometry_name, const std::string& prim_name) -> std::string
    {
        const std::string prefix = prim_name + ".";
        if (geometry_name.compare(0, prefix.size(), prefix) == 0) {
            return geometry_name.substr(prefix.size());
        }
        return geometry_name;
    }

    // One primitive of an erhe mesh: the facets it contributed to the shared
    // arrays, and the material bound to them.
    class Facet_group final
    {
    public:
        std::string                      name;
        int32_t                          first_facet{0};
        int32_t                          facet_count{0};
        const erhe::primitive::Material* material   {nullptr};
    };

    [[nodiscard]] auto write_mesh_prim(
        const erhe::scene::Node& node,
        const erhe::scene::Mesh& mesh,
        const std::string&       prim_name,
        const glm::mat4&         matrix,
        const erhe::Item_base*   override_root
    ) -> lightusd::Prim
    {
        ++m_mesh_count;
        lightusd::GeomMesh geom_mesh;
        geom_mesh.name = prim_name;
        set_transform(geom_mesh.xformOps, node, matrix);
        // The authored polygons are the mesh, not a subdivision cage: this is
        // what makes the importer build erhe geometry rather than a triangle
        // soup (doc/usd_compatibility.md, geometry attributes).
        geom_mesh.subdivisionScheme.set_value(lightusd::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone);

        Mesh_accumulator         accumulator;
        std::vector<Facet_group> groups;
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
            const erhe::primitive::Primitive_render_shape* render_shape =
                mesh_primitive.primitive ? mesh_primitive.primitive->render_shape.get() : nullptr;
            if (render_shape == nullptr) {
                continue;
            }
            const int32_t                                    first_facet = static_cast<int32_t>(accumulator.face_vertex_counts.size());
            const std::shared_ptr<erhe::geometry::Geometry>& geometry    = render_shape->get_geometry_const();
            if (geometry) {
                append_geometry(accumulator, *geometry.get());
            } else {
                const std::shared_ptr<erhe::primitive::Triangle_soup>& soup = render_shape->get_triangle_soup();
                if (!soup) {
                    continue;
                }
                append_triangle_soup(accumulator, *soup.get());
            }
            const int32_t facet_count = static_cast<int32_t>(accumulator.face_vertex_counts.size()) - first_facet;
            if (facet_count == 0) {
                continue;
            }
            Facet_group group{};
            group.name        = geometry ? subset_name_of(geometry->get_name(), prim_name) : fmt::format("{}_{}", prim_name, groups.size());
            group.first_facet = first_facet;
            group.facet_count = facet_count;
            group.material    = mesh_primitive.material.get();
            groups.push_back(std::move(group));
        }

        geom_mesh.points.set_value(accumulator.points);
        geom_mesh.faceVertexCounts.set_value(accumulator.face_vertex_counts);
        geom_mesh.faceVertexIndices.set_value(accumulator.face_vertex_indices);
        if (accumulator.has_normals) {
            geom_mesh.normals.set_value(accumulator.normals);
            geom_mesh.normals.metas().set_interpolation_enum(lightusd::Interpolation::FaceVarying);
        }
        if (accumulator.has_texcoords) {
            add_primvar(geom_mesh, "primvars:st", accumulator.texcoords);
        }
        if (accumulator.has_colors) {
            add_primvar(geom_mesh, "primvars:displayColor", accumulator.colors);
            add_primvar(geom_mesh, "primvars:displayOpacity", accumulator.opacities);
        }

        write_visibility_and_purpose(node, geom_mesh);
        write_erhe_properties(node, geom_mesh);
        write_erhe_properties(mesh, geom_mesh);
        write_instance_root_override(node, override_root, geom_mesh);

        // One primitive needs no subset: the importer's remainder group is
        // the whole mesh and reproduces it. Several primitives become one
        // materialBind GeomSubset each.
        if (groups.size() == 1) {
            bind_material(geom_mesh, groups.front().material);
        }
        lightusd::Prim prim{geom_mesh};
        if (groups.size() > 1) {
            Name_scope subset_names;
            for (const Facet_group& group : groups) {
                lightusd::GeomSubset subset;
                subset.name = subset_names.make_unique(group.name);
                subset.elementType.set_value(lightusd::GeomSubset::ElementType::Face);
                subset.familyName.set_value(lightusd::value::token{"materialBind"});
                std::vector<int32_t> indices;
                indices.reserve(static_cast<std::size_t>(group.facet_count));
                for (int32_t facet = 0; facet < group.facet_count; ++facet) {
                    indices.push_back(group.first_facet + facet);
                }
                subset.indices.set_value(std::move(indices));
                bind_material(subset, group.material);
                std::string error;
                if (!prim.add_child(lightusd::Prim{subset}, false, &error)) {
                    add_warning(fmt::format("subset '{}' of '{}' could not be added: {}", subset.name, prim_name, error));
                }
            }
        }
        return prim;
    }

    template <typename T, typename V>
    static void add_primvar(T& typed_prim, const std::string& name, const std::vector<V>& values)
    {
        lightusd::Attribute attribute;
        attribute.set_value(values);
        attribute.metas().set_interpolation_enum(lightusd::Interpolation::FaceVarying);
        typed_prim.props.emplace(name, lightusd::Property{attribute, false});
    }

    // -------------------------------------------------------------------
    // Cameras and lights
    // -------------------------------------------------------------------

    [[nodiscard]] auto write_camera_prim(
        const erhe::scene::Node&   node,
        const erhe::scene::Camera& camera,
        const std::string&         prim_name,
        const glm::mat4&           matrix,
        const erhe::Item_base*     override_root
    ) -> lightusd::Prim
    {
        using erhe::scene::Camera;

        lightusd::GeomCamera geom_camera;
        geom_camera.name = prim_name;
        set_transform(geom_camera.xformOps, node, matrix);

        if (is_local(camera, Camera::z_near_property.get()) || is_local(camera, Camera::z_far_property.get())) {
            geom_camera.clippingRange.set_value(
                lightusd::value::float2{
                    camera.get_value(Camera::z_near_property),
                    camera.get_value(Camera::z_far_property)
                }
            );
        }
        const erhe::scene::Projection::Type projection_type = camera.get_value(Camera::projection_type_property);
        if (projection_type == erhe::scene::Projection::Type::orthogonal) {
            geom_camera.projection.set_value(lightusd::GeomCamera::Projection::Orthographic);
            // A USD aperture is in tenths of a scene unit.
            if (is_local(camera, Camera::ortho_width_property.get())) {
                geom_camera.horizontalAperture.set_value(camera.get_value(Camera::ortho_width_property) * 10.0f);
            }
            if (is_local(camera, Camera::ortho_height_property.get())) {
                geom_camera.verticalAperture.set_value(camera.get_value(Camera::ortho_height_property) * 10.0f);
            }
        } else if (is_local(camera, Camera::fov_x_property.get()) || is_local(camera, Camera::fov_y_property.get())) {
            geom_camera.focalLength.set_value(c_export_focal_length);
            geom_camera.horizontalAperture.set_value(
                2.0f * c_export_focal_length * std::tan(0.5f * camera.get_value(Camera::fov_x_property))
            );
            geom_camera.verticalAperture.set_value(
                2.0f * c_export_focal_length * std::tan(0.5f * camera.get_value(Camera::fov_y_property))
            );
        }
        if (is_local(camera, Camera::exposure_property.get())) {
            geom_camera.exposure.set_value(camera.get_value(Camera::exposure_property));
        }
        if (is_local(camera, Camera::infinite_z_far_property.get()) && camera.get_value(Camera::infinite_z_far_property)) {
            add_warning(
                fmt::format(
                    "camera '{}' has an infinite far plane, which USD cannot express - clippingRange carries the finite range",
                    camera.get_name()
                )
            );
        }

        write_visibility_and_purpose(node, geom_camera);
        write_erhe_properties(node, geom_camera);
        write_erhe_properties(camera, geom_camera);
        write_instance_root_override(node, override_root, geom_camera);
        return lightusd::Prim{geom_camera};
    }

    // The UsdLux inputs erhe fills, shared by every light type.
    template <typename T>
    static void write_light_api(const erhe::scene::Light& light, T& usd_light)
    {
        using erhe::scene::Light;

        if (is_local(light, Light::color_property.get())) {
            const glm::vec3 color = light.get_value(Light::color_property);
            usd_light.color.set_value(lightusd::value::color3f{color.x, color.y, color.z});
        }
        if (is_local(light, Light::intensity_property.get())) {
            // erhe has no exposure on a light, so the whole quantity is the
            // intensity and `inputs:exposure` stays at its zero fallback -
            // which is what the importer folds back in.
            usd_light.intensity.set_value(light.get_value(Light::intensity_property));
        }
        if (is_local(light, Light::temperature_property.get())) {
            usd_light.enableColorTemperature.set_value(true);
            usd_light.colorTemperature.set_value(light.get_value(Light::temperature_property));
        }
        if (is_local(light, Light::cast_shadow_property.get())) {
            usd_light.shadowEnable.set_value(light.get_value(Light::cast_shadow_property));
        }
    }

    [[nodiscard]] auto write_light_prim(
        const erhe::scene::Node&  node,
        const erhe::scene::Light& light,
        const std::string&        prim_name,
        const glm::mat4&          matrix,
        const erhe::Item_base*    override_root
    ) -> lightusd::Prim
    {
        using erhe::scene::Light;
        using erhe::scene::Light_type;

        const Light_type light_type = light.get_value(Light::light_type_property);
        if (light_type == Light_type::directional) {
            lightusd::DistantLight distant_light;
            distant_light.name = prim_name;
            set_transform(distant_light.xformOps, node, matrix);
            write_light_api(light, distant_light);
            write_visibility_and_purpose(node, distant_light);
            write_erhe_properties(node, distant_light);
            write_erhe_properties(light, distant_light);
            write_instance_root_override(node, override_root, distant_light);
            return lightusd::Prim{distant_light};
        }

        lightusd::SphereLight sphere_light;
        sphere_light.name = prim_name;
        set_transform(sphere_light.xformOps, node, matrix);
        // A point light is a sphere light of no extent.
        sphere_light.radius.set_value(0.0f);
        write_light_api(light, sphere_light);
        if (light_type == Light_type::spot) {
            const float outer = light.get_value(Light::outer_spot_angle_property);
            const float inner = light.get_value(Light::inner_spot_angle_property);
            sphere_light.shapingConeAngle.set_value(glm::degrees(outer));
            sphere_light.shapingConeSoftness.set_value((outer > 0.0f) ? std::max(0.0f, 1.0f - (inner / outer)) : 0.0f);
        }
        write_visibility_and_purpose(node, sphere_light);
        write_erhe_properties(node, sphere_light);
        write_erhe_properties(light, sphere_light);
        write_instance_root_override(node, override_root, sphere_light);

        lightusd::Prim prim{sphere_light};
        if (light_type == Light_type::spot) {
            // The applied ShapingAPI, not the cone attributes, is what makes
            // an imported sphere light a spot light (doc/usd_compatibility.md,
            // lights).
            apply_api_schema(prim, lightusd::APISchemas::APIName::ShapingAPI, "ShapingAPI");
        }
        return prim;
    }

    static void apply_api_schema(lightusd::Prim& prim, const lightusd::APISchemas::APIName name, const std::string& instance_name)
    {
        lightusd::APISchemas& schemas = prim.metas().get_apiSchemas_mutable();
        schemas.names.emplace_back(name, instance_name);
    }

    // -------------------------------------------------------------------
    // Tags
    // -------------------------------------------------------------------

    // Item tags become UsdCollectionAPI collections on the default prim
    // (doc/usd_compatibility.md, object model): one collection per tag whose
    // `includes` names every prim carrying it, at the path the first pass
    // gave the prim.
    void record_tags(const erhe::Item_base& item, const std::string& path)
    {
        const std::set<std::string>& tags = item.get_tags();
        if (tags.empty()) {
            return;
        }
        for (const std::string& tag : tags) {
            m_tag_members[sanitize_usd_identifier(tag)].push_back(path);
        }
    }

    // The collections go on the typed value the prim holds, which is where
    // UsdCollectionAPI's instances live; every prim type the writer can put
    // at the top level is asked in turn.
    void add_collections(lightusd::Prim& prim)
    {
        if (m_tag_members.empty()) {
            return;
        }
        const bool added =
            add_collections_to<lightusd::Xform       >(prim) ||
            add_collections_to<lightusd::GeomMesh    >(prim) ||
            add_collections_to<lightusd::GeomCamera  >(prim) ||
            add_collections_to<lightusd::SphereLight >(prim) ||
            add_collections_to<lightusd::DistantLight>(prim);
        if (!added) {
            add_warning("the default prim cannot hold collections - the item tags are not written");
            return;
        }
        for (const std::pair<const std::string, std::vector<std::string>>& entry : m_tag_members) {
            apply_api_schema(prim, lightusd::APISchemas::APIName::CollectionAPI, entry.first);
        }
    }

    template <typename T>
    [[nodiscard]] auto add_collections_to(lightusd::Prim& prim) -> bool
    {
        T* typed = prim.get_data().as<T>();
        if (typed == nullptr) {
            return false;
        }
        for (const std::pair<const std::string, std::vector<std::string>>& entry : m_tag_members) {
            std::vector<lightusd::Path> paths;
            paths.reserve(entry.second.size());
            for (const std::string& path : entry.second) {
                paths.emplace_back(path, "");
            }
            lightusd::Relationship relationship;
            relationship.set(std::move(paths));
            typed->get_or_add_instance(entry.first).includes = relationship;
        }
        return true;
    }

    const Usd_save_arguments& m_arguments;
    Usd_save_result&          m_result;

    std::map<const erhe::primitive::Material*, std::size_t>  m_material_indices;
    std::map<const erhe::primitive::Material*, std::string>  m_material_paths;
    // Where each style item's `class` prim ended up, so a prim with a style
    // names it by path.
    std::map<const erhe::Item_base*, std::string>          m_style_paths;
    std::map<Texture_shader_key, std::string>                m_texture_shader_names;
    std::set<const erhe::primitive::Material*>               m_uv_reader_materials;
    std::map<std::string, std::vector<std::string>>          m_tag_members;
    std::map<const erhe::Item_base*, const std::vector<Usd_save_reference>*> m_prim_references;
    std::size_t                                              m_node_count{0};
    std::size_t                                              m_mesh_count{0};
};

} // anonymous namespace

auto sanitize_usd_identifier(const std::string_view name) -> std::string
{
    std::string result;
    result.reserve(name.size() + 1);
    for (const char character : name) {
        const bool is_identifier_character =
            ((character >= 'A') && (character <= 'Z')) ||
            ((character >= 'a') && (character <= 'z')) ||
            ((character >= '0') && (character <= '9')) ||
            (character == '_');
        result.push_back(is_identifier_character ? character : '_');
    }
    if (result.empty()) {
        return std::string{"_"};
    }
    if ((result.front() >= '0') && (result.front() <= '9')) {
        result.insert(result.begin(), '_');
    }
    return result;
}

auto save_usda(const Usd_save_arguments& arguments) -> Usd_save_result
{
    ERHE_PROFILE_FUNCTION();

    Usd_save_result result{};
    Exporter        exporter{arguments, result};
    exporter.write();
    return result;
}

} // namespace erhe::usd
