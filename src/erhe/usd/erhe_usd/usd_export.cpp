#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
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
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/instance_override.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/point_instancer.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform_op.hpp"

// LightUSD headers. Together with usd.cpp and usd_import.cpp this is the
// only place in erhe that includes them; everything the rest of erhe sees is
// declared in usd.hpp.
#include "lightusd.hh"
#include "core/composition-types.hh"
#include "core/prim.hh"
#include "core/variant-types.hh"
#include "core/model-scope.hh"
#include "core/prim-metas.hh"
#include "stage.hh"
#include "usda-writer.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "usdLux.hh"
#include "usdSkel.hh"
#include "pprint-enum.hh"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// The `SkelAnimation` prim name a skeleton's joint channels are written
// under (doc/usd-compatibility-plan.md K1). USD carries no name of its own
// for it - Tydra hands the importer the channels, not the prim - so the
// writer fixes one, which is what keeps a second save byte-identical.
constexpr std::string_view c_skel_animation_prim_name{"anim"};

// The prim type a skeleton's joint channels are authored under.
constexpr std::string_view c_skel_animation_prim_type_name{"SkelAnimation"};

// How many joint influences erhe carries per vertex: two attribute sets of
// four (joint index, weight) pairs, the shape the importer fills them in.
constexpr std::size_t c_joints_per_set{4};
constexpr std::size_t c_joint_sets    {2};
constexpr std::size_t c_joint_slots   {c_joints_per_set * c_joint_sets};

// Whether a mesh accumulator collects the joint influences of the points it
// appends: only a skinned mesh writes them, and a brush's geometry child
// never does.
enum class Skin_influences {
    skip,
    collect
};

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

// Hand `value` - one value of `op`, its own or one of its time samples - to
// `sink` in the USD value type the op is authored as, so that the op's value
// and every one of its samples travel through one type dispatch.
template <typename Sink>
void visit_xform_op_value(const erhe::scene::Xform_op& op, const erhe::scene::Xform_op_value& value, Sink&& sink)
{
    using Precision = erhe::scene::Xform_op_precision;
    if (op.type == erhe::scene::Xform_op_type::transform) {
        // matrix4d is the only matrix type erhe keeps a value in, and the op
        // holds it in double precision, so it is written without narrowing.
        const glm::dmat4          matrix_value = std::get<glm::dmat4>(value);
        lightusd::value::matrix4d matrix{};
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                matrix.m[j][i] = matrix_value[j][i];
            }
        }
        sink(matrix);
        return;
    }
    if (op.type == erhe::scene::Xform_op_type::orient) {
        const glm::dquat quaternion = std::get<glm::dquat>(value);
        switch (op.precision) {
            case Precision::double_: {
                sink(lightusd::value::quatd{{quaternion.x, quaternion.y, quaternion.z}, quaternion.w});
                return;
            }
            case Precision::float_: {
                sink(
                    lightusd::value::quatf{
                        {static_cast<float>(quaternion.x), static_cast<float>(quaternion.y), static_cast<float>(quaternion.z)},
                        static_cast<float>(quaternion.w)
                    }
                );
                return;
            }
            default: {
                sink(
                    lightusd::value::quath{
                        {to_usd_half(quaternion.x), to_usd_half(quaternion.y), to_usd_half(quaternion.z)},
                        to_usd_half(quaternion.w)
                    }
                );
                return;
            }
        }
    }
    if (is_three_component_op(op.type)) {
        const glm::dvec3 vector = std::get<glm::dvec3>(value);
        switch (op.precision) {
            case Precision::double_: {
                sink(lightusd::value::double3{vector.x, vector.y, vector.z});
                return;
            }
            case Precision::float_: {
                sink(
                    lightusd::value::float3{
                        static_cast<float>(vector.x),
                        static_cast<float>(vector.y),
                        static_cast<float>(vector.z)
                    }
                );
                return;
            }
            default: {
                sink(lightusd::value::half3{to_usd_half(vector.x), to_usd_half(vector.y), to_usd_half(vector.z)});
                return;
            }
        }
    }
    // A single-axis rotate: one angle in degrees.
    const double angle = std::get<double>(value);
    switch (op.precision) {
        case Precision::double_: sink(angle);                        return;
        case Precision::float_:  sink(static_cast<float>(angle));    return;
        default:                 sink(to_usd_half(angle));           return;
    }
}

// The op's value in the value type its authored precision names, which is
// what the writer prints the type name of: a `float3` op comes back a
// `float3` op. erhe keeps every value in double precision, so a `half` op
// round-trips through the nearest half - the same value it was read from.
// The op's value and, when it carries any, its time samples
// (src/erhe/usd/notes.md, "Time samples"). A sampled op is written with both:
// the samples are what a viewer plays, and the value is the pose at the
// stage's start time, which is what a reader that evaluates nothing sees.
void set_xform_op_value(lightusd::XformOp& usd_op, const erhe::scene::Xform_op& op)
{
    visit_xform_op_value(op, op.value, [&usd_op](const auto& usd_value) { usd_op.set_value(usd_value); });
    if (op.samples.empty()) {
        return;
    }
    lightusd::value::TimeSamples samples;
    samples.reserve(op.samples.size());
    for (const erhe::scene::Xform_op_sample& sample : op.samples) {
        visit_xform_op_value(
            op, sample.value,
            [&samples, &sample](const auto& usd_value) {
                std::string error;
                static_cast<void>(samples.add_sample(sample.time_code, usd_value, &error));
            }
        );
    }
    usd_op.set_timesamples(std::move(samples));
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
// What the clone of an arc's target contributes to the carrier prim it is
// written as (doc/usd-compatibility-plan.md X2): the item whose local values
// are authored on the carrier - the clone and the carrier are one prim in USD
// - and the material its mesh binds when the mesh is one group of facets,
// which the carrier binds as its own.
class Instance_root_override final
{
public:
    const erhe::Item_base*           item    {nullptr};
    const erhe::primitive::Material* material{nullptr};
};

class Plan_prim;

// Which variant of which set one prim of the tree belongs to.
class Variant_prim_membership final
{
public:
    const Usd_save_variant_set* set{nullptr};
    std::string                 variant_name;
    std::string                 authored_name;
};

// One prim of the tree that belongs to one variant of one of its parent's
// variant sets (doc/usd-compatibility-plan.md X4). It is planned like every
// other prim, and written
// inside its variant block under the name the file authored rather than among
// the plain children of the prim carrying the set.
class Plan_variant_prim final
{
public:
    const Usd_save_variant_set* set{nullptr};
    std::string                 variant_name;
    std::unique_ptr<Plan_prim>  prim;
};

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
    Instance_root_override                 override_root;
    std::vector<erhe::scene::Instance_override_item> overrides;
    // The bindings the clone of an arc's target holds for its groups of
    // facets: one `over` prim below the carrier per group, by the name of the
    // GeomSubset prim the group is written as.
    std::vector<std::pair<std::string, const erhe::primitive::Material*>> root_subset_bindings;
    // The variant sets the caller named for this prim
    // (doc/usd-compatibility-plan.md X4). A set adds variant blocks to the
    // prim and changes nothing else about it, so a prim carrying one is
    // written and walked the way it otherwise would be.
    std::vector<const Usd_save_variant_set*> variant_sets;
    // The prims a variant of one of `variant_sets` adds. They are not among
    // `children`: each is written inside its own variant block.
    std::vector<Plan_variant_prim>   variant_prims;
    std::vector<Plan_prim>           children;
};

// What pass one of the write decides: the prims with their paths, the name
// the stage's `defaultPrim` takes, and whether the top-level prims were
// gathered under the `World` prim that plays the erhe root's part.
class Prim_plan final
{
public:
    std::vector<Plan_prim> prims;
    std::string            default_prim_name;
    bool                   wrapped{false};
    // False when there was nothing to plan (no root node); the error is on
    // the result the exporter was given.
    bool                   planned{false};
};

// One skeleton the writer authors (doc/usd-compatibility-plan.md K1): the
// prim that is the pivot of at least one skin, that skin's joints in
// `joints` order, and the bind transform each joint is written with. The
// first skin registered for a skeleton is the one the bind pose comes from.
class Skeleton_record final
{
public:
    const erhe::scene::Node*              node{nullptr};
    std::vector<const erhe::scene::Node*> joints;
    std::vector<glm::mat4>                bind_transforms;
    // The name of the `SkelAnimation` prim the joint channels are written
    // under: the one the file authored when it had one, so a save keeps the
    // file's own spelling.
    std::string                           animation_prim_name;
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
// doc/usd_compatibility.md), and the USD spelling each of them gets. Every
// other local value travels as an `erhe:` custom attribute, so a property is
// listed here exactly once: writing both forms would author one value twice.
// `Native_property_form` (usd.hpp) says which of the two a prim offers;
// `visible`, `purpose` and `active` are carried natively either way, being
// prim metadata and plain tokens every prim has. Every schema attribute below
// is one the writer fills further down this file, and get_usd_authored_as()
// hands the same list to the Properties window's origin line (X5).
[[nodiscard]] auto native_usd_property_name(
    const std::string_view     owner,
    const std::string_view     name,
    const Native_property_form form
) -> std::string_view
{
    if (owner == "Item_base") {
        if (name == "visible") { return "visibility"; }
        if (name == "purpose") { return "purpose"; }
        if (name == "active" ) { return "active (prim metadata)"; }
        return {};
    }
    if (form == Native_property_form::custom_attributes) {
        return {};
    }
    if (owner == "Material") {
        // The values go on the `UsdPreviewSurface` shader prim below the
        // material prim, which is named "surface" here.
        if (name == "base_color")                 { return "surface.inputs:diffuseColor"; }
        if (name == "emissive")                   { return "surface.inputs:emissiveColor"; }
        if (name == "metallic")                   { return "surface.inputs:metallic"; }
        if (name == "roughness")                  { return "surface.inputs:roughness"; }
        if (name == "opacity")                    { return "surface.inputs:opacity"; }
        if (name == "ior")                        { return "surface.inputs:ior"; }
        if (name == "occlusion_texture_strength") { return "surface.inputs:occlusion"; }
        if (name == "blending_mode")              { return "surface.inputs:opacityThreshold"; }
        if (name == "alpha_cutoff")               { return "surface.inputs:opacityThreshold"; }
        if (name == "base_color_texture")         { return "surface.inputs:diffuseColor.connect"; }
        if (name == "metallic_roughness_texture") { return "surface.inputs:metallic.connect"; }
        if (name == "normal_texture")             { return "surface.inputs:normal.connect"; }
        if (name == "occlusion_texture")          { return "surface.inputs:occlusion.connect"; }
        if (name == "emissive_texture")           { return "surface.inputs:emissiveColor.connect"; }
        // The decode of the normal slot's texture and the wrap modes of
        // every slot's are the UsdUVTexture's own inputs, written for a
        // bound texture whatever the erhe value is: USD's fallbacks are not
        // erhe's, so a value left unwritten would not read back.
        if (name == "normal_texture_decode_scale") { return "normal_texture.inputs:scale"; }
        if (name == "normal_texture_decode_bias")  { return "normal_texture.inputs:bias"; }
        if (name == "base_color_texture_wrap_u")         { return "base_color_texture.inputs:wrapS"; }
        if (name == "base_color_texture_wrap_v")         { return "base_color_texture.inputs:wrapT"; }
        if (name == "metallic_roughness_texture_wrap_u") { return "metallic_roughness_texture.inputs:wrapS"; }
        if (name == "metallic_roughness_texture_wrap_v") { return "metallic_roughness_texture.inputs:wrapT"; }
        if (name == "normal_texture_wrap_u")             { return "normal_texture.inputs:wrapS"; }
        if (name == "normal_texture_wrap_v")             { return "normal_texture.inputs:wrapT"; }
        if (name == "occlusion_texture_wrap_u")          { return "occlusion_texture.inputs:wrapS"; }
        if (name == "occlusion_texture_wrap_v")          { return "occlusion_texture.inputs:wrapT"; }
        if (name == "emissive_texture_wrap_u")           { return "emissive_texture.inputs:wrapS"; }
        if (name == "emissive_texture_wrap_v")           { return "emissive_texture.inputs:wrapT"; }
        return {};
    }
    if (owner == "Gprim") {
        if (name == "double_sided") { return "doubleSided"; }
        return {};
    }
    if (owner == "Brush") {
        // The material a placed instance gets is the brush prim's own
        // material binding (doc/usd-compatibility-plan.md E4a).
        if (name == "material") { return "material:binding"; }
        return {};
    }
    if (owner == "Camera") {
        if (name == "projection_type") { return "projection"; }
        if (name == "fov_x")           { return "horizontalAperture"; }
        if (name == "fov_y")           { return "verticalAperture"; }
        if (name == "ortho_left")      { return "horizontalAperture"; }
        if (name == "ortho_width")     { return "horizontalAperture"; }
        if (name == "ortho_bottom")    { return "verticalAperture"; }
        if (name == "ortho_height")    { return "verticalAperture"; }
        if (name == "z_near")          { return "clippingRange"; }
        if (name == "z_far")           { return "clippingRange"; }
        if (name == "infinite_z_far")  { return "clippingRange"; }
        if (name == "exposure")        { return "exposure"; }
        return {};
    }
    if (owner == "Light") {
        if (name == "light_type")       { return "the prim type (DistantLight / SphereLight)"; }
        if (name == "color")            { return "inputs:color"; }
        if (name == "intensity")        { return "inputs:intensity"; }
        if (name == "temperature")      { return "inputs:colorTemperature"; }
        if (name == "inner_spot_angle") { return "inputs:shaping:coneSoftness"; }
        if (name == "outer_spot_angle") { return "inputs:shaping:coneAngle"; }
        if (name == "cast_shadow")      { return "inputs:shadow:enable"; }
        return {};
    }
    return {};
}

[[nodiscard]] auto is_native_usd_property(
    const std::string_view     owner,
    const std::string_view     name,
    const Native_property_form form
) -> bool
{
    return !native_usd_property_name(owner, name, form).empty();
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

// A node parameter travels as a (USD type, literal text) pair
// (doc/usd-texture-graphs-plan.md 2.2), so the writer parses the text back
// into the value the type names. `text` is USD's own spelling, quotes of a
// string or a token included.
[[nodiscard]] auto strip_usd_quotes(const std::string& text) -> std::string
{
    if ((text.size() >= 2) && (text.front() == '"') && (text.back() == '"')) {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

// The components of a USD tuple literal `(a, b, c)`, zero-filled to `count`.
[[nodiscard]] auto parse_usd_float_tuple(const std::string& text, const std::size_t count) -> std::vector<float>
{
    std::vector<float> values(count, 0.0f);
    std::size_t        index = 0;
    std::size_t        begin = text.find_first_of("([");
    begin = (begin == std::string::npos) ? 0 : (begin + 1);
    while ((index < count) && (begin < text.size())) {
        const char* const first = text.c_str() + begin;
        char*             last  = nullptr;
        const float       value = std::strtof(first, &last);
        if (last == first) {
            break;
        }
        values[index] = value;
        ++index;
        const std::size_t separator = text.find(',', static_cast<std::size_t>(last - text.c_str()));
        if (separator == std::string::npos) {
            break;
        }
        begin = separator + 1;
    }
    return values;
}

class Exporter final
{
public:
    Exporter(const Usd_save_arguments& arguments, Usd_save_result& result)
        : m_arguments{arguments}
        , m_result   {result}
    {
    }

    // Pass one alone: where every prim of the tree lands on the stage. A
    // caller that has to know a prim's path before the file exists - a scene
    // block entry naming a prim - plans first, fills what it fills, and hands
    // the same arguments to the write, which plans the same way again and so
    // lands the same paths.
    void plan(Prim_plan& out_plan)
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

        for (const Usd_save_variant_set& entry : m_arguments.variant_sets) {
            if (!entry.item || entry.variants.empty()) {
                continue;
            }
            m_prim_variant_sets[entry.item.get()].push_back(&entry);
            for (const Usd_save_variant& variant : entry.variants) {
                for (const Usd_save_variant_prim& prim : variant.prims) {
                    if (prim.item) {
                        m_variant_prims[prim.item.get()] = Variant_prim_membership{
                            .set           = &entry,
                            .variant_name  = variant.name,
                            .authored_name = prim.authored_name
                        };
                    }
                }
            }
        }

        for (const Usd_save_brush& entry : m_arguments.brushes) {
            if (entry.item) {
                m_brushes[entry.item.get()] = &entry;
            }
        }
        for (const Usd_save_node_graph& entry : m_arguments.node_graphs) {
            if (entry.item) {
                m_node_graphs[entry.item.get()] = &entry;
            }
        }
        for (const Usd_save_point_instancer& entry : m_arguments.point_instancers) {
            if (!entry.item) {
                continue;
            }
            m_point_instancers[entry.item.get()] = &entry;
            for (const Usd_save_point_instance& instance : entry.instances) {
                if (instance.item) {
                    m_point_instancer_instances.insert(instance.item.get());
                }
            }
        }

        // The skeletons the tree's skinned meshes name, before anything is
        // planned: a joint is an entry of its skeleton's `joints` rather than
        // a prim, so the plan has to know which prims are joints
        // (doc/usd-compatibility-plan.md K1).
        collect_skeletons(*m_arguments.root_node.get());
        collect_skeleton_tokens(*m_arguments.root_node.get());

        // Pass one: where every prim lands on the stage. A prim's path is
        // only known once its ancestors have their sanitized, sibling-unique
        // names and the wrapper decision below is made, and a mesh binds its
        // material by that path, so the whole tree is planned before
        // anything is written.
        std::vector<Plan_prim>& plan = out_plan.prims;
        Name_scope              top_level_names;
        plan_children(*m_arguments.root_node.get(), glm::mat4{1.0f}, top_level_names, plan);

        // Several top-level prims are gathered under one Xform that plays the
        // erhe root's part, so the stage still names one defaultPrim. A
        // top-level `Scope` - a content-library kind scope or folder, which
        // sits beside the scene's own prims (doc/usd-compatibility-plan.md
        // E4d) - is a namespace a reference has no use for and does not force
        // the wrapper: a scene with one prim of its own beside such scopes
        // names that prim as the defaultPrim and writes the scopes beside it,
        // so saving a scene that was read from a file adds no level to it.
        const Plan_prim* const single_prim = find_single_non_scope_prim(plan);
        out_plan.wrapped           = (plan.size() != 1) && (single_prim == nullptr);
        out_plan.default_prim_name = out_plan.wrapped
            ? std::string{c_world_prim_name}
            : ((single_prim != nullptr) ? single_prim->name : plan.front().name);
        assign_paths(plan, out_plan.wrapped ? fmt::format("/{}", c_world_prim_name) : std::string{});
        record_resource_paths(plan);
        out_plan.planned = true;
    }

    // The one top-level prim that is not a `Scope`, null when the plan holds
    // no such prim or more than one of them. It is what the stage names as its
    // defaultPrim when no wrapper is written.
    [[nodiscard]] static auto find_single_non_scope_prim(const std::vector<Plan_prim>& plan) -> const Plan_prim*
    {
        const Plan_prim* found = nullptr;
        for (const Plan_prim& prim : plan) {
            if (erhe::is<erhe::Scope>(prim.item)) {
                continue;
            }
            if (found != nullptr) {
                return nullptr;
            }
            found = &prim;
        }
        return found;
    }

    // The planned path of every prim the tree holds, by the item it is: what
    // `plan_usd_prim_paths` hands the caller.
    void collect_planned_paths(std::map<const erhe::Item_base*, std::string>& out_paths)
    {
        Prim_plan prim_plan;
        plan(prim_plan);
        collect_planned_paths_of(prim_plan.prims, out_paths);
    }

    static void collect_planned_paths_of(
        const std::vector<Plan_prim>&                 prims,
        std::map<const erhe::Item_base*, std::string>& out_paths
    )
    {
        for (const Plan_prim& prim : prims) {
            if (prim.item != nullptr) {
                out_paths[static_cast<const erhe::Item_base*>(prim.item)] = prim.path;
            }
            collect_planned_paths_of(prim.children, out_paths);
            for (const Plan_variant_prim& variant_prim : prim.variant_prims) {
                if (variant_prim.prim) {
                    if (variant_prim.prim->item != nullptr) {
                        out_paths[static_cast<const erhe::Item_base*>(variant_prim.prim->item)] = variant_prim.prim->path;
                    }
                    collect_planned_paths_of(variant_prim.prim->children, out_paths);
                }
            }
        }
    }

    void write()
    {
        ERHE_PROFILE_FUNCTION();

        Prim_plan prim_plan;
        plan(prim_plan);
        if (!prim_plan.planned) {
            return;
        }
        std::vector<Plan_prim>& plan              = prim_plan.prims;
        const bool              wrap              = prim_plan.wrapped;
        const std::string&      default_prim_name = prim_plan.default_prim_name;

        // Pass two: the prims themselves.
        lightusd::Stage             stage;
        std::vector<lightusd::Prim> content_prims;
        write_plan_prims(plan, content_prims);

        if (!wrap) {
            // Every planned prim is a top-level prim of the stage - one prim
            // per plan entry, in the plan's order - and the one the plan named
            // as the defaultPrim carries the collections.
            for (std::size_t i = 0, end = content_prims.size(); i < end; ++i) {
                if (plan[i].name == default_prim_name) {
                    add_collections(content_prims[i]);
                }
                add_root_prim(stage, std::move(content_prims[i]));
            }
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

        // The domes a load recorded, as top-level `DomeLight` prims beside
        // the content root. A dome lights the whole stage and carries no
        // place in the content tree (erhe holds it as ambient light), so the
        // stage root is where it goes; `defaultPrim` still names the content.
        std::vector<std::string> root_prim_names;
        if (wrap) {
            root_prim_names.push_back(std::string{c_world_prim_name});
        } else {
            root_prim_names.reserve(plan.size());
            for (const Plan_prim& prim : plan) {
                root_prim_names.push_back(prim.name);
            }
        }
        write_dome_lights(stage, root_prim_names);

        stage.metas().defaultPrim = lightusd::value::token{default_prim_name};
        stage.metas().upAxis.set_value(
            (m_arguments.up_axis == "Z") ? lightusd::Axis::Z :
            (m_arguments.up_axis == "X") ? lightusd::Axis::X : lightusd::Axis::Y
        );
        stage.metas().metersPerUnit.set_value(m_arguments.meters_per_unit);
        // The time coordinates only mean something once the layer holds time
        // samples, so they are authored exactly when one was written, and the
        // range is the one the samples span.
        if (m_wrote_time_samples) {
            stage.metas().timeCodesPerSecond.set_value(
                (m_arguments.time_codes_per_second > 0.0) ? m_arguments.time_codes_per_second : 24.0
            );
            stage.metas().startTimeCode.set_value(m_first_time_code);
            stage.metas().endTimeCode.set_value(m_last_time_code);
        }
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
    // The time-code range of the xformOp time samples written so far, and
    // whether any were: what the layer's startTimeCode / endTimeCode say.
    double m_first_time_code   {0.0};
    double m_last_time_code    {0.0};
    bool   m_wrote_time_samples{false};

    void add_warning(const std::string& text)
    {
        if (!m_result.warning.empty()) {
            m_result.warning += "; ";
        }
        m_result.warning += text;
        log_usd->warn("USD '{}': {}", m_arguments.path.generic_string(), text);
    }

    // One `DomeLight` root prim per recorded dome (Usd_save_arguments::
    // dome_lights). erhe holds a dome as the scene's ambient light, so the
    // values written are the ones the load read back out of the prim.
    void write_dome_lights(lightusd::Stage& stage, const std::vector<std::string>& root_prim_names)
    {
        if (m_arguments.dome_lights.empty()) {
            return;
        }
        // The names the stage's own top-level prims took, so a dome takes a
        // name of its own beside them.
        Name_scope names;
        for (const std::string& name : root_prim_names) {
            static_cast<void>(names.make_unique(name));
        }
        for (const Usd_dome_light& dome : m_arguments.dome_lights) {
            lightusd::DomeLight dome_light;
            dome_light.name = names.make_unique(dome.name.empty() ? std::string{"DomeLight"} : dome.name);
            dome_light.color.set_value(lightusd::value::color3f{dome.color.x, dome.color.y, dome.color.z});
            dome_light.intensity.set_value(dome.intensity);
            dome_light.exposure.set_value(dome.exposure);
            if (!dome.texture_file.empty()) {
                dome_light.file.set_value(lightusd::value::AssetPath{dome.texture_file});
            }
            add_root_prim(stage, lightusd::Prim{dome_light});
        }
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

    // `doubleSided` of a geometry prim (doc/usd_compatibility.md, geometry
    // attributes). Written when the value is local, the authored-only rule
    // every native attribute follows (M4).
    template <typename T>
    void write_double_sided(const erhe::scene::Gprim& gprim, T& typed_prim)
    {
        if (is_local(gprim, erhe::scene::Gprim::double_sided_property.get())) {
            typed_prim.doubleSided.set_value(gprim.get_double_sided());
        }
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

    // The prim's specifier from the item's `defined` value
    // (doc/usd-compatibility-plan.md X2): an undefined prim - one the stage
    // it was read from composed as `over` - is spelled `over` again, which is
    // what makes the round trip a fixed point and keeps it and its subtree
    // out of USD's default traversal. The specifier is the carrier, so no
    // `erhe:` custom attribute is written for it. A `class` prim is a style
    // (X3) and an `over` prim is already one, so only a `def` is lowered.
    void apply_defined_specifier(const erhe::Item_base& item, lightusd::Prim& prim)
    {
        if (item.get_value(erhe::Item_base::defined_property)) {
            return;
        }
        // The USDA writer prints the specifier the typed struct inside the
        // prim carries, so that is the field to lower; Prim's own specifier
        // is set to match so readers of either agree.
        const bool lowered =
            lower_specifier_of<lightusd::Xform              >(prim) ||
            lower_specifier_of<lightusd::Scope              >(prim) ||
            lower_specifier_of<lightusd::Model              >(prim) ||
            lower_specifier_of<lightusd::GeomMesh           >(prim) ||
            lower_specifier_of<lightusd::GeomSubset         >(prim) ||
            lower_specifier_of<lightusd::GeomCamera         >(prim) ||
            lower_specifier_of<lightusd::GeomPointInstancer >(prim) ||
            lower_specifier_of<lightusd::SphereLight        >(prim) ||
            lower_specifier_of<lightusd::DistantLight       >(prim) ||
            lower_specifier_of<lightusd::DomeLight          >(prim) ||
            lower_specifier_of<lightusd::Skeleton           >(prim) ||
            lower_specifier_of<lightusd::SkelAnimation      >(prim) ||
            lower_specifier_of<lightusd::NodeGraph          >(prim) ||
            lower_specifier_of<lightusd::Material           >(prim) ||
            lower_specifier_of<lightusd::Shader             >(prim);
        if (!lowered) {
            log_usd->error(
                "USD prim '{}' is not defined, but its prim class is not one apply_defined_specifier knows - it is written as `def`",
                item.get_name()
            );
        }
    }

    // Lowers one typed prim struct's specifier to `over`. Only a `def` is
    // lowered: an `over` is already one and a `class` prim is a style (X3).
    // Reports whether the prim holds a value of this class, so the caller can
    // tell "not this class" from "already lowered".
    template <typename T>
    [[nodiscard]] static auto lower_specifier_of(lightusd::Prim& prim) -> bool
    {
        T* typed = prim.get_data().as<T>();
        if (typed == nullptr) {
            return false;
        }
        if (typed->spec == lightusd::Specifier::Def) {
            typed->spec       = lightusd::Specifier::Over;
            prim.specifier()  = lightusd::Specifier::Over;
        }
        return true;
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

    // erhe's address modes onto UsdUVTexture's wrap tokens. erhe has no
    // border color, so no erhe value spells `black`.
    [[nodiscard]] static auto to_usd_wrap(const erhe::graphics::Sampler_address_mode mode) -> lightusd::UsdUVTexture::Wrap
    {
        switch (mode) {
            case erhe::graphics::Sampler_address_mode::repeat:          return lightusd::UsdUVTexture::Wrap::Repeat;
            case erhe::graphics::Sampler_address_mode::clamp_to_edge:   return lightusd::UsdUVTexture::Wrap::Clamp;
            case erhe::graphics::Sampler_address_mode::mirrored_repeat: return lightusd::UsdUVTexture::Wrap::Mirror;
            default:                                                    return lightusd::UsdUVTexture::Wrap::Clamp;
        }
    }

    [[nodiscard]] static auto slot_of(
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot
    ) -> const erhe::primitive::Material_texture_sampler&
    {
        const erhe::primitive::Material_texture_samplers& slots = material.data.texture_samplers;
        switch (slot) {
            case Usd_material_texture_slot::base_color:         return slots.base_color;
            case Usd_material_texture_slot::metallic_roughness: return slots.metallic_roughness;
            case Usd_material_texture_slot::normal:             return slots.normal;
            case Usd_material_texture_slot::occlusion:          return slots.occlusion;
            case Usd_material_texture_slot::emissive:           return slots.emissive;
            default:                                            return slots.base_color;
        }
    }

    // The inputs:scale one slot's UsdUVTexture is written with: the erhe
    // factor of the surface input the slot feeds, on the channel that input
    // reads. UsdPreviewSurface reads metallic from the blue channel and
    // roughness from the green one of the one metallic-roughness image, so
    // that slot carries both factors.
    [[nodiscard]] static auto texel_scale_of(
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot
    ) -> glm::vec4
    {
        using erhe::primitive::Material;
        switch (slot) {
            case Usd_material_texture_slot::base_color: {
                const glm::vec3 base_color = material.get_value(Material::base_color_property);
                return glm::vec4{base_color.x, base_color.y, base_color.z, 1.0f};
            }
            case Usd_material_texture_slot::emissive: {
                const glm::vec3 emissive = material.get_value(Material::emissive_property);
                return glm::vec4{emissive.x, emissive.y, emissive.z, 1.0f};
            }
            case Usd_material_texture_slot::metallic_roughness: {
                // The two factors sit on the channels the surface reads them
                // through, which the material names (Material::
                // roughness_channel_property / metallic_channel_property).
                // An input that names Texture_channel::none reads no channel
                // of this image - it is not connected below - so its factor
                // stays on the surface input and off the texture.
                glm::vec4 scale{1.0f, 1.0f, 1.0f, 1.0f};
                const erhe::primitive::Texture_channel roughness_channel = material.get_roughness_channel();
                const erhe::primitive::Texture_channel metallic_channel  = material.get_metallic_channel();
                if (roughness_channel != erhe::primitive::Texture_channel::none) {
                    scale[static_cast<glm::length_t>(erhe::primitive::to_uint32(roughness_channel))] =
                        material.get_value(Material::roughness_property).x;
                }
                if (metallic_channel != erhe::primitive::Texture_channel::none) {
                    scale[static_cast<glm::length_t>(erhe::primitive::to_uint32(metallic_channel))] =
                        material.get_value(Material::metallic_property);
                }
                return scale;
            }
            case Usd_material_texture_slot::normal: {
                return material.get_value(Material::normal_texture_decode_scale_property);
            }
            default: {
                return glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
            }
        }
    }

    // The `outputs:<channel>` name a scalar UsdPreviewSurface input connects
    // to: the channel the erhe material reads that input from.
    [[nodiscard]] static auto channel_output_name(const erhe::primitive::Texture_channel channel) -> const char*
    {
        switch (channel) {
            case erhe::primitive::Texture_channel::r: return "outputs:r";
            case erhe::primitive::Texture_channel::g: return "outputs:g";
            case erhe::primitive::Texture_channel::b: return "outputs:b";
            case erhe::primitive::Texture_channel::a: return "outputs:a";
            default:                                  return "outputs:r";
        }
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
        const erhe::primitive::Material_texture_sampler& slot_state = slot_of(material, slot);
        uv_texture.wrapS.set_value(to_usd_wrap(slot_state.sampler.wrap_u));
        uv_texture.wrapT.set_value(to_usd_wrap(slot_state.sampler.wrap_v));
        // A connected UsdPreviewSurface input has no factor of its own: the
        // erhe factor - which the shader multiplies the texel with - is
        // written as the texture's own inputs:scale, on the channels the
        // surface reads that slot through. The normal slot's scale and bias
        // are the texel decode instead.
        const glm::vec4 texel_scale = texel_scale_of(material, slot);
        uv_texture.scale.set_value(lightusd::value::float4{texel_scale.x, texel_scale.y, texel_scale.z, texel_scale.w});
        if (slot == Usd_material_texture_slot::normal) {
            const glm::vec4 decode_bias = material.get_value(erhe::primitive::Material::normal_texture_decode_bias_property);
            uv_texture.bias.set_value(lightusd::value::float4{decode_bias.x, decode_bias.y, decode_bias.z, decode_bias.w});
        }
        uv_texture.sourceColorSpace.set_value(
            texture->srgb
                ? lightusd::UsdUVTexture::SourceColorSpace::SRGB
                : lightusd::UsdUVTexture::SourceColorSpace::Raw
        );
        // The slot's UV transform, back in USD's `st` space. An erhe
        // identity maps onto the USD identity, so a slot at its defaults
        // reads the primvar reader directly and no UsdTransform2d prim is
        // written (src/erhe/usd/notes.md, "Texture coordinates").
        const Erhe_uv_transform erhe_transform{
            .rotation = slot_state.rotation,
            .scale    = slot_state.scale,
            .offset   = slot_state.offset
        };
        const std::string transform_name = add_uv_transform(material_prim, material_path, texture_shader_name(slot), erhe_transform);
        uv_texture.st.set_connection(
            transform_name.empty()
                ? lightusd::Path{material_path + "/uv_reader", "outputs:result"}
                : lightusd::Path{material_path + "/" + transform_name, "outputs:result"}
        );
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

    // The `UsdTransform2d` prim one slot's texture reads its UVs through, or
    // an empty name when the slot's transform is the identity. The prim sits
    // between the material's primvar reader and the slot's UsdUVTexture.
    [[nodiscard]] auto add_uv_transform(
        lightusd::Prim&          material_prim,
        const std::string&       material_path,
        const char*              slot_shader_name,
        const Erhe_uv_transform& erhe_transform
    ) -> std::string
    {
        const Usd_uv_transform_2d usd_transform = to_usd_uv_transform_2d(erhe_transform);
        const bool is_identity =
            (usd_transform.rotation_degrees == 0.0f)                  &&
            (usd_transform.scale            == glm::vec2{1.0f, 1.0f}) &&
            (usd_transform.translation      == glm::vec2{0.0f, 0.0f});
        if (is_identity) {
            return {};
        }
        lightusd::UsdTransform2d transform2d;
        transform2d.rotation.set_value(usd_transform.rotation_degrees);
        transform2d.scale.set_value(lightusd::value::float2{usd_transform.scale.x, usd_transform.scale.y});
        transform2d.translation.set_value(lightusd::value::float2{usd_transform.translation.x, usd_transform.translation.y});
        transform2d.in.set_connection(lightusd::Path{material_path + "/uv_reader", "outputs:result"});
        transform2d.result.set_authored(true);

        lightusd::Shader shader;
        shader.name    = std::string{slot_shader_name} + "_transform";
        shader.info_id = "UsdTransform2d";
        shader.value   = std::move(transform2d);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("uv transform shader '{}' could not be added: {}", shader.name, error));
            return {};
        }
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

        // `diffuseColor` is the one input whose UsdPreviewSurface fallback
        // (c_usd_diffuse_color_fallback) is not the erhe default, so leaving
        // an erhe default unauthored would mean 0.18 grey to every other
        // reader. It is written from the effective value whenever that
        // differs from the fallback - which is also what makes a save
        // reproduce itself, since the importer writes the fallback back as a
        // local value. Every other input is written only where it is local
        // (D32), the two fallbacks agreeing.
        {
            const glm::vec3 base_color = material.get_value(Material::base_color_property);
            if (base_color != c_usd_diffuse_color_fallback) {
                surface.diffuseColor.set_value(lightusd::value::color3f{base_color.x, base_color.y, base_color.z});
            }
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
        connect_texture(
            material_prim, material_path, material, Usd_material_texture_slot::occlusion,
            channel_output_name(material.get_occlusion_channel()), surface.occlusion
        );
        // erhe has one metallic-roughness slot; UsdPreviewSurface reads the
        // two scalars through separate inputs of the one texture, each on the
        // channel the material names (the glTF packing by default). An input
        // that names Texture_channel::none reads no channel of the image and
        // keeps the plain value written above.
        if (material.get_roughness_channel() != erhe::primitive::Texture_channel::none) {
            connect_texture(
                material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness,
                channel_output_name(material.get_roughness_channel()), surface.roughness
            );
        }
        if (material.get_metallic_channel() != erhe::primitive::Texture_channel::none) {
            connect_texture(
                material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness,
                channel_output_name(material.get_metallic_channel()), surface.metallic
            );
        }
        // erhe's fragment alpha is a channel of the base color texture. The
        // opacity input is connected only where the material names a channel
        // of its own: alpha is what a reader assumes anyway, and connecting
        // it unasked would make every textured material's opacity
        // texture-driven.
        if (is_local(material, Material::opacity_channel_property.get())) {
            connect_texture(
                material_prim, material_path, material, Usd_material_texture_slot::base_color,
                channel_output_name(material.get_opacity_channel()), surface.opacity
            );
        }

        lightusd::Shader shader;
        shader.name    = "surface";
        shader.info_id = "UsdPreviewSurface";
        shader.value   = std::move(surface);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("surface shader of '{}' could not be added: {}", material_path, error));
        }
    }

    // The path of the graph interface output one slot reads, empty when the
    // slot reads no graph (doc/usd-texture-graphs-plan.md R2). The pin is the
    // graph's first interface output: that is the value the graph has, and a
    // graph without one is one warning and no connection.
    [[nodiscard]] auto find_graph_output(
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot,
        lightusd::Path&                  out_path
    ) -> bool
    {
        if (m_arguments.material_graph_bindings.empty()) {
            return false;
        }
        const std::map<const erhe::primitive::Material*, std::size_t>::const_iterator index = m_material_indices.find(&material);
        if (index == m_material_indices.end()) {
            return false;
        }
        for (const Usd_save_material_graph_binding& binding : m_arguments.material_graph_bindings) {
            if ((binding.material_index != index->second) || (binding.slot != slot) || !binding.graph) {
                continue;
            }
            const std::map<const erhe::Item_base*, std::string>::const_iterator path = m_node_graph_paths.find(binding.graph.get());
            const std::map<const erhe::Item_base*, const Usd_save_node_graph*>::const_iterator record =
                m_node_graphs.find(binding.graph.get());
            if ((path == m_node_graph_paths.end()) || (record == m_node_graphs.end())) {
                add_warning(
                    fmt::format(
                        "material '{}' reads a texture graph that is no prim of the stage - the slot is not connected",
                        material.get_name()
                    )
                );
                return false;
            }
            if (record->second->outputs.empty()) {
                add_warning(
                    fmt::format(
                        "material '{}' reads texture graph '{}', which has no interface output - the slot is not connected",
                        material.get_name(), binding.graph->get_name()
                    )
                );
                return false;
            }
            out_path = lightusd::Path{
                path->second,
                std::string{c_node_graph_output_prefix} + record->second->outputs.front().name
            };
            return true;
        }
        return false;
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
        // A slot fed by a texture node graph reads the graph's interface
        // output; no `UsdUVTexture` is written for it (R2).
        lightusd::Path graph_output;
        if (find_graph_output(material, slot, graph_output)) {
            input.set_connection(graph_output);
            input.set_value_empty();
            return;
        }
        const std::string shader_name = get_texture_shader(material_prim, material_path, material, slot);
        if (shader_name.empty()) {
            return;
        }
        input.set_connection(lightusd::Path{material_path + "/" + shader_name, output_name});
        input.set_value_empty();
    }

    // Returns whether a binding was written; the caller then applies the
    // MaterialBindingAPI on the prim (apply_material_binding_api).
    [[nodiscard]] auto bind_material(lightusd::MaterialBinding& binding, const erhe::primitive::Material* material) -> bool
    {
        if (material == nullptr) {
            return false;
        }
        const std::map<const erhe::primitive::Material*, std::string>::const_iterator i = m_material_paths.find(material);
        if (i == m_material_paths.end()) {
            add_warning(
                fmt::format("material '{}' is used but was not offered to the writer - the binding is dropped", material->get_name())
            );
            return false;
        }
        lightusd::Relationship relationship;
        relationship.set(lightusd::Path{i->second, ""});
        binding.set_materialBinding(relationship);
        return true;
    }

    // -------------------------------------------------------------------
    // Nodes
    // -------------------------------------------------------------------

    // Where the children being planned sit: below an ordinary prim of the
    // tree, or below a `class` prim, whose children are the prototypes it
    // holds abstract (doc/usd-compatibility-plan.md X3). A prototype carries
    // no `content` - that is what keeps it out of the render - and is written
    // back as the ordinary `def` prim it is.
    // A point instancer's prototypes are held abstract the same way
    // (doc/usd-compatibility-plan.md S1), so the whole subtree below an
    // instancer is planned whatever the content flag says.
    enum class Prim_holder : unsigned int {
        tree            = 0,
        class_prim      = 1,
        point_instancer = 2
    };

    [[nodiscard]] static auto plans_contentless_prims(const Prim_holder holder) -> bool
    {
        return (holder == Prim_holder::class_prim) || (holder == Prim_holder::point_instancer);
    }

    // Pass one over the children of `parent`. The filter is the glTF
    // exporter's - an import_root container is unwrapped with its transform
    // composed in, a render proxy is derived data rebuilt by its owner, and
    // anything without Item_flags::content is transient editor furniture
    // (tool visuals, controllers, rendertarget UI quads) recreated every
    // session - widened by what a USD file carries without the content flag:
    // a folder scope and a resource prim carry show_in_ui rather than
    // content, so every `Scope`, every material and every prim on the way
    // down to one are planned as well
    // (doc/usd-compatibility-plan.md U4, E4d).
    void plan_children(
        const erhe::Hierarchy&          parent,
        const glm::mat4&                pre_transform,
        Name_scope&                     names,
        std::vector<Plan_prim>&         out_prims,
        std::vector<Plan_variant_prim>* out_variant_prims = nullptr,
        const Prim_holder               holder            = Prim_holder::tree
    )
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            const erhe::Typed* child_prim = dynamic_cast<const erhe::Typed*>(child.get());
            if (child_prim == nullptr) {
                continue;
            }
            const uint64_t           flags      = child_prim->get_flag_bits();
            if ((flags & erhe::Item_flags::session_only) != 0) {
                // Editor session state (the default camera injected for a
                // file that authors none), not stage content: the file
                // carries the cameras it authored or the user created, and
                // the next open injects the default again.
                continue;
            }
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
            if (m_skel_animation_items.count(child_prim) != 0) {
                continue; // rebuilt by the Skeleton prim from the joint channels
            }
            // A skin and an animation are library resources of the editor,
            // not prims of a stage: a skin is the `Skeleton` prim's arrays
            // and the skinned mesh's `SkelBindingAPI` primvars (K1), and an
            // animation is the sampled `xformOp`s of the prims it drives, so
            // both are written by what they drive and the item itself is left
            // out wherever it sits - a folder scope written since E4d holds
            // them the way any other scope holds its resources.
            if (erhe::is<erhe::scene::Skin>(child_prim) || erhe::is<erhe::scene::Animation>(child_prim)) {
                continue;
            }
            if ((child_node != nullptr) && (m_joint_items.count(child_prim) != 0)) {
                // A joint is an entry of its skeleton's `joints` and
                // `restTransforms`, not a prim: the Skeleton prim writes it
                // (doc/usd-compatibility-plan.md K1). A prim parented under a
                // joint is written where the joint sits, with the joint's own
                // local transform composed into it, the way an import_root
                // container's children are.
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
            // An instance of a point instancer is the instancer's expansion,
            // not a prim of its own: the instancer writes it as one entry of
            // `positions` / `orientations` / `scales`
            // (doc/usd-compatibility-plan.md S1).
            if (m_point_instancer_instances.count(child_prim) != 0) {
                continue;
            }
            if (
                ((flags & erhe::Item_flags::content) == 0) &&
                !plans_contentless_prims(holder)           &&
                !is_carried_without_content_flag(*child_prim)
            ) {
                continue;
            }
            // A prim one variant of the parent's sets adds is written
            // inside that block, under the name the file authored: the name
            // it has in the tree is what keeps two variants' prims apart
            // there, and the block is a namespace of its own.
            const Variant_prim_membership* const membership = find_variant_prim_membership(*child_prim, parent);
            Plan_prim plan_prim{};
            plan_prim.item          = child_prim;
            plan_prim.node          = child_node;
            plan_prim.material      = dynamic_cast<const erhe::primitive::Material*>(child.get());
            plan_prim.pre_transform = pre_transform;
            plan_prim.name          = (membership != nullptr)
                ? sanitize_usd_identifier(membership->authored_name)
                : names.make_unique(child_prim->get_name());
            plan_prim.references    = find_prim_references(*child_prim);
            plan_prim.variant_sets  = find_prim_variant_sets(*child_prim);
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
                    plan_prim.children,
                    &plan_prim.variant_prims,
                    child_prim_holder(*child_prim, holder)
                );
            }
            if ((membership != nullptr) && (out_variant_prims != nullptr)) {
                out_variant_prims->push_back(
                    Plan_variant_prim{
                        .set          = membership->set,
                        .variant_name = membership->variant_name,
                        .prim         = std::make_unique<Plan_prim>(std::move(plan_prim))
                    }
                );
                continue;
            }
            out_prims.push_back(std::move(plan_prim));
        }
    }

    // What holds the children of `prim`, given what holds `prim` itself: a
    // style item holds prototypes (X3), a point instancer holds prototypes
    // and instances (S1), and everything below an instancer stays inside it.
    [[nodiscard]] auto child_prim_holder(const erhe::Typed& prim, const Prim_holder holder) const -> Prim_holder
    {
        if (is_style_prim(prim)) {
            return Prim_holder::class_prim;
        }
        if ((holder == Prim_holder::point_instancer) || (m_point_instancers.count(&prim) != 0)) {
            return Prim_holder::point_instancer;
        }
        return Prim_holder::tree;
    }

    // The prototypes one planned instancer holds, in tree order: a planned
    // prim below it that carries no content is one, and the walk stops
    // there. That is exactly what the load makes of `rel prototypes` - the
    // prototype subtrees are the abstract ones - and `proto_indices` indexes
    // this order, which is why the load remaps the authored array onto it.
    static void collect_prototype_paths(const Plan_prim& plan_prim, std::vector<std::string>& out_paths)
    {
        for (const Plan_prim& child : plan_prim.children) {
            if ((child.item != nullptr) && ((child.item->get_flag_bits() & erhe::Item_flags::content) == 0)) {
                out_paths.push_back(child.path);
                continue;
            }
            collect_prototype_paths(child, out_paths);
        }
    }

    // The variant of `parent`'s sets `prim` belongs to, null when it belongs
    // to none: a prim listed by a set some other prim carries is a prim of
    // the tree like any other here.
    [[nodiscard]] auto find_variant_prim_membership(
        const erhe::Typed&     prim,
        const erhe::Hierarchy& parent
    ) const -> const Variant_prim_membership*
    {
        const std::map<const erhe::Item_base*, Variant_prim_membership>::const_iterator i = m_variant_prims.find(&prim);
        if (i == m_variant_prims.end()) {
            return nullptr;
        }
        return (i->second.set->item.get() == &parent) ? &i->second : nullptr;
    }

    // The arcs the caller named for this prim, null when it named none.
    [[nodiscard]] auto find_prim_references(const erhe::Typed& prim) const -> const std::vector<Usd_save_reference>*
    {
        const std::map<const erhe::Item_base*, const std::vector<Usd_save_reference>*>::const_iterator i =
            m_prim_references.find(&prim);
        return (i != m_prim_references.end()) ? i->second : nullptr;
    }

    // The variant sets the caller named for this prim, empty when it named
    // none (doc/usd-compatibility-plan.md X4).
    [[nodiscard]] auto find_prim_variant_sets(const erhe::Typed& prim) const -> std::vector<const Usd_save_variant_set*>
    {
        const std::map<const erhe::Item_base*, std::vector<const Usd_save_variant_set*>>::const_iterator i =
            m_prim_variant_sets.find(&prim);
        return (i != m_prim_variant_sets.end()) ? i->second : std::vector<const Usd_save_variant_set*>{};
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
                plan_prim.override_root.item = entry.item;
                if (entry.transform_overridden) {
                    add_warning(
                        fmt::format(
                            "prim '{}': the transform of an arc's target is the referencing prim's own transform - the override is not written",
                            plan_prim.name
                        )
                    );
                }
                plan_root_bindings(entry, plan_prim);
                continue;
            }
            plan_prim.overrides.push_back(std::move(entry));
        }
    }

    // The material bindings the clone of an arc's target holds. The clone and
    // the carrier are one prim in USD, so a mesh of one group of facets binds
    // on the carrier itself and a mesh of several binds on the `over` prim of
    // each group.
    void plan_root_bindings(const erhe::scene::Instance_override_item& entry, Plan_prim& plan_prim)
    {
        if (entry.materials.empty()) {
            return;
        }
        const erhe::scene::Mesh* mesh = dynamic_cast<const erhe::scene::Mesh*>(entry.item);
        if (mesh == nullptr) {
            add_warning(
                fmt::format("prim '{}': an arc's target binds a material and is no mesh - the binding is not written", plan_prim.name)
            );
            return;
        }
        for (const erhe::scene::Instance_override_material& material : entry.materials) {
            if (mesh->get_primitives().size() == 1) {
                plan_prim.override_root.material = material.material.get();
                continue;
            }
            const std::string subset_name = override_subset_name(*mesh, material.primitive_index);
            if (subset_name.empty()) {
                add_warning(
                    fmt::format("prim '{}': an arc's target binds a group of facets that has no name - the binding is not written", plan_prim.name)
                );
                continue;
            }
            plan_prim.root_subset_bindings.emplace_back(subset_name, material.material.get());
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

    // A brush item (doc/usd-compatibility-plan.md E4a), which is a
    // `Brush`-typed prim on the stage. As with a style, erhe::usd depends on
    // no editor type, so the kind is recognized by the token erhe::Typed
    // fixes for it; what the brush holds comes from Usd_save_arguments.
    [[nodiscard]] static auto is_brush_prim(const erhe::Typed& prim) -> bool
    {
        return prim.get_class_type_name() == c_brush_prim_type_name;
    }

    // A node graph item (doc/usd-texture-graphs-plan.md), which is a
    // marked `NodeGraph` prim on the stage. erhe::usd names no editor type
    // (R6) and a graph carries no class token of its own the way a brush
    // does, so what makes an item a graph prim is being named in
    // Usd_save_arguments::node_graphs: the record is both the recognition and
    // everything the prim holds.
    [[nodiscard]] auto is_node_graph_prim(const erhe::Typed& prim) const -> bool
    {
        return m_node_graphs.find(&prim) != m_node_graphs.end();
    }

    // A prim the file carries although it carries no Item_flags::content,
    // which is what widens the content filter. Three kinds answer true: every
    // `Scope` - a content-library folder, a kind scope, an authored USD
    // `Scope` - which is written where it sits whatever it holds, so a folder
    // tree survives a save (doc/usd-compatibility-plan.md E4d); a resource
    // prim the file carries, which today is a material, a style, a brush or a
    // node graph; and a prim on the way down to one of those.
    [[nodiscard]] auto is_carried_without_content_flag(const erhe::Typed& prim) const -> bool
    {
        if (
            erhe::is<erhe::Scope>(&prim)               ||
            erhe::is<erhe::primitive::Material>(&prim) ||
            is_style_prim(prim)                        ||
            is_brush_prim(prim)                        ||
            is_node_graph_prim(prim)
        ) {
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
            if (is_carried_without_content_flag(*child_prim)) {
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
            // A prim inside a variant block has the path it would have as a
            // child of the prim carrying the set, which is the path it has
            // while its variant is selected.
            for (Plan_variant_prim& variant_prim : prim.variant_prims) {
                variant_prim.prim->path = prim.path + "/" + variant_prim.prim->name;
                assign_paths(variant_prim.prim->children, variant_prim.prim->path);
            }
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
            if ((prim.item != nullptr) && is_node_graph_prim(*prim.item)) {
                m_node_graph_paths[prim.item] = prim.path;
            }
            if ((prim.item != nullptr) && (m_skeletons.count(prim.item) != 0)) {
                m_skeleton_paths[prim.item] = prim.path;
            }
            if ((prim.references != nullptr) && (prim.item != nullptr)) {
                record_instance_content_material_paths(*prim.item, prim.path);
            }
            record_resource_paths(prim.children);
            for (const Plan_variant_prim& variant_prim : prim.variant_prims) {
                record_resource_paths_of(*variant_prim.prim.get());
            }
        }
    }

    void record_resource_paths_of(const Plan_prim& prim)
    {
        if (prim.material != nullptr) {
            m_material_paths[prim.material] = prim.path;
        }
        if ((prim.item != nullptr) && is_style_prim(*prim.item)) {
            m_style_paths[prim.item] = prim.path;
        }
        if ((prim.item != nullptr) && is_node_graph_prim(*prim.item)) {
            m_node_graph_paths[prim.item] = prim.path;
        }
        if ((prim.item != nullptr) && (m_skeletons.count(prim.item) != 0)) {
            m_skeleton_paths[prim.item] = prim.path;
        }
        record_resource_paths(prim.children);
        for (const Plan_variant_prim& variant_prim : prim.variant_prims) {
            record_resource_paths_of(*variant_prim.prim.get());
        }
    }

    // The materials an instance's content supplies, by the path they have in
    // the composed stage: the carrier prim's path plus the path the item has
    // below the arc's target clone, the clone itself standing at the
    // carrier's own path (X1's one extra level, collapsed the way the writer
    // collapses it). The content of an instance is not written, so this is
    // what lets an override that rebinds one of its materials name it.
    void record_instance_content_material_paths(const erhe::Item_base& carrier_item, const std::string& carrier_path)
    {
        const erhe::Hierarchy* carrier = dynamic_cast<const erhe::Hierarchy*>(&carrier_item);
        if (carrier == nullptr) {
            return;
        }
        for (const std::shared_ptr<erhe::Hierarchy>& clone : carrier->get_children()) {
            if (clone) {
                record_content_material_paths(*clone.get(), carrier_path);
            }
        }
    }

    void record_content_material_paths(const erhe::Hierarchy& item, const std::string& path)
    {
        const erhe::primitive::Material* material = dynamic_cast<const erhe::primitive::Material*>(&item);
        if ((material != nullptr) && (m_material_paths.find(material) == m_material_paths.end())) {
            m_material_paths[material] = path;
        }
        for (const std::shared_ptr<erhe::Hierarchy>& child : item.get_children()) {
            if (child) {
                record_content_material_paths(*child.get(), path + "/" + child->get_name());
            }
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

        apply_defined_specifier(*plan_prim.item, prim);

        if (plan_prim.references != nullptr) {
            write_references(prim, *plan_prim.references);
        }
        write_inherits(prim, *plan_prim.item);
        write_variant_sets(prim, plan_prim);

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
        std::string                      name;
        const erhe::Item_base*           item                {nullptr};
        bool                             transform_overridden{false};
        // The material the `over` binds, null when it binds none. A binding
        // that covers one group of facets is written on the GeomSubset prim
        // of the group, which is a child `over` of the mesh's.
        const erhe::primitive::Material* material            {nullptr};
        std::vector<Override_prim>       children;
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
        if (plan_prim.overrides.empty() && plan_prim.root_subset_bindings.empty()) {
            return;
        }
        std::vector<Override_prim> tree;
        for (const std::pair<std::string, const erhe::primitive::Material*>& binding : plan_prim.root_subset_bindings) {
            find_or_add_override_prim(tree, binding.first).material = binding.second;
        }
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
                plan_override_bindings(entry, *prim);
            }
        }
        for (const Override_prim& prim : tree) {
            out_prims.push_back(write_override_prim(prim));
        }
    }

    // The material bindings of one override item, on the `over` prims that
    // carry them: a mesh with one primitive binds on its own `over`, and a
    // mesh whose groups of facets bind separately binds on a child `over`
    // named after the group - the GeomSubset prim write_geometry_mesh_prim
    // gives the group.
    void plan_override_bindings(const erhe::scene::Instance_override_item& entry, Override_prim& prim)
    {
        if (entry.materials.empty()) {
            return;
        }
        const erhe::scene::Mesh* mesh = dynamic_cast<const erhe::scene::Mesh*>(entry.item);
        if (mesh == nullptr) {
            add_warning(
                fmt::format("the override of '{}' binds a material to an item that is no mesh - the binding is not written", prim.name)
            );
            return;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        for (const erhe::scene::Instance_override_material& material : entry.materials) {
            if (primitives.size() == 1) {
                prim.material = material.material.get();
                continue;
            }
            const std::string subset_name = override_subset_name(*mesh, material.primitive_index);
            if (subset_name.empty()) {
                add_warning(
                    fmt::format(
                        "the override of '{}' binds a group of facets that has no name - the binding is not written",
                        prim.name
                    )
                );
                continue;
            }
            Override_prim& subset_prim = find_or_add_override_prim(prim.children, subset_name);
            subset_prim.material = material.material.get();
        }
    }

    // The name of the GeomSubset prim one primitive of a mesh is written as.
    // The mesh's own name is what prefixes the primitive's geometry name, so
    // that is what is dropped again here - the prim name the writer gives the
    // mesh can have been made unique and would not match.
    [[nodiscard]] static auto override_subset_name(
        const erhe::scene::Mesh& mesh,
        const std::size_t        primitive_index
    ) -> std::string
    {
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
        if (primitive_index >= primitives.size()) {
            return std::string{};
        }
        const erhe::primitive::Primitive* primitive = primitives[primitive_index].primitive.get();
        if (primitive == nullptr) {
            return std::string{};
        }
        const erhe::primitive::Primitive_render_shape* render_shape = primitive->render_shape.get();
        if (render_shape == nullptr) {
            return std::string{};
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = render_shape->get_geometry_const();
        if (!geometry) {
            return std::string{};
        }
        return subset_name_of(geometry->get_name(), mesh.get_name());
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
        const bool override_bound = (override_prim.material != nullptr) && add_material_binding(model.props, *override_prim.material);
        lightusd::Prim prim{model};
        if (override_bound) {
            apply_material_binding_api(prim);
        }
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
        write_xform_op_props(ops, props);
    }

    // The same, for a transform that travels as a value rather than on an
    // item: the authored stack when there is one, a single `transform` op
    // otherwise.
    void write_value_xform_ops(
        const erhe::scene::Instance_override&      entry,
        std::map<std::string, lightusd::Property>& props
    )
    {
        std::vector<lightusd::XformOp> ops;
        set_transform(ops, entry.xform_op_stack.has_value() ? &entry.xform_op_stack.value() : nullptr, entry.transform);
        write_xform_op_props(ops, props);
    }

    static void write_xform_op_props(
        const std::vector<lightusd::XformOp>&      ops,
        std::map<std::string, lightusd::Property>& props
    )
    {
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

    // ---------------------------------------------------------------------
    // Variant sets (doc/usd-compatibility-plan.md X4)
    // ---------------------------------------------------------------------

    // One prim of a variant block: an `over` at its path below the prim
    // carrying the set, holding the `material:binding` the variant authors
    // for it. A prim on the way down to a bound one binds nothing and exists
    // so the path does.
    class Variant_prim final
    {
    public:
        std::string                           name;
        const erhe::primitive::Material*      material      {nullptr};
        const erhe::scene::Instance_override* override_entry{nullptr};
        std::vector<Variant_prim>             children;
    };

    [[nodiscard]] static auto find_or_add_variant_prim(std::vector<Variant_prim>& prims, const std::string_view name) -> Variant_prim&
    {
        for (Variant_prim& prim : prims) {
            if (prim.name == name) {
                return prim;
            }
        }
        prims.push_back(Variant_prim{.name = std::string{name}});
        return prims.back();
    }

    // The variant sets of one prim: the `variantSets` list op, the `variants`
    // selection and one `variantSet` block per set. A set whose variants all
    // came out empty is not written at all, because an empty block is not a
    // variant set USD would read back.
    void write_variant_sets(lightusd::Prim& prim, const Plan_prim& plan_prim)
    {
        if (plan_prim.variant_sets.empty()) {
            return;
        }
        std::vector<std::string>      set_names;
        lightusd::VariantSelectionMap selection;
        for (const Usd_save_variant_set* set : plan_prim.variant_sets) {
            lightusd::VariantSet usd_set;
            usd_set.name = set->set_name;
            for (const Usd_save_variant& variant : set->variants) {
                usd_set.variantSet.emplace(variant.name, write_variant(*set, variant, plan_prim));
            }
            if (usd_set.variantSet.empty()) {
                continue;
            }
            set_names.push_back(set->set_name);
            if (!set->selected.empty()) {
                selection[set->set_name] = set->selected;
            }
            prim.variantSets().emplace(set->set_name, std::move(usd_set));
        }
        if (set_names.empty()) {
            return;
        }
        lightusd::PrimMetas& metas = prim.metas();
        metas.variantSets = std::vector<std::pair<lightusd::ListEditQual, std::vector<std::string>>>{
            std::make_pair(lightusd::ListEditQual::Append, std::move(set_names))
        };
        if (!selection.empty()) {
            metas.variants = selection;
        }
    }

    // One variant: the binding of the prim carrying the set as a property of
    // the variant itself, and every deeper binding as an `over` prim at its
    // relative path.
    // The `Variant_prim` at one relative path below the prim carrying the
    // set, made on the way down if it is not there yet. Null for the empty
    // path, which names the carrying prim - the variant itself.
    [[nodiscard]] static auto find_or_add_variant_path(std::vector<Variant_prim>& tree, const std::string& relative_path) -> Variant_prim*
    {
        std::vector<Variant_prim>* level = &tree;
        Variant_prim*              prim  = nullptr;
        std::size_t                start = 0;
        while (start < relative_path.size()) {
            const std::size_t      slash = relative_path.find('/', start);
            const std::string_view name  = (slash == std::string::npos)
                ? std::string_view{relative_path}.substr(start)
                : std::string_view{relative_path}.substr(start, slash - start);
            start = (slash == std::string::npos) ? relative_path.size() : (slash + 1);
            prim  = &find_or_add_variant_prim(*level, name);
            level = &prim->children;
        }
        return prim;
    }

    [[nodiscard]] auto write_variant(
        const Usd_save_variant_set& set,
        const Usd_save_variant&     variant,
        const Plan_prim&            plan_prim
    ) -> lightusd::Variant
    {
        lightusd::Variant         usd_variant;
        std::vector<Variant_prim> tree;
        for (const erhe::scene::Instance_override& entry : variant.overrides) {
            if (entry.relative_path.empty()) {
                write_variant_values(entry, usd_variant.properties(), usd_variant.metas());
                continue;
            }
            Variant_prim* const prim = find_or_add_variant_path(tree, entry.relative_path);
            if (prim != nullptr) {
                prim->override_entry = &entry;
            }
        }
        for (const Usd_save_variant_binding& binding : variant.bindings) {
            if (!binding.material) {
                add_warning(
                    fmt::format(
                        "variant '{}' of set '{}' binds no material at '{}' - the binding is not written",
                        variant.name,
                        set.set_name,
                        binding.relative_path
                    )
                );
                continue;
            }
            if (binding.relative_path.empty()) {
                if (add_material_binding(usd_variant.properties(), *binding.material.get())) {
                    apply_material_binding_api(usd_variant.metas());
                }
                continue;
            }
            Variant_prim* const prim = find_or_add_variant_path(tree, binding.relative_path);
            if (prim != nullptr) {
                prim->material = binding.material.get();
            }
        }
        for (const Variant_prim& variant_prim : tree) {
            usd_variant.primChildren().push_back(write_variant_prim(variant_prim));
        }
        // The prims this variant adds, each the `def` it is, with its whole
        // subtree (doc/usd-compatibility-plan.md X4).
        for (const Plan_variant_prim& added : plan_prim.variant_prims) {
            if ((added.set != &set) || (added.variant_name != variant.name)) {
                continue;
            }
            lightusd::Prim prim = write_plan_prim(*added.prim.get());
            if (variant.name != set.selected) {
                // A prim of an unselected variant is inactive in the scene
                // because its variant is not the selection, which USD says
                // by not building the prim at all. Its own `active` opinion
                // is what the selected variant's prims write.
                prim.metas().remove_active();
            }
            usd_variant.primChildren().push_back(std::move(prim));
        }
        return usd_variant;
    }

    [[nodiscard]] auto write_variant_prim(const Variant_prim& variant_prim) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name = variant_prim.name;
        model.spec = lightusd::Specifier::Over;
        if (variant_prim.override_entry != nullptr) {
            write_variant_values(*variant_prim.override_entry, model.props, model.meta);
        }
        const bool variant_bound = (variant_prim.material != nullptr) && add_material_binding(model.props, *variant_prim.material);
        lightusd::Prim prim{model};
        if (variant_bound) {
            apply_material_binding_api(prim);
        }
        for (const Variant_prim& child : variant_prim.children) {
            std::string error;
            if (!prim.add_child(write_variant_prim(child), false, &error)) {
                add_warning(fmt::format("a variant prim below '{}' could not be added: {}", variant_prim.name, error));
            }
        }
        return prim;
    }

    // `rel material:binding = </path>`, by the path the writer gave the
    // material's prim - the same path a mesh's own binding names (U4 2g).
    [[nodiscard]] auto add_material_binding(std::map<std::string, lightusd::Property>& props, const erhe::primitive::Material& material) -> bool
    {
        const std::map<const erhe::primitive::Material*, std::string>::const_iterator i = m_material_paths.find(&material);
        if (i == m_material_paths.end()) {
            add_warning(
                fmt::format("material '{}' is bound by a variant but was not written - the binding is dropped", material.get_name())
            );
            return false;
        }
        lightusd::Relationship relationship;
        relationship.set(lightusd::Path{i->second, ""});
        props.emplace("material:binding", lightusd::Property{std::move(relationship), false});
        return true;
    }

    // A prim that binds a material applies the MaterialBindingAPI: the
    // relationship alone is what USD reads, and the applied schema is what
    // says the prim carries one (usdchecker's MaterialBindingAPIAppliedChecker
    // fails a prim that has the one without the other).
    static void apply_material_binding_api(lightusd::PrimMeta& meta)
    {
        apply_api_schema(meta, lightusd::APISchemas::APIName::MaterialBindingAPI, std::string{});
    }

    static void apply_material_binding_api(lightusd::Prim& prim)
    {
        apply_material_binding_api(prim.metas());
    }

    // The property opinions of one override entry, in the form a typeless
    // prim carries them - the same form the X2 `over` writer uses, so the
    // reader reads both back through one path: `visible` and `purpose` as the
    // native tokens, `active` as prim metadata, everything else as an
    // `erhe:Owner:name` custom attribute. The value travels as text, so the
    // property registry is what types it again; a name that reaches no
    // property, or text that does not parse, is one warning and no attribute.
    void write_variant_values(
        const erhe::scene::Instance_override&      entry,
        std::map<std::string, lightusd::Property>& props,
        lightusd::PrimMetas&                       metas
    )
    {
        for (const erhe::scene::Instance_override_value& value : entry.values) {
            if (value.state == erhe::scene::Instance_override_value_state::cleared) {
                continue; // a base value, which is never written
            }
            if (value.name == "visible") {
                add_token_attribute(
                    props,
                    "visibility",
                    lightusd::to_string((value.text == "false") ? lightusd::Visibility::Invisible : lightusd::Visibility::Inherited)
                );
                continue;
            }
            if (value.name == "purpose") {
                add_token_attribute(props, "purpose", to_usd_purpose_token(value.text));
                continue;
            }
            if (value.name == "active") {
                metas.set_active(value.text != "false");
                continue;
            }
            const erhe::property::Dependency_property* property = find_property_by_qualified_name(value.name);
            if (property == nullptr) {
                add_warning(fmt::format("a variant authors '{}', which names no property - the opinion is not written", value.name));
                continue;
            }
            const std::optional<erhe::property::Property_value> parsed = erhe::property::parse_value(*property, value.text);
            if (!parsed.has_value()) {
                add_warning(
                    fmt::format("a variant authors '{}' as '{}', which does not parse - the opinion is not written", value.name, value.text)
                );
                continue;
            }
            const std::string attribute_name = fmt::format(
                "erhe:{}:{}",
                erhe::property::get_owner_type_name(property->get_owner_type()),
                property->get_name()
            );
            props.emplace(attribute_name, lightusd::Property{make_custom_attribute(*property, parsed.value()), true});
        }
        if (entry.transform_overridden) {
            write_value_xform_ops(entry, props);
        }
    }

    // The property a qualified `Owner.name` names, without an object to ask:
    // a variant's opinion is written from the text form alone, and the name
    // the reader recorded is the one the property's own owner type gives it.
    [[nodiscard]] static auto find_property_by_qualified_name(const std::string& name) -> const erhe::property::Dependency_property*
    {
        const std::size_t dot = name.find('.');
        if (dot == std::string::npos) {
            return nullptr;
        }
        const erhe::property::Property_registry&        registry = erhe::property::Property_registry::get();
        const std::optional<erhe::property::Owner_type> owner    = registry.find_owner_type(std::string_view{name}.substr(0, dot));
        if (!owner.has_value()) {
            return nullptr;
        }
        return registry.find(owner.value(), std::string_view{name}.substr(dot + 1));
    }

    // The USD `purpose` token an erhe Purpose label spells (M3): the same
    // vocabulary, uncapitalized.
    [[nodiscard]] static auto to_usd_purpose_token(const std::string& text) -> std::string
    {
        if (text == "Render") { return "render"; }
        if (text == "Proxy" ) { return "proxy";  }
        if (text == "Guide" ) { return "guide";  }
        return "default";
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
        if (is_brush_prim(*plan_prim.item)) {
            return write_brush_prim(*plan_prim.item, plan_prim.name);
        }
        if (is_node_graph_prim(*plan_prim.item)) {
            return write_node_graph_prim(*plan_prim.item, plan_prim.name, plan_prim.path);
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

    // A brush item as the `Brush`-typed prim it is
    // (doc/usd-compatibility-plan.md E4a): a prim of erhe's own type name
    // holding the brush's geometry as a child `Mesh`, the density and the
    // normal style as `erhe:Brush:` custom attributes, and the material a
    // placed instance gets as the prim's own `material:binding`. USD has no
    // schema for it, so a viewer without erhe sees a prim of unknown type
    // with a `Mesh` child.
    [[nodiscard]] auto write_brush_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name           = prim_name;
        model.prim_type_name = std::string{c_brush_prim_type_name};
        write_token_visibility_and_purpose(item, model.props);
        write_active(item, model);
        write_erhe_properties(item, model);

        const std::map<const erhe::Item_base*, const Usd_save_brush*>::const_iterator i = m_brushes.find(&item);
        if (i == m_brushes.end()) {
            add_warning(
                fmt::format("brush '{}' was not offered to the writer - it is written without its geometry", prim_name)
            );
            return lightusd::Prim{model};
        }
        const Usd_save_brush& brush = *i->second;

        lightusd::Attribute density;
        density.set_value(brush.density);
        model.props.emplace(std::string{c_brush_density_attribute}, lightusd::Property{std::move(density), true});
        if (!brush.normal_style.empty()) {
            lightusd::Attribute normal_style;
            normal_style.set_value(lightusd::value::token{brush.normal_style});
            model.props.emplace(std::string{c_brush_normal_style_attribute}, lightusd::Property{std::move(normal_style), true});
        }
        const bool brush_bound = brush.material && add_material_binding(model.props, *brush.material.get());

        lightusd::Prim prim{model};
        if (brush_bound) {
            apply_material_binding_api(prim);
        }
        if (!brush.geometry) {
            add_warning(fmt::format("brush '{}' has no geometry - the prim is written without its Mesh child", prim_name));
            return prim;
        }
        std::string error;
        if (!prim.add_child(write_geometry_mesh_prim(*brush.geometry.get(), std::string{c_brush_geometry_prim_name}), false, &error)) {
            add_warning(fmt::format("the geometry of brush '{}' could not be added: {}", prim_name, error));
        }
        return prim;
    }

    // One node parameter as the attribute its recorded USD type names
    // (doc/usd-texture-graphs-plan.md 2.2). A type the writer has no USD form
    // for is one warning and a `string` carrying the text as it stands, which
    // is the same rule a gradient or a curve travels by.
    [[nodiscard]] auto make_node_graph_attribute(
        const std::string& usd_type,
        const std::string& text,
        const std::string& owner
    ) -> lightusd::Attribute
    {
        lightusd::Attribute attribute;
        if (usd_type == "float") {
            attribute.set_value(std::strtof(text.c_str(), nullptr));
        } else if (usd_type == "int") {
            attribute.set_value(static_cast<std::int32_t>(std::strtol(text.c_str(), nullptr, 10)));
        } else if (usd_type == "bool") {
            attribute.set_value((text == "true") || (text == "1"));
        } else if (usd_type == "token") {
            attribute.set_value(lightusd::value::token{strip_usd_quotes(text)});
        } else if (usd_type == "float2") {
            const std::vector<float> v = parse_usd_float_tuple(text, 2);
            attribute.set_value(lightusd::value::float2{v[0], v[1]});
        } else if (usd_type == "color3f") {
            const std::vector<float> v = parse_usd_float_tuple(text, 3);
            attribute.set_value(lightusd::value::color3f{v[0], v[1], v[2]});
        } else if (usd_type == "color4f") {
            const std::vector<float> v = parse_usd_float_tuple(text, 4);
            attribute.set_value(lightusd::value::color4f{v[0], v[1], v[2], v[3]});
        } else if (usd_type == "float3") {
            // A geometry graph's vector parameters - a translation, a scale, a
            // centre - are quantities, not colors, so the caller names them
            // `float3` / `float4` where a texture graph names a color
            // (doc/usd_compatibility.md, "Geometry node graphs").
            const std::vector<float> v = parse_usd_float_tuple(text, 3);
            attribute.set_value(lightusd::value::float3{v[0], v[1], v[2]});
        } else if (usd_type == "float4") {
            const std::vector<float> v = parse_usd_float_tuple(text, 4);
            attribute.set_value(lightusd::value::float4{v[0], v[1], v[2], v[3]});
        } else {
            if (usd_type != "string") {
                add_warning(
                    fmt::format("'{}' has no USD form for type '{}' - it is written as a string", owner, usd_type)
                );
            }
            attribute.set_value(strip_usd_quotes(text));
        }
        return attribute;
    }

    // One pin as the `inputs:` / `outputs:` attribute it is: the pin's value
    // type, and a `.connect` to the source node's output when the pin carries
    // a link. An unlinked pin is the typed attribute alone, so the pin exists
    // in the file (doc/usd-texture-graphs-plan.md 2.1).
    [[nodiscard]] static auto make_node_graph_pin_attribute(
        const Usd_node_graph_pin&                   pin,
        const std::string&                          graph_path,
        const std::map<std::string, std::string>&   node_prim_names
    ) -> lightusd::Attribute
    {
        lightusd::Attribute attribute;
        attribute.set_type_name(pin.value_type);
        if (pin.source_node.empty()) {
            return attribute;
        }
        const std::map<std::string, std::string>::const_iterator i = node_prim_names.find(pin.source_node);
        if (i == node_prim_names.end()) {
            return attribute;
        }
        attribute.set_connection(
            lightusd::Path{
                graph_path + "/" + i->second,
                std::string{c_node_graph_output_prefix} + pin.source_pin
            }
        );
        return attribute;
    }

    // One node of a graph as the generic `Shader` prim it is: `info:id` the
    // node's type name under the prefix the graph's format names, the editor
    // position as one custom attribute, and the parameters and pins as its
    // `inputs:` / `outputs:` properties.
    [[nodiscard]] auto write_node_graph_node_prim(
        const Usd_node_graph_node&                node,
        const std::string_view                    node_id_prefix,
        const std::string&                        prim_name,
        const std::string&                        graph_path,
        const std::map<std::string, std::string>& node_prim_names
    ) -> lightusd::Prim
    {
        lightusd::ShaderNode shader_node;
        if (node.has_position) {
            lightusd::Attribute position;
            position.set_value(lightusd::value::float2{node.position_x, node.position_y});
            shader_node.props.emplace(
                std::string{c_node_graph_position_attribute},
                lightusd::Property{std::move(position), true}
            );
        }
        for (const Usd_node_graph_parameter& parameter : node.parameters) {
            const std::string name = std::string{c_node_graph_input_prefix} + parameter.name;
            shader_node.props.emplace(
                name,
                lightusd::Property{make_node_graph_attribute(parameter.usd_type, parameter.value, prim_name + "." + name), false}
            );
        }
        for (const Usd_node_graph_pin& pin : node.inputs) {
            shader_node.props.emplace(
                std::string{c_node_graph_input_prefix} + pin.name,
                lightusd::Property{make_node_graph_pin_attribute(pin, graph_path, node_prim_names), false}
            );
        }
        for (const Usd_node_graph_pin& pin : node.outputs) {
            lightusd::Attribute attribute;
            attribute.set_type_name(pin.value_type);
            shader_node.props.emplace(
                std::string{c_node_graph_output_prefix} + pin.name,
                lightusd::Property{std::move(attribute), false}
            );
        }

        lightusd::Shader shader;
        shader.name    = prim_name;
        shader.info_id = std::string{node_id_prefix} + node.type_name;
        shader.value   = std::move(shader_node);
        return lightusd::Prim{shader};
    }

    // A graph asset as the marked `NodeGraph` prim it is
    // (doc/usd-texture-graphs-plan.md 2.4, section 4): the marker attribute,
    // the interface outputs as connections into the nodes, one generic
    // `Shader` child per node, in the record's order so a second save spells
    // the same file (R4), and - for a geometry graph that has evaluated
    // something - its geometry as the child `Mesh "result"` a brush's
    // geometry is written as, so a viewer without erhe sees what the graph
    // makes.
    [[nodiscard]] auto write_node_graph_prim(
        const erhe::Typed& item,
        const std::string& prim_name,
        const std::string& prim_path
    ) -> lightusd::Prim
    {
        const std::map<const erhe::Item_base*, const Usd_save_node_graph*>::const_iterator i = m_node_graphs.find(&item);
        const Usd_save_node_graph& record = *i->second;

        // The node prim names: the node name by the identifier rule, made
        // sibling-unique by the M2 rule the writer uses everywhere.
        Name_scope                         node_names;
        std::map<std::string, std::string> node_prim_names;
        std::vector<std::string>           prim_names;
        prim_names.reserve(record.nodes.size());
        for (const Usd_node_graph_node& node : record.nodes) {
            const std::string name = node_names.make_unique(node.name);
            prim_names.push_back(name);
            node_prim_names[node.name] = name;
        }

        lightusd::NodeGraph graph;
        graph.name = prim_name;
        lightusd::Attribute format;
        format.set_value(lightusd::value::token{record.format});
        graph.props.emplace(std::string{c_node_graph_format_attribute}, lightusd::Property{std::move(format), true});
        for (const Usd_node_graph_pin& output : record.outputs) {
            graph.props.emplace(
                std::string{c_node_graph_output_prefix} + output.name,
                lightusd::Property{make_node_graph_pin_attribute(output, prim_path, node_prim_names), false}
            );
        }

        const std::string_view node_id_prefix = node_graph_node_id_prefix(record.format);
        if (node_id_prefix.empty() && !record.nodes.empty()) {
            add_warning(
                fmt::format("graph '{}' has format '{}', which names no node id prefix", prim_name, record.format)
            );
        }
        lightusd::Prim prim{graph};
        for (std::size_t index = 0, end = record.nodes.size(); index < end; ++index) {
            std::string error;
            if (
                !prim.add_child(
                    write_node_graph_node_prim(record.nodes[index], node_id_prefix, prim_names[index], prim_path, node_prim_names),
                    false,
                    &error
                )
            ) {
                add_warning(fmt::format("node '{}' of graph '{}' could not be added: {}", prim_names[index], prim_name, error));
            }
        }
        if (record.geometry) {
            std::string error;
            if (
                !prim.add_child(
                    write_geometry_mesh_prim(*record.geometry.get(), std::string{c_node_graph_result_prim_name}),
                    false,
                    &error
                )
            ) {
                add_warning(fmt::format("the result geometry of graph '{}' could not be added: {}", prim_name, error));
            }
        }
        return prim;
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

        const std::shared_ptr<erhe::scene::Point_instancer> point_instancer = std::dynamic_pointer_cast<erhe::scene::Point_instancer>(
            const_cast<erhe::scene::Node&>(node).shared_from_this()
        );

        const Instance_root_override& override_root = plan_prim.override_root;

        // A prim that is the pivot of a skin is the file's `Skeleton`
        // (doc/usd-compatibility-plan.md K1), which is a transformable prim
        // of its own kind rather than the plain `Xform` it would otherwise be.
        const std::map<const erhe::Item_base*, Skeleton_record>::const_iterator skeleton = m_skeletons.find(&node);
        if (skeleton != m_skeletons.end()) {
            return write_skeleton_prim(node, skeleton->second, plan_prim, prim_name, matrix, override_root);
        }

        lightusd::Prim prim =
            mesh            ? write_mesh_prim           (node, *mesh.get(),   prim_name, matrix, override_root) :
            camera          ? write_camera_prim         (node, *camera.get(), prim_name, matrix, override_root) :
            light           ? write_light_prim          (node, *light.get(),  prim_name, matrix, override_root) :
            point_instancer ? write_point_instancer_prim(plan_prim,           prim_name, matrix, override_root) :
                              write_xform_prim          (node,                prim_name, matrix, override_root);
        return prim;
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
    void set_transform(std::vector<lightusd::XformOp>& xform_ops, const erhe::scene::Node& node, const glm::mat4& matrix)
    {
        set_transform(xform_ops, node.get_xform_op_stack(), matrix);
    }

    void set_transform(
        std::vector<lightusd::XformOp>&    xform_ops,
        const erhe::scene::Xform_op_stack* stack,
        const glm::mat4&                   matrix
    )
    {
        // A time-sampled stack is written whatever the transform the prim
        // holds right now says: that transform is the pose the animation
        // player put the prim in, and the stack is what the file authored
        // (src/erhe/usd/notes.md, "Time samples").
        if ((stack != nullptr) && (stack->has_time_samples() || is_near(glm::mat4{stack->compose()}, matrix))) {
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

    void write_xform_op_stack(std::vector<lightusd::XformOp>& xform_ops, const erhe::scene::Xform_op_stack& stack)
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
            for (const erhe::scene::Xform_op_sample& sample : op.samples) {
                m_first_time_code = m_wrote_time_samples ? std::min(m_first_time_code, sample.time_code) : sample.time_code;
                m_last_time_code  = m_wrote_time_samples ? std::max(m_last_time_code,  sample.time_code) : sample.time_code;
                m_wrote_time_samples = true;
            }
            xform_ops.push_back(usd_op);
        }
    }

    // One point instancer as the `PointInstancer` prim it is
    // (doc/usd-compatibility-plan.md S1). The prototypes are the children the
    // plan holds - the instances are not among them - named by `rel
    // prototypes` in tree order, and `positions`, `orientations` and `scales`
    // and `protoIndices` are recomputed from the instance prims - their own
    // transforms and the prototype each one references - so an instance the
    // user moved, deleted or duplicated persists. `orientations` and `scales`
    // are written when any instance needs them: a rotation or a scale of the
    // tree is never dropped, and an instancer of plain translations stays as
    // compact as the file that authored it.
    [[nodiscard]] auto write_point_instancer_prim(
        const Plan_prim&              plan_prim,
        const std::string&            prim_name,
        const glm::mat4&              matrix,
        const Instance_root_override& override_root
    ) -> lightusd::Prim
    {
        const erhe::scene::Node& node = *plan_prim.node;
        lightusd::GeomPointInstancer instancer;
        instancer.name = prim_name;
        set_transform(instancer.xformOps, node, matrix);
        write_visibility_and_purpose(node, instancer);
        write_erhe_properties(node, instancer);
        write_instance_root_override(node, override_root, instancer);

        std::vector<std::string> prototype_paths;
        collect_prototype_paths(plan_prim, prototype_paths);
        if (!prototype_paths.empty()) {
            std::vector<lightusd::Path> usd_prototype_paths;
            usd_prototype_paths.reserve(prototype_paths.size());
            for (const std::string& prototype_path : prototype_paths) {
                usd_prototype_paths.push_back(lightusd::Path{prototype_path, ""});
            }
            lightusd::Relationship prototypes;
            prototypes.set(std::move(usd_prototype_paths));
            instancer.prototypes = prototypes;
        }

        const std::map<const erhe::Item_base*, const Usd_save_point_instancer*>::const_iterator i =
            m_point_instancers.find(plan_prim.item);
        if (i == m_point_instancers.end()) {
            add_warning(
                fmt::format("point instancer '{}' was not offered to the writer - it is written without its instances", prim_name)
            );
            return lightusd::Prim{instancer};
        }

        std::vector<lightusd::value::point3f> positions;
        std::vector<lightusd::value::quath>   orientations;
        std::vector<lightusd::value::float3>  scales;
        std::vector<int32_t>                  usd_proto_indices;
        bool                                  any_rotation{false};
        bool                                  any_scale   {false};
        for (const Usd_save_point_instance& instance : i->second->instances) {
            const erhe::scene::Node* instance_node = dynamic_cast<const erhe::scene::Node*>(instance.item.get());
            if (instance_node == nullptr) {
                continue;
            }
            const erhe::scene::Trs_transform transform   = instance_node->parent_from_node_transform();
            const glm::vec3                  translation = transform.get_translation();
            const glm::quat                  rotation    = transform.get_rotation();
            const glm::vec3                  scale       = transform.get_scale();
            positions.push_back(lightusd::value::point3f{translation.x, translation.y, translation.z});
            orientations.push_back(
                lightusd::value::quath{
                    {to_usd_half(rotation.x), to_usd_half(rotation.y), to_usd_half(rotation.z)},
                    to_usd_half(rotation.w)
                }
            );
            scales.push_back(lightusd::value::float3{scale.x, scale.y, scale.z});
            any_rotation = any_rotation || (rotation != glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
            any_scale    = any_scale    || (scale    != glm::vec3{1.0f, 1.0f, 1.0f});
            usd_proto_indices.push_back(static_cast<int32_t>(instance.proto_index));
        }
        if (positions.empty()) {
            return lightusd::Prim{instancer};
        }
        instancer.positions.set_value(positions);
        instancer.protoIndices.set_value(usd_proto_indices);
        if (any_rotation) {
            instancer.orientations.set_value(orientations);
        }
        if (any_scale) {
            instancer.scales.set_value(scales);
        }
        return lightusd::Prim{instancer};
    }

    [[nodiscard]] auto write_xform_prim(
        const erhe::scene::Node& node,
        const std::string&       prim_name,
        const glm::mat4&         matrix,
        const Instance_root_override& override_root
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
    void write_instance_root_override(const erhe::Item_base& carrier, const Instance_root_override& override_root, T& typed_prim)
    {
        if (override_root.material != nullptr) {
            if constexpr (std::is_base_of_v<lightusd::MaterialBinding, T>) {
                if (bind_material(typed_prim, override_root.material)) {
                    apply_material_binding_api(typed_prim.meta);
                }
            } else {
                add_warning(
                    fmt::format(
                        "prim '{}': an arc's target binds a material and is written as a prim that carries no binding - the binding is not written",
                        carrier.get_name()
                    )
                );
            }
        }
        if (override_root.item == nullptr) {
            return;
        }
        const erhe::Item_base& item = *override_root.item;
        if (is_override_available(carrier, item, erhe::Item_base::visible_property.get())) {
            typed_prim.visibility.set_value(
                item.get_value(erhe::Item_base::visible_property)
                    ? lightusd::Visibility::Inherited
                    : lightusd::Visibility::Invisible
            );
        }
        if (is_override_available(carrier, item, erhe::Item_base::purpose_property.get())) {
            typed_prim.purpose.set_value(to_usd_purpose(item.get_value(erhe::Item_base::purpose_property)));
        }
        if (is_override_available(carrier, item, erhe::Item_base::active_property.get())) {
            typed_prim.meta.set_active(item.get_value(erhe::Item_base::active_property));
        }
        // A name an `erhe:` attribute of the carrier already carries is
        // reported and dropped by write_erhe_properties itself.
        write_erhe_properties(item, typed_prim);
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
    // Skinning
    // -------------------------------------------------------------------

    // The skeletons the tree's skinned meshes name (K1). `Mesh::skin` is all
    // the writer needs: a skin names its joints in `joints` order and its
    // pivot, and that pivot is the prim the `Skeleton` is written on.
    //
    // USD authors the bind pose on the skeleton and the geometry bind
    // transform on the mesh, while erhe keeps only their product
    // `inverse_bind_j = inverse(bind_j) * geomBindTransform`. The first skin
    // of a skeleton is therefore written through the identity geometry bind
    // transform, with `bindTransforms[j] = inverse(inverse_bind_j)`; every
    // further skin of the same skeleton keeps those bind transforms and
    // carries the difference as its own `primvars:skel:geomBindTransform`,
    // `bind_0 * inverse_bind_0`. Reading either back gives exactly the
    // inverse bind matrices the skins hold, which is what makes a save a
    // fixed point although the file's original bind pose is not kept.
    void collect_skeletons(const erhe::Hierarchy& parent)
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            if (!child) {
                continue;
            }
            const erhe::scene::Mesh* mesh = dynamic_cast<const erhe::scene::Mesh*>(child.get());
            if ((mesh != nullptr) && mesh->skin) {
                register_skin(*mesh, *mesh->skin.get());
            }
            collect_skeletons(*child.get());
        }
    }

    // A prim carrying the authored `Skeleton` token that no skin of the tree
    // names: it is still written as the `Skeleton` prim it was read as, with
    // the joint arrays a skin would have supplied left out.
    void collect_skeleton_tokens(const erhe::Hierarchy& parent)
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : parent.get_children()) {
            if (!child) {
                continue;
            }
            const erhe::scene::Node* node = dynamic_cast<const erhe::scene::Node*>(child.get());
            const erhe::Typed*       item = dynamic_cast<const erhe::Typed*>(child.get());
            if (
                (node != nullptr) &&
                (item != nullptr) &&
                (item->get_prim_type_name() == c_skeleton_prim_type_name) &&
                (m_skeletons.count(item) == 0)
            ) {
                add_warning(
                    fmt::format("skeleton '{}' skins no mesh of the scene - it is written without its joints", child->get_name())
                );
                Skeleton_record record{};
                record.node                = node;
                record.animation_prim_name = take_skel_animation_prim_name(*node);
                m_skeletons.emplace(item, std::move(record));
            }
            collect_skeleton_tokens(*child.get());
        }
    }

    void register_skin(const erhe::scene::Mesh& mesh, const erhe::scene::Skin& skin)
    {
        if (m_skin_geom_bind_transforms.count(&skin) != 0) {
            return;
        }
        const erhe::scene::Node* skeleton = skin.skin_data.skeleton.get();
        if (skeleton == nullptr) {
            add_warning(
                fmt::format("mesh '{}' names a skin with no skeleton prim - the mesh is written unskinned", mesh.get_name())
            );
            return;
        }
        const std::map<const erhe::Item_base*, Skeleton_record>::const_iterator i = m_skeletons.find(skeleton);
        if (i != m_skeletons.end()) {
            const Skeleton_record& record = i->second;
            if (skin.skin_data.joints.size() != record.joints.size()) {
                add_warning(
                    fmt::format(
                        "mesh '{}' binds skeleton '{}' through {} joint(s) where the skeleton was written with {} - the skeleton's joints are kept",
                        mesh.get_name(), skeleton->get_name(), skin.skin_data.joints.size(), record.joints.size()
                    )
                );
            }
            glm::mat4 geometry_from_bind{1.0f};
            if (!record.bind_transforms.empty() && !skin.skin_data.inverse_bind_matrices.empty()) {
                geometry_from_bind = record.bind_transforms.front() * skin.skin_data.inverse_bind_matrices.front();
            }
            m_skin_geom_bind_transforms.emplace(&skin, geometry_from_bind);
            return;
        }

        Skeleton_record record{};
        record.node                = skeleton;
        record.animation_prim_name = take_skel_animation_prim_name(*skeleton);
        record.joints.reserve(skin.skin_data.joints.size());
        record.bind_transforms.reserve(skin.skin_data.joints.size());
        for (std::size_t joint_index = 0, end = skin.skin_data.joints.size(); joint_index < end; ++joint_index) {
            const erhe::scene::Node* joint = skin.skin_data.joints[joint_index].get();
            record.joints.push_back(joint);
            if (joint != nullptr) {
                m_joint_items.insert(joint);
            }
            const glm::mat4 inverse_bind = (joint_index < skin.skin_data.inverse_bind_matrices.size())
                ? skin.skin_data.inverse_bind_matrices[joint_index]
                : glm::mat4{1.0f};
            record.bind_transforms.push_back(glm::inverse(inverse_bind));
        }
        m_skeletons.emplace(skeleton, std::move(record));
        m_skin_geom_bind_transforms.emplace(&skin, glm::mat4{1.0f});
    }

    // The name the skeleton's `SkelAnimation` prim is written under, and the
    // decision not to write the tree prim it came from: Tydra hands the
    // importer the joint channels rather than the prim, so the prim of the
    // tree carries nothing, and the writer rebuilds it from the channels
    // (doc/usd-compatibility-plan.md K1). Keeping the authored name is what
    // makes a save a fixed point.
    [[nodiscard]] auto take_skel_animation_prim_name(const erhe::scene::Node& skeleton) -> std::string
    {
        for (const std::shared_ptr<erhe::Hierarchy>& child : skeleton.get_children()) {
            const erhe::Typed* item = dynamic_cast<const erhe::Typed*>(child.get());
            if ((item != nullptr) && (item->get_prim_type_name() == c_skel_animation_prim_type_name)) {
                m_skel_animation_items.insert(item);
                return sanitize_usd_identifier(child->get_name());
            }
        }
        return std::string{c_skel_animation_prim_name};
    }

    // The `joints` token of one joint prim: its path below the skeleton prim,
    // sanitized segment by segment, so the prim `Bone_001_1` under `Bone_1`
    // under the skeleton is `Bone_1/Bone_001_1`. Empty when the joint is not
    // below the skeleton, which is what makes the skeleton write no joint
    // arrays at all.
    [[nodiscard]] static auto joint_token_of(const erhe::scene::Node& skeleton, const erhe::scene::Node& joint) -> std::string
    {
        std::string                      token  = sanitize_usd_identifier(joint.get_name());
        std::shared_ptr<erhe::Hierarchy> parent = joint.get_parent().lock();
        while (parent) {
            if (parent.get() == static_cast<const erhe::Hierarchy*>(&skeleton)) {
                return token;
            }
            token  = sanitize_usd_identifier(parent->get_name()) + "/" + token;
            parent = parent->get_parent().lock();
        }
        return std::string{};
    }

    // The joint paths of one skeleton, empty when a joint is missing or sits
    // outside the skeleton prim - a skeleton whose joints erhe cannot name is
    // written without joint arrays rather than with wrong ones.
    [[nodiscard]] auto joint_tokens_of(const Skeleton_record& record) -> std::vector<std::string>
    {
        std::vector<std::string> tokens;
        tokens.reserve(record.joints.size());
        for (const erhe::scene::Node* joint : record.joints) {
            const std::string token = (joint != nullptr) ? joint_token_of(*record.node, *joint) : std::string{};
            if (token.empty()) {
                add_warning(
                    fmt::format(
                        "skeleton '{}' has a joint that is not a prim below it - the skeleton is written without its joints",
                        record.node->get_name()
                    )
                );
                return std::vector<std::string>{};
            }
            tokens.push_back(token);
        }
        return tokens;
    }

    // One skeleton as the `Skeleton` prim it is (K1): the joint paths, the
    // rest pose the joint prims hold, the bind pose recomputed from the skin,
    // and - when the caller's animations drive its joints - the
    // `SkelAnimation` prim it names in `skel:animationSource`. The joints
    // themselves are not prims of the stage: plan_children leaves them out,
    // because USD carries them as these arrays.
    [[nodiscard]] auto write_skeleton_prim(
        const erhe::scene::Node&      node,
        const Skeleton_record&        record,
        const Plan_prim&              plan_prim,
        const std::string&            prim_name,
        const glm::mat4&              matrix,
        const Instance_root_override& override_root
    ) -> lightusd::Prim
    {
        lightusd::Skeleton skeleton;
        skeleton.name = prim_name;
        set_transform(skeleton.xformOps, node, matrix);
        write_visibility_and_purpose(node, skeleton);
        write_erhe_properties(node, skeleton);
        write_instance_root_override(node, override_root, skeleton);

        const std::vector<std::string> joint_tokens = joint_tokens_of(record);
        std::optional<lightusd::Prim>  animation_prim;
        std::string                    animation_name;
        if (!joint_tokens.empty()) {
            std::vector<lightusd::value::token>    joints;
            std::vector<lightusd::value::matrix4d> bind_transforms;
            std::vector<lightusd::value::matrix4d> rest_transforms;
            joints.reserve(joint_tokens.size());
            bind_transforms.reserve(joint_tokens.size());
            rest_transforms.reserve(joint_tokens.size());
            for (std::size_t joint_index = 0, end = joint_tokens.size(); joint_index < end; ++joint_index) {
                joints.emplace_back(joint_tokens[joint_index]);
                bind_transforms.push_back(to_usd(record.bind_transforms[joint_index]));
                rest_transforms.push_back(to_usd(record.joints[joint_index]->parent_from_node()));
            }
            skeleton.joints.set_value(std::move(joints));
            skeleton.bindTransforms.set_value(std::move(bind_transforms));
            skeleton.restTransforms.set_value(std::move(rest_transforms));

            Name_scope animation_names;
            for (const Plan_prim& child : plan_prim.children) {
                static_cast<void>(animation_names.make_unique(child.name));
            }
            animation_name = animation_names.make_unique(record.animation_prim_name);
            animation_prim = write_skel_animation_prim(record, joint_tokens, animation_name);
        }
        if (animation_prim.has_value()) {
            lightusd::Relationship relationship;
            relationship.set(lightusd::Path{fmt::format("{}/{}", plan_prim.path, animation_name), ""});
            skeleton.animationSource = relationship;
        }

        lightusd::Prim prim{skeleton};
        if (animation_prim.has_value()) {
            // usdchecker fails a prim that carries a SkelBindingAPI
            // relationship without the applied schema, exactly as it does for
            // a material binding.
            apply_api_schema(prim.metas(), lightusd::APISchemas::APIName::SkelBindingAPI, std::string{});
            std::string error;
            if (!prim.add_child(std::move(animation_prim.value()), false, &error)) {
                add_warning(fmt::format("the animation of skeleton '{}' could not be added: {}", prim_name, error));
            }
        }
        return prim;
    }

    // The channels of the caller's animations that drive one joint of one
    // skeleton.
    class Joint_channels final
    {
    public:
        const erhe::scene::Animation_sampler* translation{nullptr};
        const erhe::scene::Animation_sampler* rotation   {nullptr};
        const erhe::scene::Animation_sampler* scale      {nullptr};
    };

    // One channel's value at a time in seconds: linear between the two keys
    // around it and clamped to the first and the last key outside the keyed
    // range, which is how an erhe animation sampler reads.
    [[nodiscard]] static auto sample_channel(
        const erhe::scene::Animation_sampler& sampler,
        const std::size_t                     component_count,
        const float                           time,
        glm::vec4&                            out_value
    ) -> bool
    {
        const std::size_t key_count = std::min(
            sampler.timestamps.size(),
            (component_count > 0) ? (sampler.data.size() / component_count) : std::size_t{0}
        );
        if (key_count == 0) {
            return false;
        }
        const auto value_at = [&sampler, component_count](const std::size_t key) -> glm::vec4 {
            glm::vec4 value{0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t component = 0; component < component_count; ++component) {
                value[static_cast<glm::length_t>(component)] = sampler.data[(key * component_count) + component];
            }
            return value;
        };
        if (time <= sampler.timestamps.front()) {
            out_value = value_at(0);
            return true;
        }
        if (time >= sampler.timestamps[key_count - 1]) {
            out_value = value_at(key_count - 1);
            return true;
        }
        for (std::size_t key = 1; key < key_count; ++key) {
            if (time > sampler.timestamps[key]) {
                continue;
            }
            const float span = sampler.timestamps[key] - sampler.timestamps[key - 1];
            const float t    = (span > 0.0f) ? ((time - sampler.timestamps[key - 1]) / span) : 0.0f;
            out_value = glm::mix(value_at(key - 1), value_at(key), t);
            return true;
        }
        out_value = value_at(key_count - 1);
        return true;
    }

    // The translation, rotation and scale of a matrix with no shear, which is
    // what a joint's rest transform is: the joint's pose at every time code a
    // channel of another joint keys but this one does not.
    static void decompose_trs(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
    {
        translation = glm::vec3{matrix[3]};
        scale = glm::vec3{
            glm::length(glm::vec3{matrix[0]}),
            glm::length(glm::vec3{matrix[1]}),
            glm::length(glm::vec3{matrix[2]})
        };
        glm::mat3 basis{glm::vec3{matrix[0]}, glm::vec3{matrix[1]}, glm::vec3{matrix[2]}};
        for (int column = 0; column < 3; ++column) {
            if (scale[column] > 0.0f) {
                basis[column] /= scale[column];
            }
        }
        rotation = glm::normalize(glm::quat_cast(basis));
    }

    // The joint channels driving one skeleton, as the `SkelAnimation` prim
    // USD carries them in (K1). USD keys every joint's translation, rotation
    // and scale of one skeleton on one shared timeline, so the time codes
    // written are the union of the times the channels key, and a joint no
    // channel reaches contributes its rest pose at each of them.
    [[nodiscard]] auto write_skel_animation_prim(
        const Skeleton_record&          record,
        const std::vector<std::string>& joint_tokens,
        const std::string&              prim_name
    ) -> std::optional<lightusd::Prim>
    {
        const double time_codes_per_second = (m_arguments.time_codes_per_second > 0.0) ? m_arguments.time_codes_per_second : 24.0;

        std::vector<Joint_channels> joint_channels;
        joint_channels.resize(record.joints.size());
        std::set<double> time_codes;
        bool             any_channel{false};
        for (const std::shared_ptr<erhe::scene::Animation>& animation : m_arguments.animations) {
            if (!animation) {
                continue;
            }
            for (const erhe::scene::Animation_channel& channel : animation->channels) {
                if (!channel.target || (channel.sampler_index >= animation->samplers.size())) {
                    continue;
                }
                const std::vector<const erhe::scene::Node*>::const_iterator i = std::find(
                    record.joints.begin(), record.joints.end(), channel.target.get()
                );
                if (i == record.joints.end()) {
                    continue;
                }
                const erhe::scene::Animation_sampler&  sampler = animation->samplers[channel.sampler_index];
                Joint_channels&                        joint   = joint_channels[static_cast<std::size_t>(i - record.joints.begin())];
                const erhe::scene::Animation_sampler** target  = nullptr;
                switch (channel.path) {
                    case erhe::scene::Animation_path::TRANSLATION: target = &joint.translation; break;
                    case erhe::scene::Animation_path::ROTATION:    target = &joint.rotation;    break;
                    case erhe::scene::Animation_path::SCALE:       target = &joint.scale;       break;
                    default: break;
                }
                if ((target == nullptr) || (*target != nullptr)) {
                    continue;
                }
                *target     = &sampler;
                any_channel = true;
                for (const float timestamp : sampler.timestamps) {
                    time_codes.insert(static_cast<double>(timestamp) * time_codes_per_second);
                }
            }
        }
        if (!any_channel || time_codes.empty()) {
            return std::nullopt;
        }

        lightusd::SkelAnimation animation_prim;
        animation_prim.name = prim_name;
        std::vector<lightusd::value::token> joints;
        joints.reserve(joint_tokens.size());
        for (const std::string& token : joint_tokens) {
            joints.emplace_back(token);
        }
        animation_prim.joints.set_value(std::move(joints));

        lightusd::Animatable<std::vector<lightusd::value::float3>> translations;
        lightusd::Animatable<std::vector<lightusd::value::quatf>>  rotations;
        lightusd::Animatable<std::vector<lightusd::value::half3>>  scales;
        for (const double time_code : time_codes) {
            const float                          time = static_cast<float>(time_code / time_codes_per_second);
            std::vector<lightusd::value::float3> translation_values;
            std::vector<lightusd::value::quatf>  rotation_values;
            std::vector<lightusd::value::half3>  scale_values;
            translation_values.reserve(record.joints.size());
            rotation_values.reserve(record.joints.size());
            scale_values.reserve(record.joints.size());
            for (std::size_t joint_index = 0, end = record.joints.size(); joint_index < end; ++joint_index) {
                glm::vec3 translation{0.0f, 0.0f, 0.0f};
                glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
                glm::vec3 scale{1.0f, 1.0f, 1.0f};
                decompose_trs(record.joints[joint_index]->parent_from_node(), translation, rotation, scale);

                const Joint_channels& channels = joint_channels[joint_index];
                glm::vec4             value{0.0f, 0.0f, 0.0f, 0.0f};
                if ((channels.translation != nullptr) && sample_channel(*channels.translation, 3, time, value)) {
                    translation = glm::vec3{value};
                }
                if ((channels.rotation != nullptr) && sample_channel(*channels.rotation, 4, time, value)) {
                    // An erhe rotation channel keys (x, y, z, w).
                    rotation = glm::normalize(glm::quat{value.w, value.x, value.y, value.z});
                }
                if ((channels.scale != nullptr) && sample_channel(*channels.scale, 3, time, value)) {
                    scale = glm::vec3{value};
                }
                translation_values.push_back(lightusd::value::float3{translation.x, translation.y, translation.z});
                rotation_values.push_back(lightusd::value::quatf{{rotation.x, rotation.y, rotation.z}, rotation.w});
                scale_values.push_back(
                    lightusd::value::half3{to_usd_half(scale.x), to_usd_half(scale.y), to_usd_half(scale.z)}
                );
            }
            translations.add_sample(time_code, translation_values);
            rotations.add_sample(time_code, rotation_values);
            scales.add_sample(time_code, scale_values);

            m_first_time_code    = m_wrote_time_samples ? std::min(m_first_time_code, time_code) : time_code;
            m_last_time_code     = m_wrote_time_samples ? std::max(m_last_time_code,  time_code) : time_code;
            m_wrote_time_samples = true;
        }
        animation_prim.translations.set_value(std::move(translations));
        animation_prim.rotations.set_value(std::move(rotations));
        animation_prim.scales.set_value(std::move(scales));
        return lightusd::Prim{animation_prim};
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
        // The joint influences of every point, `c_joint_slots` per point, and
        // whether they are being collected at all: a skinned mesh writes them
        // as `vertex` primvars indexed by the point index, which is the domain
        // erhe carries them in (doc/usd-compatibility-plan.md K1).
        std::vector<int32_t>                     joint_indices;
        std::vector<float>                       joint_weights;
        Skin_influences                          skin_influences{Skin_influences::skip};
        bool                                     has_normals  {false};
        bool                                     has_texcoords{false};
        bool                                     has_colors   {false};

        // One influence slot of the point being appended. A skinned point
        // gets exactly `c_joint_slots` of them, zero-weighted where erhe
        // carries no influence, so the arrays stay indexed by the point.
        void add_joint_influence(const int32_t index, const float weight)
        {
            joint_indices.push_back(index);
            joint_weights.push_back(weight);
        }
    };

    static void append_geometry(Mesh_accumulator& out, const erhe::geometry::Geometry& geometry)
    {
        const GEO::Mesh&                       mesh       = geometry.get_mesh();
        const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
        const std::size_t                      base       = out.points.size();

        for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
            const GEO::vec3f point = erhe::geometry::get_pointf(mesh.vertices, vertex);
            out.points.push_back(lightusd::value::point3f{point.x, point.y, point.z});
            if (out.skin_influences == Skin_influences::collect) {
                for (std::size_t set = 0; set < c_joint_sets; ++set) {
                    const erhe::geometry::Attribute_present<GEO::vec4u>& index_attribute =
                        (set == 0) ? attributes.vertex_joint_indices_0 : attributes.vertex_joint_indices_1;
                    const erhe::geometry::Attribute_present<GEO::vec4f>& weight_attribute =
                        (set == 0) ? attributes.vertex_joint_weights_0 : attributes.vertex_joint_weights_1;
                    const std::optional<GEO::vec4u> indices = index_attribute.try_get(vertex);
                    const std::optional<GEO::vec4f> weights = weight_attribute.try_get(vertex);
                    for (std::size_t component = 0; component < c_joints_per_set; ++component) {
                        const GEO::index_t slot = static_cast<GEO::index_t>(component);
                        out.add_joint_influence(
                            (indices.has_value() && weights.has_value()) ? static_cast<int32_t>(indices.value()[slot]) : 0,
                            (indices.has_value() && weights.has_value()) ? weights.value()[slot] : 0.0f
                        );
                    }
                }
            }
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

                // Back to USD's bottom-left `st` origin
                // (src/erhe/usd/notes.md, "Texture coordinates").
                const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
                const glm::vec2 st = texcoord.has_value()
                    ? flip_texcoord_v(glm::vec2{texcoord.value().x, texcoord.value().y})
                    : glm::vec2{0.0f, 0.0f};
                out.texcoords.push_back(
                    texcoord.has_value()
                        ? lightusd::value::texcoord2f{st.x, st.y}
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
            if (out.skin_influences == Skin_influences::collect) {
                for (std::size_t set = 0; set < c_joint_sets; ++set) {
                    const Vertex_attribute* joint_indices = stream.find_attribute(Vertex_attribute_usage::joint_indices, set);
                    const Vertex_attribute* joint_weights = stream.find_attribute(Vertex_attribute_usage::joint_weights, set);
                    glm::vec4               indices{0.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec4               weights{0.0f, 0.0f, 0.0f, 0.0f};
                    const bool              has_indices = read_soup_attribute(soup, joint_indices, stride, vertex_index, indices);
                    const bool              has_weights = read_soup_attribute(soup, joint_weights, stride, vertex_index, weights);
                    for (std::size_t component = 0; component < c_joints_per_set; ++component) {
                        const glm::length_t slot = static_cast<glm::length_t>(component);
                        out.add_joint_influence(
                            (has_indices && has_weights) ? static_cast<int32_t>(indices[slot]) : 0,
                            (has_indices && has_weights) ? weights[slot] : 0.0f
                        );
                    }
                }
            }
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
                const bool      has_texcoord = read_soup_attribute(soup, texcoord, stride, vertex_index, value);
                const glm::vec2 st           = has_texcoord
                    ? flip_texcoord_v(glm::vec2{value.x, value.y})
                    : glm::vec2{0.0f, 0.0f};
                out.texcoords.push_back(
                    has_texcoord
                        ? lightusd::value::texcoord2f{st.x, st.y}
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

    // The vertex arrays of one accumulator on a GeomMesh, with the
    // interpolation each of them is authored at.
    void fill_geom_mesh(lightusd::GeomMesh& geom_mesh, const Mesh_accumulator& accumulator)
    {
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
    }

    // One erhe geometry as one `Mesh` prim of its own: no transform, no
    // material binding and no item behind it - what a brush's geometry child
    // is (doc/usd-compatibility-plan.md E4a). The authored polygons are the
    // mesh rather than a subdivision cage, which is what makes the importer
    // build erhe geometry back rather than a triangle soup. An erhe geometry
    // binds no material of its own, so the prim carries no GeomSubset: a
    // material a brush hands on is the brush prim's binding.
    [[nodiscard]] auto write_geometry_mesh_prim(const erhe::geometry::Geometry& geometry, const std::string& prim_name) -> lightusd::Prim
    {
        ++m_mesh_count;
        lightusd::GeomMesh geom_mesh;
        geom_mesh.name = prim_name;
        geom_mesh.subdivisionScheme.set_value(lightusd::GeomMesh::SubdivisionScheme::SubdivisionSchemeNone);

        Mesh_accumulator accumulator;
        append_geometry(accumulator, geometry);
        fill_geom_mesh(geom_mesh, accumulator);
        return lightusd::Prim{geom_mesh};
    }

    [[nodiscard]] auto write_mesh_prim(
        const erhe::scene::Node& node,
        const erhe::scene::Mesh& mesh,
        const std::string&       prim_name,
        const glm::mat4&         matrix,
        const Instance_root_override& override_root
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
        accumulator.skin_influences = mesh.skin ? Skin_influences::collect : Skin_influences::skip;
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

        fill_geom_mesh(geom_mesh, accumulator);

        write_visibility_and_purpose(node, geom_mesh);
        write_double_sided(mesh, geom_mesh);
        write_erhe_properties(node, geom_mesh);
        write_erhe_properties(mesh, geom_mesh);
        write_instance_root_override(node, override_root, geom_mesh);

        // One primitive needs no subset: the importer's remainder group is
        // the whole mesh and reproduces it. Several primitives become one
        // materialBind GeomSubset each.
        const bool mesh_bound = (groups.size() == 1) && bind_material(geom_mesh, groups.front().material);
        const bool skin_bound = write_skin_binding(mesh, accumulator, geom_mesh);
        lightusd::Prim prim{geom_mesh};
        if (mesh_bound) {
            apply_material_binding_api(prim);
        }
        if (skin_bound) {
            apply_api_schema(prim.metas(), lightusd::APISchemas::APIName::SkelBindingAPI, std::string{});
        }
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
                const bool subset_bound = bind_material(subset, group.material);
                lightusd::Prim subset_prim{subset};
                if (subset_bound) {
                    apply_material_binding_api(subset_prim);
                }
                std::string error;
                if (!prim.add_child(std::move(subset_prim), false, &error)) {
                    add_warning(fmt::format("subset '{}' of '{}' could not be added: {}", subset.name, prim_name, error));
                }
            }
        }
        return prim;
    }

    // The `SkelBindingAPI` of one skinned mesh (doc/usd-compatibility-plan.md
    // K1): the skeleton it binds, the influences its vertices carry and the
    // geometry bind transform the skin was written with. The influences are
    // `vertex` primvars indexed by the point index, which is the domain the
    // accumulator filled them in. Reports whether the schema was applied.
    [[nodiscard]] auto write_skin_binding(
        const erhe::scene::Mesh& mesh,
        const Mesh_accumulator&  accumulator,
        lightusd::GeomMesh&      geom_mesh
    ) -> bool
    {
        const erhe::scene::Skin* skin = mesh.skin.get();
        if (skin == nullptr) {
            return false;
        }
        const erhe::scene::Node* skeleton = skin->skin_data.skeleton.get();
        if (skeleton == nullptr) {
            return false;
        }
        const std::map<const erhe::Item_base*, std::string>::const_iterator i = m_skeleton_paths.find(skeleton);
        if (i == m_skeleton_paths.end()) {
            add_warning(
                fmt::format(
                    "mesh '{}' binds skeleton '{}', which is not a prim of the file - the mesh is written unskinned",
                    mesh.get_name(), skeleton->get_name()
                )
            );
            return false;
        }
        lightusd::Relationship relationship;
        relationship.set(lightusd::Path{i->second, ""});
        geom_mesh.props.emplace("skel:skeleton", lightusd::Property{std::move(relationship), false});

        const std::map<const erhe::scene::Skin*, glm::mat4>::const_iterator geometry_from_bind =
            m_skin_geom_bind_transforms.find(skin);
        if ((geometry_from_bind != m_skin_geom_bind_transforms.end()) && !is_identity(geometry_from_bind->second)) {
            lightusd::Attribute attribute;
            attribute.set_value(to_usd(geometry_from_bind->second));
            geom_mesh.props.emplace("primvars:skel:geomBindTransform", lightusd::Property{std::move(attribute), false});
        }

        // The influences erhe carries per vertex, with the trailing slots no
        // vertex weights dropped: erhe pads a vertex out to the width of the
        // sets it fills and reading a narrower `elementSize` back pads it
        // again, so the narrowest width that holds every influence is what
        // makes a second save byte-identical.
        const std::size_t point_count = std::min(accumulator.points.size(), accumulator.joint_weights.size() / c_joint_slots);
        std::size_t       element_size{1};
        for (std::size_t point = 0; point < point_count; ++point) {
            for (std::size_t slot = 0; slot < c_joint_slots; ++slot) {
                if (accumulator.joint_weights[(point * c_joint_slots) + slot] > 0.0f) {
                    element_size = std::max(element_size, slot + 1);
                }
            }
        }
        std::vector<int32_t> indices;
        std::vector<float>   weights;
        indices.reserve(point_count * element_size);
        weights.reserve(point_count * element_size);
        for (std::size_t point = 0; point < point_count; ++point) {
            for (std::size_t slot = 0; slot < element_size; ++slot) {
                indices.push_back(accumulator.joint_indices[(point * c_joint_slots) + slot]);
                weights.push_back(accumulator.joint_weights[(point * c_joint_slots) + slot]);
            }
        }
        add_vertex_primvar(geom_mesh, "primvars:skel:jointIndices", indices, element_size);
        add_vertex_primvar(geom_mesh, "primvars:skel:jointWeights", weights, element_size);
        return true;
    }

    template <typename T, typename V>
    static void add_vertex_primvar(
        T&                    typed_prim,
        const std::string&    name,
        const std::vector<V>& values,
        const std::size_t     element_size
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(values);
        attribute.metas().set_interpolation_enum(lightusd::Interpolation::Vertex);
        attribute.metas().set_elementSize(static_cast<uint32_t>(element_size));
        typed_prim.props.emplace(name, lightusd::Property{std::move(attribute), false});
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
        const Instance_root_override& override_root
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
        const Instance_root_override& override_root
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
            apply_api_schema(prim, lightusd::APISchemas::APIName::ShapingAPI, std::string{});
        }
        return prim;
    }

    // One API schema applied to a prim. USD spells an instance of a
    // multi-apply schema as `Schema:instance`, so `instance_name` is that
    // instance and stays empty for a single-apply schema.
    static void apply_api_schema(
        lightusd::PrimMeta&                 meta,
        const lightusd::APISchemas::APIName name,
        const std::string&                  instance_name
    )
    {
        lightusd::APISchemas& schemas = meta.get_apiSchemas_mutable();
        schemas.names.emplace_back(name, instance_name);
    }

    static void apply_api_schema(
        lightusd::Prim&                     prim,
        const lightusd::APISchemas::APIName name,
        const std::string&                  instance_name
    )
    {
        apply_api_schema(prim.metas(), name, instance_name);
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
    // What every brush prim of the tree holds, by the item the caller named
    // (doc/usd-compatibility-plan.md E4a).
    std::map<const erhe::Item_base*, const Usd_save_brush*>                  m_brushes;

    // What every texture node graph prim of the tree holds, by the item the
    // caller named, and where each one landed on the stage.
    std::map<const erhe::Item_base*, const Usd_save_node_graph*>             m_node_graphs;
    std::map<const erhe::Item_base*, std::string>                           m_node_graph_paths;
    // The point instancers of the scene, and the prims that are their
    // expansion (doc/usd-compatibility-plan.md S1): an instance prim is not
    // planned, and an instancer reads its instances' transforms back out of
    // the tree.
    std::map<const erhe::Item_base*, const Usd_save_point_instancer*>        m_point_instancers;
    std::set<const erhe::Item_base*>                                         m_point_instancer_instances;
    // The variant sets the caller named, by the item carrying them; one item
    // can carry more than one set (doc/usd-compatibility-plan.md X4).
    std::map<const erhe::Item_base*, std::vector<const Usd_save_variant_set*>> m_prim_variant_sets;
    // Which variant of which set each prim a variant adds belongs to, by the
    // item: what tells plan_children to write it inside the block instead of
    // among the carrying prim's children.
    std::map<const erhe::Item_base*, Variant_prim_membership>                  m_variant_prims;
    // The skeletons of the skins the tree's meshes bind, by the prim that is
    // a skin's pivot, and where each of those prims landed on the stage so a
    // skinned mesh names it by path (doc/usd-compatibility-plan.md K1).
    std::map<const erhe::Item_base*, Skeleton_record>                          m_skeletons;
    std::map<const erhe::Item_base*, std::string>                              m_skeleton_paths;
    // The geometry bind transform each skin is written with.
    std::map<const erhe::scene::Skin*, glm::mat4>                              m_skin_geom_bind_transforms;
    // Every joint of every skeleton: a joint is an entry of the skeleton's
    // `joints` rather than a prim of its own, so it is not planned.
    std::set<const erhe::Item_base*>                                           m_joint_items;
    // The `SkelAnimation` prims of the tree: the writer rebuilds each of them
    // from the joint channels, so the prim the import made is not planned.
    std::set<const erhe::Item_base*>                                           m_skel_animation_items;
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

auto plan_usd_prim_paths(const Usd_save_arguments& arguments) -> std::map<const erhe::Item_base*, std::string>
{
    ERHE_PROFILE_FUNCTION();

    std::map<const erhe::Item_base*, std::string> paths;
    Usd_save_result                               result{};
    Exporter                                      exporter{arguments, result};
    exporter.collect_planned_paths(paths);
    return paths;
}

auto get_usd_authored_as(const std::string_view owner, const std::string_view name, const Native_property_form form) -> std::string
{
    const std::string_view native = native_usd_property_name(owner, name, form);
    if (!native.empty()) {
        return std::string{native};
    }
    return fmt::format("erhe:{}:{}", owner, name);
}

} // namespace erhe::usd
