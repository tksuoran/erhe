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
#include "usdPhysics.hh"
#include "usdSkel.hh"
#include "pprint-enum.hh"

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
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

// Whether an attribute a physics record writes is one the schema declares -
// which USD spells without `custom` - or an erhe-only one, which is a custom
// attribute.
enum class Attribute_form : unsigned int {
    schema_attribute = 0,
    custom_attribute = 1
};

// Which of the six degrees of freedom a joint limit or drive names.
enum class Limit_axis_kind : unsigned int {
    linear  = 0,
    angular = 1
};

// No physics record for this prim.
constexpr std::size_t c_no_physics_index = ~std::size_t{0};

// One prim a body adds below its own for a collider the tree holds no prim
// for: the synthesized collider it is and the sibling-unique name the plan
// gave it.
class Plan_physics_child final
{
public:
    std::size_t index{0};
    std::string name;
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
    // The physics this prim carries (doc/usd_compatibility.md, "Physics"):
    // the body description it is the prim of, and the names the plan gave the
    // prims that body adds below it.
    std::size_t                      physics_body{c_no_physics_index};
    std::string                      physics_collider_name;
    std::string                      physics_joint_name;
    std::vector<Plan_physics_child>  physics_synthesized;
    // The prims a compound trigger names, each written as a collider prim
    // below this one; `index` is the body entry the member's shape is on.
    std::vector<Plan_physics_child>  physics_trigger_members;
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
// The three transform channels of `Usd_save_arguments::animations` that drive
// one node: what the writer reconciles a sampled `xformOp` stack with
// (src/erhe/usd/notes.md, "Time samples").
class Node_transform_channels final
{
public:
    const erhe::scene::Animation_sampler* translation{nullptr};
    const erhe::scene::Animation_sampler* rotation   {nullptr};
    const erhe::scene::Animation_sampler* scale      {nullptr};
};

// The channels of `Usd_save_arguments::animations` that drive one item's
// non-xformOp attributes: the closed list of attributes a save carries
// beyond the transform (src/erhe/usd/notes.md, "Time samples").
class Item_attribute_channels final
{
public:
    const erhe::scene::Animation_sampler* visible   {nullptr};
    const erhe::scene::Animation_sampler* intensity {nullptr};
    const erhe::scene::Animation_sampler* color     {nullptr};
    const erhe::scene::Animation_sampler* base_color{nullptr};
    const erhe::scene::Animation_sampler* roughness {nullptr};
    const erhe::scene::Animation_sampler* metallic  {nullptr};
    const erhe::scene::Animation_sampler* opacity   {nullptr};
};

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

        index_physics();

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

        // The transform channels an edited clip holds, before any prim is
        // written: a sampled stack is reconciled with them
        // (src/erhe/usd/notes.md, "Time samples").
        collect_transform_channels();
        collect_attribute_channels();

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
        // Where every prim of the tree landed, which is what a physics record
        // names its material, its filter and the prims a joint joins by.
        m_planned_paths.clear();
        collect_planned_paths_of(plan, m_planned_paths);
        plan_physics_scene(plan, out_plan);
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
            if (!m_physics_scene_name.empty() && (m_physics_scene_parent_path == fmt::format("/{}", c_world_prim_name))) {
                std::string error;
                if (!world_prim.add_child(write_physics_scene_prim(), false, &error)) {
                    add_warning(fmt::format("the PhysicsScene prim could not be added: {}", error));
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

    // The transform channels of the animations the caller handed over, by
    // the node each drives, and the prims whose edited clip the authored ops
    // could not hold ("Reconciling an edited clip with the authored ops").
    std::map<const erhe::scene::Node*, Node_transform_channels> m_transform_channels;

    // The channels driving a non-xformOp attribute, by the item each drives
    // (src/erhe/usd/notes.md, "Time samples").
    std::map<const erhe::Item_base*, Item_attribute_channels>    m_attribute_channels;
    std::set<const erhe::scene::Node*>                          m_write_back_refusals;
    bool                                                        m_warned_interpolation{false};

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
        // A clip driving `visible` writes the token samples beside that
        // value (src/erhe/usd/notes.md, "Time samples").
        write_sampled_attribute<lightusd::Visibility>(
            typed_prim.visibility, get_attribute_channels(item).visible, 1,
            [](const glm::vec4& value) -> lightusd::Visibility {
                return (value.x != 0.0f) ? lightusd::Visibility::Inherited : lightusd::Visibility::Invisible;
            }
        );
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

    // The exact anisotropic roughness on the material prim. Neither terminal
    // carries the pair as erhe holds it - the preview surface has one value,
    // and the OpenPBR parameterization spells only the pairs whose X
    // component is the rougher one - so the material authors its own
    // `erhe:Material:roughness`, which a reload applies after the network
    // (I2). The network is what another reader shades with; this is what
    // makes the erhe round trip bit-exact.
    void write_exact_roughness(const erhe::primitive::Material& material, lightusd::Material& usd_material)
    {
        // Only a local roughness: a value a style or an inheritance supplies is
        // that chain's to give, and authoring it here would make the reload
        // hold a local value the material never had. Such a material carries
        // its roughness in the network alone.
        if (!is_local(material, erhe::primitive::Material::roughness_property.get())) {
            return;
        }
        const glm::vec2 roughness = material.get_value(erhe::primitive::Material::roughness_property);
        lightusd::Attribute        attribute;
        lightusd::primvar::PrimVar var;
        var.set_value(lightusd::value::float2{roughness.x, roughness.y});
        attribute.set_var(std::move(var));
        const std::string attribute_name{"erhe:Material:roughness"};
        if (usd_material.props.find(attribute_name) != usd_material.props.end()) {
            return;
        }
        usd_material.props.emplace(attribute_name, lightusd::Property{std::move(attribute), true});
    }

    // Whether the material has a value the `UsdPreviewSurface` has no input
    // for: an anisotropic roughness (its two components differ, which is the
    // pair only OpenPBR carries) or a transmission. Such a material is written
    // with an OpenPBR network beside its preview surface; every other material
    // writes none, so a file of ordinary materials is the file it was
    // (doc/usd-compatibility-plan.md E2).
    [[nodiscard]] static auto needs_open_pbr_network(const erhe::primitive::Material& material) -> bool
    {
        using erhe::primitive::Material;
        const glm::vec2 roughness = material.get_value(Material::roughness_property);
        return (roughness.x != roughness.y) || (material.get_value(Material::transmission_property) != 0.0f);
    }

    [[nodiscard]] static auto make_float_input(const float value) -> lightusd::Attribute
    {
        lightusd::Attribute attribute;
        attribute.set_value(value);
        return attribute;
    }

    [[nodiscard]] static auto make_color_input(const glm::vec3& value) -> lightusd::Attribute
    {
        lightusd::Attribute attribute;
        attribute.set_value(lightusd::value::color3f{value.x, value.y, value.z});
        return attribute;
    }

    // The source one OpenPBR input reads: the very prim the preview surface's
    // input of that slot reads - the material's own `UsdUVTexture` for the
    // slot, or the interface output of the texture graph that feeds it - so a
    // material with both terminals has one shading network, not two.
    [[nodiscard]] auto connect_open_pbr_texture(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material,
        const Usd_material_texture_slot  slot,
        const char*                      output_name,
        const char*                      type_name,
        lightusd::Attribute&             attribute
    ) -> bool
    {
        lightusd::Path graph_output;
        if (find_graph_output(material, slot, graph_output)) {
            attribute.set_type_name(type_name);
            attribute.set_connection(graph_output);
            return true;
        }
        const std::string shader_name = get_texture_shader(material_prim, material_path, material, slot);
        if (shader_name.empty()) {
            return false;
        }
        attribute.set_type_name(type_name);
        attribute.set_connection(lightusd::Path{material_path + "/" + shader_name, output_name});
        return true;
    }

    // The OpenPBR network of a material whose anisotropic roughness or
    // transmission the `UsdPreviewSurface` cannot carry: one generic `Shader`
    // prim the material offers through `outputs:mtlx:surface`, which is the
    // terminal the importer prefers and the one another MaterialX reader
    // finds. The values are the material's effective ones, and an input is
    // authored where it differs from its OpenPBR fallback - except
    // `base_color` and `specular_roughness`, whose fallbacks are not erhe
    // defaults, so they are authored whatever the value is (the mirror of the
    // import rule, `src/erhe/usd/notes.md`, "OpenPBR networks").
    void write_open_pbr_shader(
        lightusd::Prim&                  material_prim,
        const std::string&               material_path,
        const erhe::primitive::Material& material
    )
    {
        using erhe::primitive::Material;
        lightusd::ShaderNode shader_node;
        const auto author = [&shader_node](const std::string_view name, lightusd::Attribute attribute)
        {
            shader_node.props.emplace(
                std::string{c_node_graph_input_prefix} + std::string{name},
                lightusd::Property{std::move(attribute), false}
            );
        };

        const glm::vec2          erhe_roughness = material.get_value(Material::roughness_property);
        const Open_pbr_roughness roughness      = from_anisotropic_roughness(erhe_roughness);
        if (erhe_roughness.y > erhe_roughness.x) {
            add_warning(
                fmt::format(
                    "material '{}' is rougher along Y than along X, which no OpenPBR anisotropy spells - "
                    "the network carries the X roughness alone",
                    material.get_name()
                )
            );
        }

        // base_color, the emission and the two scalars of the shared
        // metallic-roughness slot read a texture where the material has one,
        // exactly as the preview surface's inputs do; otherwise they carry the
        // plain value.
        lightusd::Attribute base_color_input;
        if (!connect_open_pbr_texture(
                material_prim, material_path, material, Usd_material_texture_slot::base_color,
                "outputs:rgb", "color3f", base_color_input
            )
        ) {
            base_color_input = make_color_input(material.get_value(Material::base_color_property));
        }
        author("base_color", std::move(base_color_input));

        lightusd::Attribute roughness_input;
        if ((material.get_roughness_channel() == erhe::primitive::Texture_channel::none) ||
            !connect_open_pbr_texture(
                material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness,
                channel_output_name(material.get_roughness_channel()), "float", roughness_input
            )
        ) {
            roughness_input = make_float_input(roughness.roughness);
        }
        author("specular_roughness", std::move(roughness_input));
        if (roughness.anisotropy != 0.0f) {
            author("specular_roughness_anisotropy", make_float_input(roughness.anisotropy));
        }

        const float         metallic = material.get_value(Material::metallic_property);
        lightusd::Attribute metallic_input;
        if ((material.get_metallic_channel() != erhe::primitive::Texture_channel::none) &&
            connect_open_pbr_texture(
                material_prim, material_path, material, Usd_material_texture_slot::metallic_roughness,
                channel_output_name(material.get_metallic_channel()), "float", metallic_input
            )
        ) {
            author("base_metalness", std::move(metallic_input));
        } else if (metallic != 0.0f) {
            author("base_metalness", make_float_input(metallic));
        }

        // 1.5 is the fallback of both OpenPBR's `specular_ior` and erhe's own
        // `ior`, so a material at it authors nothing.
        const float ior = material.get_value(Material::ior_property);
        if (ior != 1.5f) {
            author("specular_ior", make_float_input(ior));
        }
        const float transmission = material.get_value(Material::transmission_property);
        if (transmission != 0.0f) {
            author("transmission_weight", make_float_input(transmission));
        }

        // erhe's `emissive` is the whole linear value the shader adds, and
        // OpenPBR's emission is a color times a luminance, so the color
        // carries the value and the luminance is one - which is what the
        // importer's `emission_color * emission_luminance` reads back.
        const glm::vec3     emissive = material.get_value(Material::emissive_property);
        lightusd::Attribute emission_input;
        if (connect_open_pbr_texture(
                material_prim, material_path, material, Usd_material_texture_slot::emissive,
                "outputs:rgb", "color3f", emission_input
            )
        ) {
            author("emission_color",     std::move(emission_input));
            author("emission_luminance", make_float_input(1.0f));
        } else if (emissive != glm::vec3{0.0f, 0.0f, 0.0f}) {
            author("emission_color",     make_color_input(emissive));
            author("emission_luminance", make_float_input(1.0f));
        }

        const float opacity = material.get_value(Material::opacity_property);
        if (opacity != 1.0f) {
            author("geometry_opacity", make_float_input(opacity));
        }

        lightusd::Attribute normal_input;
        if (connect_open_pbr_texture(
                material_prim, material_path, material, Usd_material_texture_slot::normal,
                "outputs:rgb", "normal3f", normal_input
            )
        ) {
            author("geometry_normal", std::move(normal_input));
        }

        lightusd::Attribute surface_output;
        surface_output.set_type_name("token");
        shader_node.props.emplace(
            std::string{c_node_graph_output_prefix} + "surface",
            lightusd::Property{std::move(surface_output), false}
        );

        lightusd::Shader shader;
        shader.name    = std::string{c_open_pbr_shader_prim_name};
        shader.info_id = std::string{c_open_pbr_info_id};
        shader.value   = std::move(shader_node);

        std::string error;
        if (!material_prim.add_child(lightusd::Prim{shader}, false, &error)) {
            add_warning(fmt::format("OpenPBR shader of '{}' could not be added: {}", material_path, error));
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
        // A material the preview surface cannot carry offers a second
        // terminal: the OpenPBR network, through the output the importer
        // prefers (doc/usd-compatibility-plan.md E2).
        const bool open_pbr = needs_open_pbr_network(material);
        if (open_pbr) {
            usd_material.mtlxSurface.set(
                lightusd::Path{plan_prim.path + "/" + std::string{c_open_pbr_shader_prim_name}, "outputs:surface"}
            );
        }
        write_erhe_properties(material, usd_material);
        if (open_pbr) {
            write_exact_roughness(material, usd_material);
        }

        lightusd::Prim material_prim{usd_material};
        warn_about_unresolved_textures(material);
        // The preview surface is written first: it makes the UsdUVTexture
        // prims of the material's slots, which the OpenPBR inputs then read
        // rather than writing a second set.
        write_surface_shader(material_prim, plan_prim.path, material);
        if (open_pbr) {
            write_open_pbr_shader(material_prim, plan_prim.path, material);
        }
        write_physics_material_on_prim(material, material_prim, plan_prim.path);
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
        // `metallic`, `roughness` and `opacity` are written below, beside the
        // keys of the clip driving them: a cubic clip writes the attribute as
        // a `Ts` spline carrying its own default.
        if (is_local(material, Material::ior_property.get())) {
            surface.ior.set_value(material.get_value(Material::ior_property));
        }
        if (is_local(material, Material::occlusion_texture_strength_property.get())) {
            surface.occlusion.set_value(material.get_value(Material::occlusion_texture_strength_property));
        }
        // The keys of a clip driving the material, beside those values
        // (src/erhe/usd/notes.md, "Time samples"). erhe's roughness is
        // anisotropic and UsdPreviewSurface has one, so a keyed roughness
        // writes the x component of each key, the way the plain value does.
        // A cubic clip writes a `Ts` spline as a property of the shader node
        // instead of the schema attribute, which is why the values of those
        // attributes are written here rather than above.
        const Item_attribute_channels channels = get_attribute_channels(material);
        write_sampled_attribute<lightusd::value::color3f>(
            surface.diffuseColor, channels.base_color, 3,
            [](const glm::vec4& value) -> lightusd::value::color3f {
                return lightusd::value::color3f{value.x, value.y, value.z};
            }
        );
        const std::optional<float> roughness = is_local(material, Material::roughness_property.get())
            ? std::optional<float>{material.get_value(Material::roughness_property).x}
            : std::optional<float>{};
        if (!write_spline_attribute(surface.props, "inputs:roughness", channels.roughness, 2, roughness)) {
            if (roughness.has_value()) {
                surface.roughness.set_value(roughness.value());
            }
            write_sampled_attribute<float>(
                surface.roughness, channels.roughness, 2,
                [](const glm::vec4& value) -> float { return value.x; }
            );
        }
        const std::optional<float> metallic = is_local(material, Material::metallic_property.get())
            ? std::optional<float>{material.get_value(Material::metallic_property)}
            : std::optional<float>{};
        if (!write_spline_attribute(surface.props, "inputs:metallic", channels.metallic, 1, metallic)) {
            if (metallic.has_value()) {
                surface.metallic.set_value(metallic.value());
            }
            write_sampled_attribute<float>(
                surface.metallic, channels.metallic, 1,
                [](const glm::vec4& value) -> float { return value.x; }
            );
        }
        const std::optional<float> opacity = is_local(material, Material::opacity_property.get())
            ? std::optional<float>{material.get_value(Material::opacity_property)}
            : std::optional<float>{};
        if (!write_spline_attribute(surface.props, "inputs:opacity", channels.opacity, 1, opacity)) {
            if (opacity.has_value()) {
                surface.opacity.set_value(opacity.value());
            }
            write_sampled_attribute<float>(
                surface.opacity, channels.opacity, 1,
                [](const glm::vec4& value) -> float { return value.x; }
            );
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

    // Which children of a prim a pass plans: all of them, or only the ones a
    // variant block of that prim authored. A carrier's other children are the
    // instance content its arcs supply, which is not written, while a prim one
    // of its variant blocks authored is the variant's own content
    // (doc/usd-compatibility-plan.md C6).
    enum class Child_selection : unsigned int {
        all_children       = 0,
        variant_prims_only = 1
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
        const Prim_holder               holder            = Prim_holder::tree,
        const Child_selection           selection         = Child_selection::all_children
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
                    pre_transform * child_node->authored_parent_from_node_transform().get_matrix(),
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
                    pre_transform * child_node->authored_parent_from_node_transform().get_matrix(),
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
            if ((selection == Child_selection::variant_prims_only) && (membership == nullptr)) {
                continue;
            }
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
                // The prims the carrier's own variant blocks authored are
                // written back inside those blocks; everything else below a
                // carrier is instance content the arcs supply.
                Name_scope             carrier_names;
                std::vector<Plan_prim> instance_content;
                plan_children(
                    *child_prim,
                    (child_node != nullptr) ? glm::mat4{1.0f} : pre_transform,
                    carrier_names,
                    instance_content,
                    &plan_prim.variant_prims,
                    child_prim_holder(*child_prim, holder),
                    Child_selection::variant_prims_only
                );
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
                // The collider and joint prims the body on this prim adds
                // take their names from the same scope its children took
                // theirs from.
                plan_physics_children(plan_prim, child_names);
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
    // A physics material, a collision filter and a joint-settings item are
    // library resources: they carry no content flag, and the prim each one is
    // is what the physics record names, so the plan holds them the way it
    // holds a material or a brush.
    [[nodiscard]] auto is_physics_resource_prim(const erhe::Typed& prim) const -> bool
    {
        return
            (m_physics_material_record.count(&prim) != 0) ||
            (m_physics_filter_record.count(&prim) != 0)   ||
            (m_physics_settings_record.count(&prim) != 0);
    }

    [[nodiscard]] auto is_carried_without_content_flag(const erhe::Typed& prim) const -> bool
    {
        if (
            erhe::is<erhe::Scope>(&prim)               ||
            erhe::is<erhe::primitive::Material>(&prim) ||
            is_style_prim(prim)                        ||
            is_brush_prim(prim)                        ||
            is_node_graph_prim(prim)                   ||
            is_physics_resource_prim(prim)
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

        write_physics_on_prim(plan_prim, prim);
        apply_defined_specifier(*plan_prim.item, prim);

        if (plan_prim.references != nullptr) {
            write_references(prim.metas(), *plan_prim.references);
        }
        write_inherits(prim, *plan_prim.item);
        write_variant_sets(prim, plan_prim);

        std::vector<lightusd::Prim> child_prims;
        write_plan_prims(plan_prim.children, child_prims);
        write_override_prims(plan_prim, child_prims);
        write_physics_child_prims(plan_prim, child_prims);
        if (!m_physics_scene_name.empty() && (plan_prim.path == m_physics_scene_parent_path)) {
            child_prims.push_back(write_physics_scene_prim());
        }
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
        set_transform(ops, *node, node->authored_parent_from_node_transform().get_matrix());
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
        // The composition arcs the variant block authors, back inside the block
        // they came from rather than on the prim carrying the set
        // (doc/usd-compatibility-plan.md C6).
        if (!variant.references.empty()) {
            write_references(usd_variant.metas(), variant.references);
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
    void write_references(lightusd::PrimMetas& metas, const std::vector<Usd_save_reference>& references)
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
        if (m_physics_material_record.count(plan_prim.item) != 0) {
            return write_physics_material_prim(*plan_prim.item, plan_prim.name, plan_prim.path);
        }
        if (m_physics_filter_record.count(plan_prim.item) != 0) {
            return write_collision_group_prim(*plan_prim.item, plan_prim.name);
        }
        if (m_physics_settings_record.count(plan_prim.item) != 0) {
            return write_joint_settings_prim(*plan_prim.item, plan_prim.name);
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
        const glm::mat4          matrix    = plan_prim.pre_transform * node.authored_parent_from_node_transform().get_matrix();

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

    // ------------------------------------------------------------------
    // Reconciling an edited clip with the authored ops
    // (src/erhe/usd/notes.md, "Time samples")
    // ------------------------------------------------------------------

    // Index the transform channels of the animations the caller handed over
    // by the node each drives. A channel reading its sampler at a value
    // offset is left out: its keys are not the sampler's own, and the
    // write-back reads keys rather than resampling.
    // Index the channels driving the attributes a save carries beyond the
    // transform, by the item each drives (src/erhe/usd/notes.md, "Time
    // samples"). A channel reading its sampler at a value offset is left out
    // for the reason collect_transform_channels() gives, and a channel
    // driving any other property is named in one warning per animation: USD
    // has no carrier for it in this writer.
    void collect_attribute_channels()
    {
        using erhe::primitive::Material;
        using erhe::scene::Light;
        for (const std::shared_ptr<erhe::scene::Animation>& animation : m_arguments.animations) {
            if (!animation) {
                continue;
            }
            std::set<std::string> skipped_property_names;
            for (const erhe::scene::Animation_channel& channel : animation->channels) {
                if (
                    !channel.target ||
                    (channel.property == nullptr) ||
                    (channel.sampler_index >= animation->samplers.size())
                ) {
                    continue;
                }
                if (erhe::scene::get_animation_path(channel) != erhe::scene::Animation_path::INVALID) {
                    continue; // a transform channel; collect_transform_channels() has it
                }
                const erhe::scene::Animation_sampler&  sampler = animation->samplers[channel.sampler_index];
                // A cubic sampler keys [in tangent, value, out tangent], so a
                // channel of its own keys reads it one value in; any other
                // offset is a channel reading someone else's keys, which is
                // left out for the reason collect_transform_channels() gives.
                const bool is_cubic = (sampler.interpolation_mode == erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
                const std::size_t own_value_offset = is_cubic ? erhe::scene::get_component_count(channel) : std::size_t{0};
                if (channel.value_offset != own_value_offset) {
                    continue;
                }
                Item_attribute_channels&               entry   = m_attribute_channels[channel.target.get()];
                const erhe::scene::Animation_sampler** target   = nullptr;
                if (channel.property == erhe::Item_base::visible_property.get_ptr()) {
                    target = &entry.visible;
                } else if (channel.property == Light::intensity_property.get_ptr()) {
                    target = &entry.intensity;
                } else if (channel.property == Light::color_property.get_ptr()) {
                    target = &entry.color;
                } else if (channel.property == Material::base_color_property.get_ptr()) {
                    target = &entry.base_color;
                } else if (channel.property == Material::roughness_property.get_ptr()) {
                    target = &entry.roughness;
                } else if (channel.property == Material::metallic_property.get_ptr()) {
                    target = &entry.metallic;
                } else if (channel.property == Material::opacity_property.get_ptr()) {
                    target = &entry.opacity;
                } else {
                    skipped_property_names.insert(std::string{channel.property->get_name()});
                    continue;
                }
                if (*target == nullptr) {
                    *target = &sampler;
                }
            }
            if (!skipped_property_names.empty()) {
                std::string names;
                for (const std::string& name : skipped_property_names) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += name;
                }
                add_warning(
                    fmt::format(
                        "animation '{}' drives {} - USD carries no time samples for that property here, so the channel is not written",
                        animation->get_name(), names
                    )
                );
            }
        }
    }

    // The channels of one item, or an empty record.
    [[nodiscard]] auto get_attribute_channels(const erhe::Item_base& item) const -> Item_attribute_channels
    {
        const std::map<const erhe::Item_base*, Item_attribute_channels>::const_iterator i =
            m_attribute_channels.find(&item);
        return (i != m_attribute_channels.end()) ? i->second : Item_attribute_channels{};
    }

    // One schema attribute written with the samples of the channel driving it
    // (src/erhe/usd/notes.md, "Time samples"): one sample per key, at
    // `key time * timeCodesPerSecond`, beside the default the caller already
    // wrote. No authored sample record is kept for these attributes, so the
    // keys are always what a save writes.
    template <typename T, typename Value_of_key>
    void write_sampled_attribute(
        lightusd::TypedAttributeWithFallback<lightusd::Animatable<T>>& attribute,
        const erhe::scene::Animation_sampler*                          sampler,
        const std::size_t                                              component_count,
        Value_of_key&&                                                 value_of_key
    )
    {
        if ((sampler == nullptr) || (component_count == 0)) {
            return;
        }
        const std::size_t key_count = std::min(
            sampler->timestamps.size(),
            sampler->data.size() / component_count
        );
        if (key_count == 0) {
            return;
        }
        const double time_codes_per_second = (m_arguments.time_codes_per_second > 0.0)
            ? m_arguments.time_codes_per_second
            : 24.0;
        lightusd::Animatable<T> animatable;
        if (attribute.authored() && attribute.get_value().has_value()) {
            T scalar{};
            if (attribute.get_value().get_scalar(&scalar)) {
                animatable.set(scalar);
            }
        }
        for (std::size_t key = 0; key < key_count; ++key) {
            glm::vec4 value{0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t component = 0; component < component_count; ++component) {
                value[static_cast<glm::length_t>(component)] = sampler->data[(key * component_count) + component];
            }
            const double time_code = static_cast<double>(sampler->timestamps[key]) * time_codes_per_second;
            animatable.add_sample(time_code, value_of_key(value));
            m_first_time_code = m_wrote_time_samples ? std::min(m_first_time_code, time_code) : time_code;
            m_last_time_code  = m_wrote_time_samples ? std::max(m_last_time_code,  time_code) : time_code;
            m_wrote_time_samples = true;
        }
        attribute.set_value(animatable);
    }

    // Whether a channel's sampler is one erhe writes as a `Ts` spline rather
    // than as time samples (src/erhe/usd/notes.md, "Time samples").
    [[nodiscard]] static auto is_spline_sampler(const erhe::scene::Animation_sampler* sampler) -> bool
    {
        return (sampler != nullptr) &&
               (sampler->interpolation_mode == erhe::scene::Animation_interpolation_mode::CUBICSPLINE);
    }

    // One scalar schema attribute written as a `Ts` spline of its own property
    // (src/erhe/usd/notes.md, "Time samples"): a hermite spline of one knot
    // per key, at `key time * timeCodesPerSecond`, whose tangent slopes are
    // the key's tangents divided by that same rate - erhe keys value units per
    // second, USD value units per time code. The default beside it is the
    // item's own value, or the first key when the item authors none, so the
    // attribute still declares its type to a reader that has no spline.
    // Answers whether a spline was written; a caller whose channel is not
    // cubic writes the attribute the way it always did.
    [[nodiscard]] auto write_spline_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         attribute_name,
        const erhe::scene::Animation_sampler*      sampler,
        const std::size_t                          component_count,
        const std::optional<float>&                default_value
    ) -> bool
    {
        if (!is_spline_sampler(sampler) || (component_count == 0)) {
            return false;
        }
        const std::size_t stride    = 3 * component_count;
        const std::size_t key_count = std::min(sampler->timestamps.size(), sampler->data.size() / stride);
        if (key_count == 0) {
            return false;
        }
        const double time_codes_per_second = (m_arguments.time_codes_per_second > 0.0)
            ? m_arguments.time_codes_per_second
            : 24.0;
        lightusd::primvar::PrimVar::SplineData spline;
        spline.curveType         = 1; // hermite
        spline.preExtrapolation  = 1; // held, which is erhe's clamp to the end keys
        spline.postExtrapolation = 1;
        spline.knots.reserve(key_count);
        for (std::size_t key = 0; key < key_count; ++key) {
            const std::size_t base        = key * stride;
            const double      in_tangent  = static_cast<double>(sampler->data[base]);
            const double      value       = static_cast<double>(sampler->data[base + component_count]);
            const double      out_tangent = static_cast<double>(sampler->data[base + (2 * component_count)]);
            const double      time_code   = static_cast<double>(sampler->timestamps[key]) * time_codes_per_second;
            lightusd::primvar::PrimVar::SplineKnotData knot;
            knot.time              = time_code;
            knot.val               = lightusd::value::Value{value};
            knot.preTangentSlope   = in_tangent  / time_codes_per_second;
            knot.postTangentSlope  = out_tangent / time_codes_per_second;
            knot.interpolationMode = 3; // curve
            spline.knots.push_back(std::move(knot));
            m_first_time_code = m_wrote_time_samples ? std::min(m_first_time_code, time_code) : time_code;
            m_last_time_code  = m_wrote_time_samples ? std::max(m_last_time_code,  time_code) : time_code;
            m_wrote_time_samples = true;
        }
        const double first_knot_value = spline.knots.front().val.get_value<double>().value_or(0.0);
        const float  default_scalar   = default_value.value_or(static_cast<float>(first_knot_value));
        lightusd::primvar::PrimVar var;
        var.set_value(default_scalar);
        var.set_spline(std::move(spline));
        lightusd::Attribute attribute;
        attribute.set_var(std::move(var));
        props.emplace(attribute_name, lightusd::Property{std::move(attribute), false});
        return true;
    }

    void collect_transform_channels()
    {
        for (const std::shared_ptr<erhe::scene::Animation>& animation : m_arguments.animations) {
            if (!animation) {
                continue;
            }
            for (const erhe::scene::Animation_channel& channel : animation->channels) {
                const std::shared_ptr<erhe::scene::Node> target_node = erhe::scene::get_target_node(channel);
                if (
                    !target_node ||
                    (channel.sampler_index >= animation->samplers.size()) ||
                    (channel.value_offset != 0)
                ) {
                    continue;
                }
                const erhe::scene::Animation_path path = erhe::scene::get_animation_path(channel);
                if (path == erhe::scene::Animation_path::INVALID) {
                    continue;
                }
                Node_transform_channels&               entry   = m_transform_channels[target_node.get()];
                const erhe::scene::Animation_sampler&  sampler = animation->samplers[channel.sampler_index];
                const erhe::scene::Animation_sampler** target  =
                    (path == erhe::scene::Animation_path::TRANSLATION) ? &entry.translation :
                    (path == erhe::scene::Animation_path::ROTATION)    ? &entry.rotation    :
                                                                         &entry.scale;
                if (*target == nullptr) {
                    *target = &sampler;
                }
            }
        }
    }

    // How many keys a channel's sampler holds, counting only those its data
    // array covers.
    [[nodiscard]] static auto get_key_count(
        const erhe::scene::Animation_sampler& sampler,
        const std::size_t                     component_count
    ) -> std::size_t
    {
        return std::min(
            sampler.timestamps.size(),
            (component_count > 0) ? (sampler.data.size() / component_count) : std::size_t{0}
        );
    }

    [[nodiscard]] static auto get_key_value(
        const erhe::scene::Animation_sampler& sampler,
        const std::size_t                     component_count,
        const std::size_t                     key
    ) -> glm::vec4
    {
        glm::vec4 value{0.0f, 0.0f, 0.0f, 0.0f};
        for (std::size_t component = 0; component < component_count; ++component) {
            value[static_cast<glm::length_t>(component)] = sampler.data[(key * component_count) + component];
        }
        return value;
    }

    // Two values are the same when they differ by less than one part in 1e5.
    // An unedited clip's keys and the samples they were read from travelled
    // through one float conversion, so the tolerance only has to absorb a
    // reload; an edit smaller than it is an edit a save does not carry.
    [[nodiscard]] static auto is_near_value(const double lhs, const double rhs) -> bool
    {
        constexpr double tolerance = 1e-5;
        return std::abs(lhs - rhs) <= (tolerance * std::max(1.0, std::max(std::abs(lhs), std::abs(rhs))));
    }

    [[nodiscard]] static auto is_near_vector(const glm::dvec3 lhs, const glm::dvec3 rhs) -> bool
    {
        return is_near_value(lhs.x, rhs.x) && is_near_value(lhs.y, rhs.y) && is_near_value(lhs.z, rhs.z);
    }

    // Two quaternions are the same rotation when they agree up to sign: the
    // reader keeps a channel's sampled quaternions on one hemisphere, and
    // which one it picked says nothing about the value.
    [[nodiscard]] static auto is_near_rotation(const glm::dquat lhs, glm::dquat rhs) -> bool
    {
        if (glm::dot(lhs, rhs) < 0.0) {
            rhs = -rhs;
        }
        return is_near_value(lhs.x, rhs.x) && is_near_value(lhs.y, rhs.y) &&
               is_near_value(lhs.z, rhs.z) && is_near_value(lhs.w, rhs.w);
    }

    // The rotation a channel key holds. An erhe rotation channel keys
    // (x, y, z, w).
    [[nodiscard]] static auto get_key_rotation(const glm::vec4& key) -> glm::dquat
    {
        return glm::dquat{
            static_cast<double>(key.w),
            static_cast<double>(key.x),
            static_cast<double>(key.y),
            static_cast<double>(key.z)
        };
    }

    [[nodiscard]] auto get_time_codes_per_second() const -> double
    {
        return (m_arguments.time_codes_per_second > 0.0) ? m_arguments.time_codes_per_second : 24.0;
    }

    [[nodiscard]] auto get_time_code(const float time) const -> double
    {
        return static_cast<double>(time) * get_time_codes_per_second();
    }

    // A rotation in the form the op's own type takes. False when the type is
    // a single-axis rotate and the rotation turns about another axis too -
    // the op cannot hold it.
    [[nodiscard]] static auto make_rotation_op_value(
        const erhe::scene::Xform_op_type type,
        const glm::dquat                 rotation,
        erhe::scene::Xform_op_value&     out_value
    ) -> bool
    {
        using Op = erhe::scene::Xform_op_type;
        if (type == Op::orient) {
            out_value = rotation;
            return true;
        }
        if ((type == Op::rotate_x) || (type == Op::rotate_y) || (type == Op::rotate_z)) {
            const glm::dvec3 angles = erhe::scene::euler_degrees_from_rotation(Op::rotate_xyz, rotation);
            const int        axis   = (type == Op::rotate_x) ? 0 : (type == Op::rotate_y) ? 1 : 2;
            for (int component = 0; component < 3; ++component) {
                if ((component != axis) && (std::abs(angles[component]) > 1e-3)) {
                    return false;
                }
            }
            out_value = angles[axis];
            return true;
        }
        out_value = erhe::scene::euler_degrees_from_rotation(type, rotation);
        return true;
    }

    // What one op can hold of a matrix that is still to be accounted for:
    // the translation column, the column lengths, the orthonormalized basis,
    // or - for a `transform` op - the whole matrix.
    [[nodiscard]] static auto extract_op_value(
        const erhe::scene::Xform_op_type type,
        const glm::dmat4&                remaining,
        erhe::scene::Xform_op_value&     out_value
    ) -> bool
    {
        using Op = erhe::scene::Xform_op_type;
        switch (type) {
            case Op::transform: {
                out_value = remaining;
                return true;
            }
            case Op::translate: {
                out_value = glm::dvec3{remaining[3]};
                return true;
            }
            case Op::scale: {
                out_value = glm::dvec3{
                    glm::length(glm::dvec3{remaining[0]}),
                    glm::length(glm::dvec3{remaining[1]}),
                    glm::length(glm::dvec3{remaining[2]})
                };
                return true;
            }
            default: break;
        }
        glm::dmat3 basis{glm::dvec3{remaining[0]}, glm::dvec3{remaining[1]}, glm::dvec3{remaining[2]}};
        for (int column = 0; column < 3; ++column) {
            const double length = glm::length(basis[column]);
            if (length > 0.0) {
                basis[column] /= length;
            }
        }
        return make_rotation_op_value(type, glm::normalize(glm::quat_cast(basis)), out_value);
    }

    // Whether an op of a sampled stack is one the write-back may put a new
    // value into: an inverted or suffixed op is part of a construction the
    // stack authored around the sampled ones (a pivot pair), and an op that
    // carries no samples authored one value for the whole clip.
    [[nodiscard]] static auto is_write_back_op(const erhe::scene::Xform_op& op) -> bool
    {
        return !op.samples.empty() && !op.inverted && op.suffix.empty();
    }

    // The union of the time codes the stack's ops sample at, in increasing
    // order - the timeline the reader baked a stack it cannot drive op by op
    // onto.
    static void collect_stack_time_codes(const erhe::scene::Xform_op_stack& stack, std::vector<double>& out_time_codes)
    {
        out_time_codes.clear();
        for (const erhe::scene::Xform_op& op : stack.ops) {
            for (const erhe::scene::Xform_op_sample& sample : op.samples) {
                out_time_codes.push_back(sample.time_code);
            }
        }
        std::sort(out_time_codes.begin(), out_time_codes.end());
        out_time_codes.erase(std::unique(out_time_codes.begin(), out_time_codes.end()), out_time_codes.end());
    }

    // The pose the stack composes to at one time code, decomposed - what the
    // reader's bake produced as the three channels' keys. `posed` is scratch
    // the caller owns, so a sweep over a timeline allocates nothing.
    static void get_stack_pose(
        const erhe::scene::Xform_op_stack& stack,
        const double                       time_code,
        erhe::scene::Xform_op_stack&       posed,
        glm::dvec3&                        out_translation,
        glm::dquat&                        out_rotation,
        glm::dvec3&                        out_scale
    )
    {
        for (std::size_t i = 0, end = stack.ops.size(); i < end; ++i) {
            posed.ops[i].value = get_xform_op_value_at(stack.ops[i], time_code);
        }
        const glm::mat4 matrix     = glm::mat4{posed.compose()};
        glm::vec3       scale      {1.0f};
        glm::quat       rotation   {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3       translation{0.0f};
        glm::vec3       skew       {0.0f};
        glm::vec4       perspective{0.0f};
        glm::decompose(matrix, scale, rotation, translation, skew, perspective);
        out_translation = glm::dvec3{translation};
        out_rotation    = glm::dquat{rotation};
        out_scale       = glm::dvec3{scale};
    }

    // Whether one channel's keys are still the projection of the authored
    // samples the reader made them from: the same key times, and the value
    // `key_is_authored` accepts at each.
    template <typename Key_predicate>
    [[nodiscard]] auto channel_keys_are_authored(
        const erhe::scene::Animation_sampler& sampler,
        const std::size_t                     component_count,
        const std::vector<double>&            time_codes,
        Key_predicate&&                       key_is_authored
    ) const -> bool
    {
        const std::size_t key_count = get_key_count(sampler, component_count);
        if (key_count != time_codes.size()) {
            return false;
        }
        for (std::size_t key = 0; key < key_count; ++key) {
            if (!is_near_value(get_time_code(sampler.timestamps[key]), time_codes[key])) {
                return false;
            }
            if (!key_is_authored(key, get_key_value(sampler, component_count, key))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static auto get_path_channel(
        const Node_transform_channels&    channels,
        const erhe::scene::Animation_path path
    ) -> const erhe::scene::Animation_sampler*
    {
        return (path == erhe::scene::Animation_path::TRANSLATION) ? channels.translation :
               (path == erhe::scene::Animation_path::ROTATION)    ? channels.rotation    :
               (path == erhe::scene::Animation_path::SCALE)       ? channels.scale       : nullptr;
    }

    // The samples of a stack the channels drive op by op, derived from the
    // keys: one sample per key, at `time * timeCodesPerSecond` and in the
    // op's own value form. An op whose channel still keys the authored
    // samples keeps them, so a file only an edit reached is the only file a
    // save changes.
    [[nodiscard]] auto write_back_driven_stack(
        const erhe::scene::Xform_op_stack& stack,
        const Node_transform_channels&     channels,
        erhe::scene::Xform_op_stack&       out_stack,
        bool&                              out_refused
    ) const -> bool
    {
        out_stack = stack;
        bool any_change{false};
        for (std::size_t i = 0, end = stack.ops.size(); i < end; ++i) {
            const erhe::scene::Xform_op& op = stack.ops[i];
            if (op.samples.empty()) {
                continue;
            }
            erhe::scene::Animation_path path{erhe::scene::Animation_path::INVALID};
            if (!get_xform_op_animation_path(op.type, path)) {
                continue;
            }
            const erhe::scene::Animation_sampler* sampler = get_path_channel(channels, path);
            if (sampler == nullptr) {
                continue;
            }
            const std::size_t   component_count = erhe::scene::get_component_count(path);
            std::vector<double> time_codes;
            time_codes.reserve(op.samples.size());
            for (const erhe::scene::Xform_op_sample& sample : op.samples) {
                time_codes.push_back(sample.time_code);
            }
            const bool authored = channel_keys_are_authored(
                *sampler, component_count, time_codes,
                [&op, path](const std::size_t key, const glm::vec4& value) -> bool {
                    if (path == erhe::scene::Animation_path::ROTATION) {
                        return is_near_rotation(get_key_rotation(value), get_xform_op_sample_rotation(op, op.samples[key]));
                    }
                    return is_near_vector(glm::dvec3{value}, std::get<glm::dvec3>(op.samples[key].value));
                }
            );
            if (authored) {
                continue;
            }
            const std::size_t key_count = get_key_count(*sampler, component_count);
            if (key_count == 0) {
                continue;
            }
            std::vector<erhe::scene::Xform_op_sample> samples;
            samples.reserve(key_count);
            for (std::size_t key = 0; key < key_count; ++key) {
                const glm::vec4             value = get_key_value(*sampler, component_count, key);
                erhe::scene::Xform_op_value op_value{};
                if (path == erhe::scene::Animation_path::ROTATION) {
                    if (!make_rotation_op_value(op.type, glm::normalize(get_key_rotation(value)), op_value)) {
                        out_refused = true;
                        return false;
                    }
                } else {
                    op_value = glm::dvec3{value};
                }
                samples.push_back(
                    erhe::scene::Xform_op_sample{
                        .time_code = get_time_code(sampler->timestamps[key]),
                        .value     = op_value
                    }
                );
            }
            out_stack.ops[i].samples = std::move(samples);
            out_stack.ops[i].value   = get_xform_op_value_at(out_stack.ops[i], out_stack.ops[i].samples.front().time_code);
            any_change = true;
        }
        return any_change;
    }

    // The samples of a baked stack, derived from the composed pose the three
    // channels key: at each key time the pose the channels ask for is solved
    // into the stack op by op, left to right, each op taking what it can hold
    // of what is left and the rest travelling on. An op the write-back may
    // not touch keeps its authored value and takes its part out all the same,
    // so a pivot pair still pivots. What is left after the last op must be
    // the identity - when it is not, the stack cannot carry the pose and the
    // authored samples stay.
    [[nodiscard]] auto write_back_baked_stack(
        const erhe::scene::Xform_op_stack& stack,
        const Node_transform_channels&     channels,
        erhe::scene::Xform_op_stack&       out_stack,
        bool&                              out_refused
    ) const -> bool
    {
        if ((channels.translation == nullptr) && (channels.rotation == nullptr) && (channels.scale == nullptr)) {
            return false;
        }
        std::vector<double> authored_time_codes;
        collect_stack_time_codes(stack, authored_time_codes);
        if (authored_time_codes.empty()) {
            return false;
        }
        erhe::scene::Xform_op_stack posed = stack;

        // Unedited? Every channel then keys the authored timeline with the
        // pose the stack composes to there.
        bool authored{true};
        for (const erhe::scene::Animation_path path : {
            erhe::scene::Animation_path::TRANSLATION,
            erhe::scene::Animation_path::ROTATION,
            erhe::scene::Animation_path::SCALE
        }) {
            const erhe::scene::Animation_sampler* sampler = get_path_channel(channels, path);
            if (sampler == nullptr) {
                continue;
            }
            authored = channel_keys_are_authored(
                *sampler, erhe::scene::get_component_count(path), authored_time_codes,
                [&stack, &posed, &authored_time_codes, path](const std::size_t key, const glm::vec4& value) -> bool {
                    glm::dvec3 translation{0.0};
                    glm::dquat rotation   {1.0, 0.0, 0.0, 0.0};
                    glm::dvec3 scale      {1.0};
                    get_stack_pose(stack, authored_time_codes[key], posed, translation, rotation, scale);
                    switch (path) {
                        case erhe::scene::Animation_path::TRANSLATION: return is_near_vector(glm::dvec3{value}, translation);
                        case erhe::scene::Animation_path::ROTATION:    return is_near_rotation(get_key_rotation(value), rotation);
                        default:                                      return is_near_vector(glm::dvec3{value}, scale);
                    }
                }
            );
            if (!authored) {
                break;
            }
        }
        if (authored) {
            return false;
        }

        // The timeline the edited clip keys, which is the union of what its
        // channels key rather than what the file authored.
        std::vector<double> time_codes;
        for (const erhe::scene::Animation_sampler* sampler : {channels.translation, channels.rotation, channels.scale}) {
            if (sampler == nullptr) {
                continue;
            }
            for (const float time : sampler->timestamps) {
                time_codes.push_back(get_time_code(time));
            }
        }
        std::sort(time_codes.begin(), time_codes.end());
        time_codes.erase(std::unique(time_codes.begin(), time_codes.end()), time_codes.end());
        if (time_codes.empty()) {
            return false;
        }

        out_stack = stack;
        for (erhe::scene::Xform_op& op : out_stack.ops) {
            if (is_write_back_op(op)) {
                op.samples.clear();
                op.samples.reserve(time_codes.size());
            }
        }
        for (const double time_code : time_codes) {
            const float time = static_cast<float>(time_code / get_time_codes_per_second());
            glm::dvec3  translation{0.0};
            glm::dquat  rotation   {1.0, 0.0, 0.0, 0.0};
            glm::dvec3  scale      {1.0};
            get_stack_pose(stack, time_code, posed, translation, rotation, scale);
            glm::vec4 value{0.0f, 0.0f, 0.0f, 0.0f};
            if ((channels.translation != nullptr) && sample_channel(*channels.translation, 3, time, value)) {
                translation = glm::dvec3{value};
            }
            if ((channels.rotation != nullptr) && sample_channel(*channels.rotation, 4, time, value)) {
                rotation = glm::normalize(get_key_rotation(value));
            }
            if ((channels.scale != nullptr) && sample_channel(*channels.scale, 3, time, value)) {
                scale = glm::dvec3{value};
            }
            const glm::dmat4 requested =
                glm::translate(glm::dmat4{1.0}, translation) *
                glm::dmat4{glm::dmat3{rotation}} *
                glm::scale(glm::dmat4{1.0}, scale);

            glm::dmat4 remaining = requested;
            for (std::size_t i = 0, end = out_stack.ops.size(); i < end; ++i) {
                erhe::scene::Xform_op& op   = out_stack.ops[i];
                erhe::scene::Xform_op  step = stack.ops[i];
                // The authored op says whether this one is written back: the
                // one being filled has had its samples cleared.
                if (is_write_back_op(stack.ops[i])) {
                    erhe::scene::Xform_op_value op_value{};
                    if (!extract_op_value(op.type, remaining, op_value)) {
                        out_refused = true;
                        return false;
                    }
                    step.value = op_value;
                    op.samples.push_back(erhe::scene::Xform_op_sample{.time_code = time_code, .value = op_value});
                } else {
                    step.value = get_xform_op_value_at(stack.ops[i], time_code);
                }
                remaining = glm::inverse(step.to_matrix()) * remaining;
            }
            if (!is_near(glm::mat4{remaining}, glm::mat4{1.0f})) {
                out_refused = true;
                return false;
            }
        }
        for (erhe::scene::Xform_op& op : out_stack.ops) {
            if (!op.samples.empty()) {
                op.value = get_xform_op_value_at(op, op.samples.front().time_code);
            }
        }
        return true;
    }

    // USD gives the time samples of a floating-point attribute one
    // interpolation, linear, and authors no per-attribute choice a reader
    // could pick another one from. A clip keyed any other way writes its keys
    // and reads back linear, which the save says once.
    void warn_once_about_interpolation(const Node_transform_channels& channels)
    {
        if (m_warned_interpolation) {
            return;
        }
        for (const erhe::scene::Animation_sampler* sampler : {channels.translation, channels.rotation, channels.scale}) {
            if ((sampler != nullptr) && (sampler->interpolation_mode != erhe::scene::Animation_interpolation_mode::LINEAR)) {
                m_warned_interpolation = true;
                add_warning(
                    fmt::format(
                        "an edited animation interpolates its keys as {} - USD time samples are linear, and the file reads back as linear",
                        erhe::scene::c_str(sampler->interpolation_mode)
                    )
                );
                return;
            }
        }
    }

    // The stack a sampled prim is written with: the authored one when the
    // animation's keys are still the projection of its samples, and one
    // carrying the keys when they are not (src/erhe/usd/notes.md, "Time
    // samples"). False when the authored stack is what gets written; an edit
    // the ops cannot hold is named in one warning per prim.
    [[nodiscard]] auto reconcile_sampled_stack(
        const erhe::scene::Node&           node,
        const erhe::scene::Xform_op_stack& stack,
        erhe::scene::Xform_op_stack&       out_stack
    ) -> bool
    {
        const std::map<const erhe::scene::Node*, Node_transform_channels>::const_iterator i =
            m_transform_channels.find(&node);
        if (i == m_transform_channels.end()) {
            return false;
        }
        bool       refused{false};
        const bool driven  = get_xform_op_stack_animation_refusal(stack).empty();
        const bool written = driven
            ? write_back_driven_stack(stack, i->second, out_stack, refused)
            : write_back_baked_stack (stack, i->second, out_stack, refused);
        if (written) {
            warn_once_about_interpolation(i->second);
        }
        if (refused && (m_write_back_refusals.count(&node) == 0)) {
            m_write_back_refusals.insert(&node);
            add_warning(
                fmt::format(
                    "the edited animation of prim '{}' does not fit its authored xformOps - its samples are written as the file authored them",
                    node.get_name()
                )
            );
        }
        return written;
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
        const erhe::scene::Xform_op_stack* stack = node.get_xform_op_stack();
        if ((stack != nullptr) && stack->has_time_samples()) {
            erhe::scene::Xform_op_stack edited;
            if (reconcile_sampled_stack(node, *stack, edited)) {
                write_xform_op_stack(xform_ops, edited);
                return;
            }
        }
        set_transform(xform_ops, stack, matrix);
    }

    void set_transform(
        std::vector<lightusd::XformOp>&    xform_ops,
        const erhe::scene::Xform_op_stack* stack,
        const glm::mat4&                   matrix
    )
    {
        // `matrix` is the authored transform - the base under the animated
        // layer (D5), so the pose a playing animation put the prim in never
        // reaches here. A time-sampled stack is written whatever that matrix
        // says: the samples are the authority over the stack's composition
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
            const erhe::scene::Trs_transform transform   = instance_node->authored_parent_from_node_transform();
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
                rest_transforms.push_back(to_usd(record.joints[joint_index]->authored_parent_from_node_transform().get_matrix()));
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
                const std::shared_ptr<erhe::scene::Node> target_node = erhe::scene::get_target_node(channel);
                if (!target_node || (channel.sampler_index >= animation->samplers.size())) {
                    continue;
                }
                const std::vector<const erhe::scene::Node*>::const_iterator i = std::find(
                    record.joints.begin(), record.joints.end(), target_node.get()
                );
                if (i == record.joints.end()) {
                    continue;
                }
                const erhe::scene::Animation_sampler&  sampler = animation->samplers[channel.sampler_index];
                Joint_channels&                        joint   = joint_channels[static_cast<std::size_t>(i - record.joints.begin())];
                const erhe::scene::Animation_sampler** target  = nullptr;
                switch (erhe::scene::get_animation_path(channel)) {
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
                decompose_trs(record.joints[joint_index]->authored_parent_from_node_transform().get_matrix(), translation, rotation, scale);

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
    void write_light_api(const erhe::scene::Light& light, T& usd_light)
    {
        using erhe::scene::Light;

        if (is_local(light, Light::color_property.get())) {
            const glm::vec3 color = light.get_value(Light::color_property);
            usd_light.color.set_value(lightusd::value::color3f{color.x, color.y, color.z});
        }
        if (is_local(light, Light::temperature_property.get())) {
            usd_light.enableColorTemperature.set_value(true);
            usd_light.colorTemperature.set_value(light.get_value(Light::temperature_property));
        }
        if (is_local(light, Light::cast_shadow_property.get())) {
            usd_light.shadowEnable.set_value(light.get_value(Light::cast_shadow_property));
        }
        // The keys of a clip driving the light, beside those values
        // (src/erhe/usd/notes.md, "Time samples"). erhe has no exposure on a
        // light, so the whole quantity is the intensity and `inputs:exposure`
        // stays at its zero fallback - which is what the importer folds back
        // in.
        const Item_attribute_channels channels = get_attribute_channels(light);
        const std::optional<float>    intensity = is_local(light, Light::intensity_property.get())
            ? std::optional<float>{light.get_value(Light::intensity_property)}
            : std::optional<float>{};
        // A cubic clip writes the attribute as a spline carrying its own
        // default, so the schema attribute is left to it.
        if (!write_spline_attribute(usd_light.props, "inputs:intensity", channels.intensity, 1, intensity)) {
            if (intensity.has_value()) {
                usd_light.intensity.set_value(intensity.value());
            }
            write_sampled_attribute<float>(
                usd_light.intensity, channels.intensity, 1,
                [](const glm::vec4& value) -> float { return value.x; }
            );
        }
        write_sampled_attribute<lightusd::value::color3f>(
            usd_light.color, channels.color, 3,
            [](const glm::vec4& value) -> lightusd::value::color3f {
                return lightusd::value::color3f{value.x, value.y, value.z};
            }
        );
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
    // Physics (doc/usd_compatibility.md, "Physics")
    // -------------------------------------------------------------------

    // The records of `Usd_save_physics`, by the item each names: what the
    // plan and the write look a prim up in.
    void index_physics()
    {
        const erhe::scene::Physics_description* description = m_arguments.physics.description;
        if (description == nullptr) {
            return;
        }
        for (std::size_t index = 0, end = description->node_physics.size(); index < end; ++index) {
            const erhe::scene::Physics_node_description& body = description->node_physics[index];
            if (body.node) {
                m_physics_body_index[body.node.get()] = index;
            }
        }
        for (std::size_t index = 0, end = description->synthesized_colliders.size(); index < end; ++index) {
            const erhe::scene::Physics_synthesized_collider& collider = description->synthesized_colliders[index];
            if (collider.parent) {
                m_physics_synthesized_indices[collider.parent.get()].push_back(index);
            }
        }
        index_physics_records(m_arguments.physics.materials,         m_physics_material_record);
        index_physics_records(m_arguments.physics.collision_filters, m_physics_filter_record);
        index_physics_records(m_arguments.physics.joint_settings,    m_physics_settings_record);
        index_physics_records(m_arguments.physics.bodies,            m_physics_body_record);
    }

    static void index_physics_records(
        const std::vector<Usd_save_physics_record>&          records,
        std::map<const erhe::Item_base*, std::size_t>&       out_indices
    )
    {
        for (std::size_t index = 0, end = records.size(); index < end; ++index) {
            if (records[index].item) {
                out_indices[records[index].item.get()] = index;
            }
        }
    }

    [[nodiscard]] auto physics_description() const -> const erhe::scene::Physics_description*
    {
        return m_arguments.physics.description;
    }

    [[nodiscard]] auto find_physics_record(
        const std::vector<Usd_save_physics_record>&          records,
        const std::map<const erhe::Item_base*, std::size_t>& indices,
        const erhe::Item_base&                               item
    ) const -> const Usd_save_physics_record*
    {
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator i = indices.find(&item);
        return (i == indices.end()) ? nullptr : &records[i->second];
    }

    // The shape one body states, as a collider or as a trigger: the two carry
    // the same geometry and the same filter, and a trigger is a body that
    // detects overlaps rather than colliding.
    [[nodiscard]] static auto collider_geometry_of(
        const erhe::scene::Physics_node_description& entry
    ) -> const erhe::scene::Physics_node_geometry*
    {
        if (entry.collider.has_value()) {
            return &entry.collider.value().geometry;
        }
        if (entry.trigger.has_value() && entry.trigger.value().geometry.has_value()) {
            return &entry.trigger.value().geometry.value();
        }
        return nullptr;
    }

    [[nodiscard]] static auto collider_material_of(
        const erhe::scene::Physics_node_description& entry
    ) -> std::optional<std::size_t>
    {
        return entry.collider.has_value() ? entry.collider.value().material_index : std::optional<std::size_t>{};
    }

    [[nodiscard]] static auto collider_filter_of(
        const erhe::scene::Physics_node_description& entry
    ) -> std::optional<std::size_t>
    {
        if (entry.collider.has_value()) {
            return entry.collider.value().filter_index;
        }
        return entry.trigger.has_value() ? entry.trigger.value().filter_index : std::optional<std::size_t>{};
    }

    // The name of the joint-settings item a joint uses, which is the name of
    // the joint prim the body carries.
    [[nodiscard]] auto joint_prim_name(const std::size_t joint_index) const -> std::string
    {
        const erhe::scene::Physics_description* description = physics_description();
        if ((description == nullptr) || (joint_index >= description->joints.size()) || description->joints[joint_index].name.empty()) {
            return std::string{"joint"};
        }
        return sanitize_usd_identifier(description->joints[joint_index].name);
    }

    // The prims a body adds below the prim it sits on: one `collider` child
    // per implicit shape - the prim's own class says nothing about the shape,
    // so the shape is a prim of its own - and one joint prim named after the
    // settings item. Their names are taken from the same scope the planned
    // children took theirs from, so a scene that already holds a prim of that
    // name keeps it.
    void plan_physics_children(Plan_prim& plan_prim, Name_scope& child_names)
    {
        const erhe::scene::Physics_description* description = physics_description();
        if ((description == nullptr) || (plan_prim.node == nullptr)) {
            return;
        }
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator body =
            m_physics_body_index.find(static_cast<const erhe::Item_base*>(plan_prim.node));
        if (body != m_physics_body_index.end()) {
            plan_prim.physics_body = body->second;
            const erhe::scene::Physics_node_description& entry    = description->node_physics[body->second];
            const erhe::scene::Physics_node_geometry*     geometry = collider_geometry_of(entry);
            if ((geometry != nullptr) && geometry->shape_index.has_value()) {
                plan_prim.physics_collider_name = child_names.make_unique(std::string{c_physics_collider_prim_name});
            }
            if (entry.trigger.has_value()) {
                // A compound trigger states its shapes on the prims it names,
                // and each of those is a collider prim below this one.
                for (const std::shared_ptr<erhe::scene::Node>& member : entry.trigger.value().compound_nodes) {
                    const std::size_t member_index = find_body_index(member.get());
                    if (member_index == c_no_physics_index) {
                        continue;
                    }
                    const erhe::scene::Physics_node_geometry* member_geometry =
                        collider_geometry_of(description->node_physics[member_index]);
                    if ((member_geometry == nullptr) || !member_geometry->shape_index.has_value()) {
                        continue;
                    }
                    plan_prim.physics_trigger_members.push_back(
                        Plan_physics_child{
                            .index = member_index,
                            .name  = child_names.make_unique(member->get_name())
                        }
                    );
                }
            }
            if (entry.joint.has_value()) {
                plan_prim.physics_joint_name = child_names.make_unique(joint_prim_name(entry.joint.value().joint_index));
            }
        }
        const std::map<const erhe::Item_base*, std::vector<std::size_t>>::const_iterator synthesized =
            m_physics_synthesized_indices.find(static_cast<const erhe::Item_base*>(plan_prim.node));
        if (synthesized != m_physics_synthesized_indices.end()) {
            for (const std::size_t index : synthesized->second) {
                const erhe::scene::Physics_synthesized_collider& collider = description->synthesized_colliders[index];
                plan_prim.physics_synthesized.push_back(
                    Plan_physics_child{
                        .index = index,
                        .name  = child_names.make_unique(
                            collider.name.empty() ? std::string{c_physics_collider_prim_name} : collider.name
                        )
                    }
                );
            }
        }
    }

    [[nodiscard]] auto find_body_index(const erhe::scene::Node* node) const -> std::size_t
    {
        if (node == nullptr) {
            return c_no_physics_index;
        }
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator i =
            m_physics_body_index.find(static_cast<const erhe::Item_base*>(node));
        return (i == m_physics_body_index.end()) ? c_no_physics_index : i->second;
    }

    // Where the `PhysicsScene` prim goes: below the prim the stage names as
    // its defaultPrim, so a save adds no top-level prim and the wrap decision
    // above is the one the tree made.
    void plan_physics_scene(std::vector<Plan_prim>& plan, const Prim_plan& prim_plan)
    {
        if (!m_arguments.physics.has_physics_scene) {
            return;
        }
        if (prim_plan.wrapped) {
            m_physics_scene_parent_path = fmt::format("/{}", c_world_prim_name);
            m_physics_scene_name        = std::string{c_physics_scene_prim_type_name};
            return;
        }
        for (const Plan_prim& prim : plan) {
            if (prim.name != prim_plan.default_prim_name) {
                continue;
            }
            Name_scope names;
            for (const Plan_prim& child : prim.children) {
                static_cast<void>(names.make_unique(child.name));
            }
            m_physics_scene_parent_path = prim.path;
            m_physics_scene_name        = names.make_unique(std::string{c_physics_scene_prim_type_name});
            return;
        }
    }

    // The gravity of the physics world, as the one `PhysicsScene` prim of the
    // file. Each value is written only when the caller has one: USD's own
    // fallbacks - the stage's negative up axis, earth gravity - are what a
    // file that authors neither means.
    [[nodiscard]] auto write_physics_scene_prim() -> lightusd::Prim
    {
        lightusd::PhysicsScene scene;
        scene.name = m_physics_scene_name;
        if (m_arguments.physics.gravity_direction.has_value()) {
            const glm::vec3 direction = m_arguments.physics.gravity_direction.value();
            scene.gravityDirection.set_value(lightusd::value::vector3f{direction.x, direction.y, direction.z});
        }
        if (m_arguments.physics.gravity_magnitude.has_value()) {
            scene.gravityMagnitude.set_value(m_arguments.physics.gravity_magnitude.value());
        }
        return lightusd::Prim{scene};
    }

    // The generic property map of a written prim: where an applied API
    // schema's attributes and the `erhe:` custom attributes of a physics
    // record go. Every prim class a physics record can land on is listed.
    [[nodiscard]] auto mutable_props_of(lightusd::Prim& prim) -> std::map<std::string, lightusd::Property>*
    {
        std::map<std::string, lightusd::Property>* props = nullptr;
        if ((props = props_of<lightusd::Xform         >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomMesh      >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::Model         >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::Scope         >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::Material      >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomCube      >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomSphere    >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomCapsule   >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomCylinder  >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::Skeleton      >(prim)) != nullptr) { return props; }
        if ((props = props_of<lightusd::GeomCamera    >(prim)) != nullptr) { return props; }
        return nullptr;
    }

    template <typename T>
    [[nodiscard]] static auto props_of(lightusd::Prim& prim) -> std::map<std::string, lightusd::Property>*
    {
        T* typed = prim.get_data().as<T>();
        return (typed != nullptr) ? &typed->props : nullptr;
    }

    static void add_float_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const float                                value,
        const Attribute_form                       form
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(value);
        props.emplace(name, lightusd::Property{std::move(attribute), form == Attribute_form::custom_attribute});
    }

    static void add_bool_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const bool                                 value,
        const Attribute_form                       form
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(value);
        props.emplace(name, lightusd::Property{std::move(attribute), form == Attribute_form::custom_attribute});
    }

    static void add_vector3f_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const glm::vec3&                           value
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(lightusd::value::vector3f{value.x, value.y, value.z});
        props.emplace(name, lightusd::Property{std::move(attribute), false});
    }

    static void add_point3f_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const glm::vec3&                           value
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(lightusd::value::point3f{value.x, value.y, value.z});
        props.emplace(name, lightusd::Property{std::move(attribute), false});
    }

    static void add_string_array_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const std::vector<std::string>&            values
    )
    {
        lightusd::Attribute attribute;
        attribute.set_value(values);
        props.emplace(name, lightusd::Property{std::move(attribute), true});
    }

    static void add_relationship_property(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const std::vector<std::string>&            paths,
        const Attribute_form                       form
    )
    {
        if (paths.empty()) {
            return;
        }
        lightusd::Relationship relationship;
        if (paths.size() == 1) {
            relationship.set(lightusd::Path{paths.front(), ""});
        } else {
            std::vector<lightusd::Path> path_vector;
            path_vector.reserve(paths.size());
            for (const std::string& path : paths) {
                path_vector.emplace_back(path, "");
            }
            relationship.set(std::move(path_vector));
        }
        props.emplace(name, lightusd::Property{std::move(relationship), form == Attribute_form::custom_attribute});
    }

    // The erhe-only values of one physics record, as the `erhe:Owner:name`
    // custom attributes they are. The value travels as property text and the
    // record's USD type spells it again, the way a node-graph parameter does.
    void write_physics_record_properties(
        std::map<std::string, lightusd::Property>& props,
        const Usd_save_physics_record*             record,
        const std::string&                         owner
    )
    {
        if (record == nullptr) {
            return;
        }
        for (const Usd_physics_property& property : record->properties) {
            const std::size_t separator = property.name.find('.');
            if (separator == std::string::npos) {
                continue;
            }
            const std::string attribute_name = fmt::format(
                "erhe:{}:{}",
                property.name.substr(0, separator),
                property.name.substr(separator + 1)
            );
            props.emplace(
                attribute_name,
                lightusd::Property{make_node_graph_attribute(property.usd_type, property.value, owner), true}
            );
        }
    }

    // One value of a record that a schema attribute carries rather than an
    // `erhe:` custom attribute: the reader reports it as a property because
    // the neutral description has no field for it, and the writer puts it
    // back where the schema keeps it.
    [[nodiscard]] static auto find_record_property(
        const Usd_save_physics_record* record,
        const std::string&             name
    ) -> const Usd_physics_property*
    {
        if (record == nullptr) {
            return nullptr;
        }
        for (const Usd_physics_property& property : record->properties) {
            if (property.name == name) {
                return &property;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto planned_path_of(const erhe::Item_base* item) const -> std::string
    {
        if (item == nullptr) {
            return std::string{};
        }
        const std::map<const erhe::Item_base*, std::string>::const_iterator i = m_planned_paths.find(item);
        return (i == m_planned_paths.end()) ? std::string{} : i->second;
    }

    // The body and the collider one prim of the tree carries: the API schemas
    // the mapping names, applied to the prim itself, and the attributes of
    // each. A body with no motion is a static body, which is the collision
    // schema alone.
    void write_physics_on_prim(const Plan_prim& plan_prim, lightusd::Prim& prim)
    {
        const erhe::scene::Physics_description* description = physics_description();
        if ((description == nullptr) || (plan_prim.physics_body == c_no_physics_index)) {
            return;
        }
        std::map<std::string, lightusd::Property>* props = mutable_props_of(prim);
        if (props == nullptr) {
            add_warning(fmt::format("prim '{}' holds physics that its prim class cannot carry", plan_prim.path));
            return;
        }
        const erhe::scene::Physics_node_description& entry  = description->node_physics[plan_prim.physics_body];
        const Usd_save_physics_record* const         record = (plan_prim.physics_body < m_arguments.physics.bodies.size())
            ? &m_arguments.physics.bodies[plan_prim.physics_body]
            : nullptr;
        if (entry.motion.has_value()) {
            const erhe::scene::Physics_node_motion& motion = entry.motion.value();
            apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsRigidBodyAPI, std::string{});
            if (motion.is_kinematic) {
                add_bool_attribute(*props, "physics:kinematicEnabled", true, Attribute_form::schema_attribute);
            }
            if (motion.linear_velocity != glm::vec3{0.0f}) {
                add_vector3f_attribute(*props, "physics:velocity", motion.linear_velocity);
            }
            if (motion.angular_velocity != glm::vec3{0.0f}) {
                // USD spells an angular velocity in degrees per second.
                add_vector3f_attribute(
                    *props,
                    "physics:angularVelocity",
                    glm::vec3{
                        to_degrees(motion.angular_velocity.x),
                        to_degrees(motion.angular_velocity.y),
                        to_degrees(motion.angular_velocity.z)
                    }
                );
            }
            const bool has_mass = motion.mass.has_value() || (motion.center_of_mass != glm::vec3{0.0f}) || motion.inertia_diagonal.has_value();
            if (has_mass) {
                apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsMassAPI, std::string{});
                if (motion.mass.has_value()) {
                    add_float_attribute(*props, "physics:mass", motion.mass.value(), Attribute_form::schema_attribute);
                }
                if (motion.center_of_mass != glm::vec3{0.0f}) {
                    add_point3f_attribute(*props, "physics:centerOfMass", motion.center_of_mass);
                }
                if (motion.inertia_diagonal.has_value()) {
                    const glm::vec3     inertia = motion.inertia_diagonal.value();
                    lightusd::Attribute attribute;
                    attribute.set_value(lightusd::value::float3{inertia.x, inertia.y, inertia.z});
                    props->emplace("physics:diagonalInertia", lightusd::Property{std::move(attribute), false});
                    if (motion.inertia_orientation.has_value()) {
                        const glm::quat     orientation = motion.inertia_orientation.value();
                        lightusd::Attribute axes;
                        axes.set_value(
                            lightusd::value::quatf{
                                {{orientation.x, orientation.y, orientation.z}},
                                orientation.w
                            }
                        );
                        props->emplace("physics:principalAxes", lightusd::Property{std::move(axes), false});
                    }
                }
            }
            if (motion.gravity_factor != 1.0f) {
                add_float_attribute(
                    *props,
                    std::string{c_node_physics_gravity_factor_attribute},
                    motion.gravity_factor,
                    Attribute_form::custom_attribute
                );
            }
        }
        write_physics_record_properties(*props, record, plan_prim.path);
        if (entry.trigger.has_value()) {
            // A trigger is a body that detects overlaps rather than
            // colliding, which no UsdPhysics schema states.
            add_bool_attribute(*props, std::string{c_node_physics_is_trigger_attribute}, true, Attribute_form::custom_attribute);
        }
        const erhe::scene::Physics_node_geometry* const geometry = collider_geometry_of(entry);
        if ((geometry != nullptr) && !geometry->shape_index.has_value()) {
            // A mesh shape is the prim's own geometry, so the collision
            // schemas go on the prim rather than on a child of it.
            write_mesh_collider_on_prim(prim, *props, *geometry, collider_material_of(entry), plan_prim.path);
        }
    }

    void write_mesh_collider_on_prim(
        lightusd::Prim&                            prim,
        std::map<std::string, lightusd::Property>& props,
        const erhe::scene::Physics_node_geometry&  geometry,
        const std::optional<std::size_t>&          material_index,
        const std::string&                         prim_path
    )
    {
        apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsCollisionAPI, std::string{});
        apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsMeshCollisionAPI, std::string{});
        add_token_attribute(props, "physics:approximation", geometry.convex_hull ? "convexHull" : "none");
        write_physics_material_binding(prim, props, material_index, prim_path);
    }

    void write_physics_material_binding(
        lightusd::Prim&                            prim,
        std::map<std::string, lightusd::Property>& props,
        const std::optional<std::size_t>&          material_index,
        const std::string&                         prim_path
    )
    {
        if (!material_index.has_value()) {
            return;
        }
        if (material_index.value() >= m_arguments.physics.materials.size()) {
            return;
        }
        const std::string path = planned_path_of(m_arguments.physics.materials[material_index.value()].item.get());
        if (path.empty()) {
            add_warning(
                fmt::format("prim '{}' binds a physics material that is no prim of the tree - the binding is dropped", prim_path)
            );
            return;
        }
        apply_material_binding_api(prim);
        add_relationship_property(props, std::string{c_physics_material_binding}, {path}, Attribute_form::schema_attribute);
    }

    // The prims a body adds below its own: the implicit shape of its
    // collider, the shapes of the synthesized colliders that name it, and the
    // joint it carries.
    void write_physics_child_prims(const Plan_prim& plan_prim, std::vector<lightusd::Prim>& out_prims)
    {
        const erhe::scene::Physics_description* description = physics_description();
        if (description == nullptr) {
            return;
        }
        if (plan_prim.physics_body != c_no_physics_index) {
            const erhe::scene::Physics_node_description&     entry    = description->node_physics[plan_prim.physics_body];
            const erhe::scene::Physics_node_geometry* const  geometry = collider_geometry_of(entry);
            if (!plan_prim.physics_collider_name.empty() && (geometry != nullptr)) {
                out_prims.push_back(
                    write_implicit_collider_prim(
                        plan_prim.physics_collider_name,
                        geometry->shape_index.value(),
                        collider_material_of(entry),
                        glm::vec3{0.0f},
                        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                        glm::vec3{1.0f},
                        plan_prim.path
                    )
                );
            }
            for (const Plan_physics_child& member : plan_prim.physics_trigger_members) {
                const erhe::scene::Physics_node_description&    member_entry    = description->node_physics[member.index];
                const erhe::scene::Physics_node_geometry* const member_geometry = collider_geometry_of(member_entry);
                out_prims.push_back(
                    write_implicit_collider_prim(
                        member.name,
                        member_geometry->shape_index.value(),
                        collider_material_of(member_entry),
                        glm::vec3{0.0f},
                        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                        glm::vec3{1.0f},
                        plan_prim.path
                    )
                );
            }
            if (!plan_prim.physics_joint_name.empty() && entry.joint.has_value()) {
                out_prims.push_back(write_joint_prim(plan_prim, entry, entry.joint.value()));
            }
        }
        for (const Plan_physics_child& child : plan_prim.physics_synthesized) {
            const erhe::scene::Physics_synthesized_collider& collider = description->synthesized_colliders[child.index];
            if (!collider.geometry.shape_index.has_value()) {
                add_warning(
                    fmt::format("the synthesized collider '{}' of prim '{}' carries no implicit shape - it is not written", child.name, plan_prim.path)
                );
                continue;
            }
            out_prims.push_back(
                write_implicit_collider_prim(
                    child.name,
                    collider.geometry.shape_index.value(),
                    collider.material_index,
                    collider.translation,
                    collider.rotation,
                    collider.scale,
                    plan_prim.path
                )
            );
        }
    }

    // One implicit collision shape as the primitive-schema prim it is: a
    // `guide` prim carrying the collision schema and the shape's dimensions,
    // which is what says it is a collider rather than something to render.
    [[nodiscard]] auto write_implicit_collider_prim(
        const std::string&                prim_name,
        const std::size_t                 shape_index,
        const std::optional<std::size_t>& material_index,
        const glm::vec3&                  translation,
        const glm::quat&                  rotation,
        const glm::vec3&                  scale,
        const std::string&                parent_path
    ) -> lightusd::Prim
    {
        const erhe::scene::Physics_description* description = physics_description();
        const erhe::scene::Physics_shape        shape       = (shape_index < description->shapes.size())
            ? description->shapes[shape_index]
            : erhe::scene::Physics_shape{};
        glm::vec3      shape_scale{1.0f};
        lightusd::Prim prim  = make_shape_prim(shape, prim_name, shape_scale);
        std::map<std::string, lightusd::Property>* props = mutable_props_of(prim);
        if (props == nullptr) {
            return prim;
        }
        apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsCollisionAPI, std::string{});
        if ((shape.type == erhe::scene::Physics_shape_type::e_capsule) || (shape.type == erhe::scene::Physics_shape_type::e_cylinder)) {
            if (shape.radius_bottom != shape.radius_top) {
                add_float_attribute(*props, std::string{c_physics_shape_radius_bottom_attribute}, shape.radius_bottom, Attribute_form::custom_attribute);
                add_float_attribute(*props, std::string{c_physics_shape_radius_top_attribute   }, shape.radius_top,    Attribute_form::custom_attribute);
            }
        }
        write_physics_material_binding(prim, *props, material_index, parent_path + "/" + prim_name);
        set_collider_transform(prim, translation, rotation, scale * shape_scale);
        return prim;
    }

    // The shape's own prim class and dimensions. A tapered capsule or
    // cylinder is written with the larger radius on the schema attribute and
    // the exact pair on the two `erhe:Physics_shape:` attributes, so a reader
    // without erhe simulates a shape of the right size. A box of unequal
    // extents is a unit `Cube` scaled per axis - the form USD's own physics
    // tooling authors - which `out_shape_scale` reports to the caller, who
    // composes it with the place the collider sits at.
    [[nodiscard]] auto make_shape_prim(
        const erhe::scene::Physics_shape& shape,
        const std::string&                prim_name,
        glm::vec3&                        out_shape_scale
    ) -> lightusd::Prim
    {
        switch (shape.type) {
            case erhe::scene::Physics_shape_type::e_sphere: {
                lightusd::GeomSphere sphere;
                sphere.name = prim_name;
                sphere.radius.set_value(static_cast<double>(shape.radius));
                sphere.purpose.set_value(lightusd::Purpose::Guide);
                return lightusd::Prim{sphere};
            }
            case erhe::scene::Physics_shape_type::e_capsule: {
                lightusd::GeomCapsule capsule;
                capsule.name = prim_name;
                capsule.radius.set_value(static_cast<double>(std::max(shape.radius_bottom, shape.radius_top)));
                capsule.height.set_value(static_cast<double>(shape.height));
                capsule.axis.set_value(lightusd::Axis::Y);
                capsule.purpose.set_value(lightusd::Purpose::Guide);
                return lightusd::Prim{capsule};
            }
            case erhe::scene::Physics_shape_type::e_cylinder: {
                lightusd::GeomCylinder cylinder;
                cylinder.name = prim_name;
                cylinder.radius.set_value(static_cast<double>(std::max(shape.radius_bottom, shape.radius_top)));
                cylinder.height.set_value(static_cast<double>(shape.height));
                cylinder.axis.set_value(lightusd::Axis::Y);
                cylinder.purpose.set_value(lightusd::Purpose::Guide);
                return lightusd::Prim{cylinder};
            }
            default: {
                // A box is a `Cube`, whose one dimension is its edge length,
                // so a box of unequal extents is the unit cube scaled by them.
                lightusd::GeomCube cube;
                cube.name = prim_name;
                const bool cubical = (shape.size.x == shape.size.y) && (shape.size.y == shape.size.z);
                if (cubical) {
                    cube.size.set_value(static_cast<double>(shape.size.x));
                } else {
                    cube.size.set_value(1.0);
                    out_shape_scale = shape.size;
                }
                cube.purpose.set_value(lightusd::Purpose::Guide);
                return lightusd::Prim{cube};
            }
        }
    }

    // The place a collider sits in its body's space, and the size a box states
    // through its scale, as the `xformOp`s of the collider prim: the plain
    // translate / orient / scale triple USD's own physics tooling authors,
    // which is also what the read applies back onto the shape. A collider at
    // the body's own frame and of its own size writes none.
    void set_collider_transform(
        lightusd::Prim&   prim,
        const glm::vec3&  translation,
        const glm::quat&  rotation,
        const glm::vec3&  scale
    )
    {
        std::vector<lightusd::XformOp>* xform_ops =
            xform_ops_of<lightusd::GeomCube   >(prim) ? xform_ops_of<lightusd::GeomCube   >(prim) :
            xform_ops_of<lightusd::GeomSphere >(prim) ? xform_ops_of<lightusd::GeomSphere >(prim) :
            xform_ops_of<lightusd::GeomCapsule>(prim) ? xform_ops_of<lightusd::GeomCapsule>(prim) :
                                                        xform_ops_of<lightusd::GeomCylinder>(prim);
        if (xform_ops == nullptr) {
            return;
        }
        if (translation != glm::vec3{0.0f}) {
            lightusd::XformOp op;
            op.op_type = lightusd::XformOp::OpType::Translate;
            op.set_value(lightusd::value::double3{translation.x, translation.y, translation.z});
            xform_ops->push_back(op);
        }
        if (rotation != glm::quat{1.0f, 0.0f, 0.0f, 0.0f}) {
            lightusd::XformOp op;
            op.op_type = lightusd::XformOp::OpType::Orient;
            op.set_value(lightusd::value::quatf{{{rotation.x, rotation.y, rotation.z}}, rotation.w});
            xform_ops->push_back(op);
        }
        if (scale != glm::vec3{1.0f}) {
            lightusd::XformOp op;
            op.op_type = lightusd::XformOp::OpType::Scale;
            op.set_value(lightusd::value::double3{scale.x, scale.y, scale.z});
            xform_ops->push_back(op);
        }
    }

    template <typename T>
    [[nodiscard]] static auto xform_ops_of(lightusd::Prim& prim) -> std::vector<lightusd::XformOp>*
    {
        T* typed = prim.get_data().as<T>();
        return (typed != nullptr) ? &typed->xformOps : nullptr;
    }

    // Which of a joint prim's two frames is written: `localPos0` /
    // `localRot0`, the frame of the first body, or `localPos1` / `localRot1`,
    // the frame of the second.
    enum class Joint_frame
    {
        first,
        second
    };

    // Whether one physics entry is a body rather than a joint frame: a frame
    // node carries the joint alone.
    [[nodiscard]] static auto is_rigid_body_entry(const erhe::scene::Physics_node_description& entry) -> bool
    {
        return entry.motion.has_value() || entry.collider.has_value() || entry.trigger.has_value();
    }

    // The nearest self-or-ancestor prim of `node` that carries a body: the
    // prim a USD joint names as one of its two bodies, which is the prim
    // erhe's constraint takes that body from.
    [[nodiscard]] auto find_joint_body_node(const erhe::scene::Node* node) const -> const erhe::scene::Node*
    {
        const erhe::scene::Physics_description* description = physics_description();
        if (description == nullptr) {
            return nullptr;
        }
        const erhe::scene::Node* current = node;
        while (current != nullptr) {
            const std::size_t index = find_body_index(current);
            if ((index != c_no_physics_index) && is_rigid_body_entry(description->node_physics[index])) {
                return current;
            }
            current = current->get_parent_node().get();
        }
        return nullptr;
    }

    // One frame of a joint: the transform of the node holding that side of
    // the joint in the space of the body prim it hangs below
    // (doc/usd_compatibility.md, "Physics"). A node that is the body prim
    // itself states the identity, which is left unwritten.
    void write_joint_frame(
        lightusd::PhysicsJoint&        usd_joint,
        const Joint_frame              side,
        const erhe::scene::Node* const body_node,
        const erhe::scene::Node* const frame_node,
        const std::string&             joint_path
    )
    {
        if ((body_node == nullptr) || (frame_node == nullptr) || (body_node == frame_node)) {
            return;
        }
        const glm::mat4 body_from_frame = glm::inverse(body_node->world_from_node()) * frame_node->world_from_node();
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        // The frame is read off the world transforms rather than off the
        // node's own: a frame node reloads with the transform its prim
        // states divided out of the composed one, and taking both sides the
        // same way is what makes the second write of a file the first.
        decompose_trs(body_from_frame, translation, rotation, scale);
        if (glm::distance(scale, glm::vec3{1.0f}) > 1e-4f) {
            add_warning(
                fmt::format(
                    "the joint of prim '{}' takes a frame from prim '{}', which is scaled against its body - a USD joint frame states no scale and the frame is written without it",
                    joint_path, frame_node->get_name()
                )
            );
        }
        if (translation != glm::vec3{0.0f}) {
            const lightusd::value::point3f value{translation.x, translation.y, translation.z};
            if (side == Joint_frame::first) {
                usd_joint.localPos0.set_value(value);
            } else {
                usd_joint.localPos1.set_value(value);
            }
        }
        if (rotation != glm::quat{1.0f, 0.0f, 0.0f, 0.0f}) {
            const lightusd::value::quatf value{{{rotation.x, rotation.y, rotation.z}}, rotation.w};
            if (side == Joint_frame::first) {
                usd_joint.localRot0.set_value(value);
            } else {
                usd_joint.localRot1.set_value(value);
            }
        }
    }

    // The joint one prim carries, as the `PhysicsJoint` prim below it: the
    // two bodies it joins, the frame of each in its body's space, and the
    // limits and drives of its settings item - applied inline, and named by
    // the relationship when the settings are a prim of their own that other
    // joints share.
    [[nodiscard]] auto write_joint_prim(
        const Plan_prim&                             plan_prim,
        const erhe::scene::Physics_node_description& entry,
        const erhe::scene::Physics_node_joint&       joint
    ) -> lightusd::Prim
    {
        lightusd::PhysicsJoint usd_joint;
        usd_joint.name = plan_prim.physics_joint_name;
        // The prim each side of the joint names is the nearest body at or
        // above the node holding that side; that node states the frame.
        const erhe::scene::Node* body0_node = find_joint_body_node(entry.node.get());
        std::string              body0_path = planned_path_of(body0_node);
        if (body0_path.empty()) {
            body0_node = entry.node.get();
            body0_path = plan_prim.path;
        }
        usd_joint.body0.set(lightusd::Path{body0_path, ""});
        write_joint_frame(usd_joint, Joint_frame::first, body0_node, entry.node.get(), plan_prim.path);

        const erhe::scene::Node* body1_node = find_joint_body_node(joint.connected_node.get());
        std::string              body1_path = planned_path_of(body1_node);
        if (body1_path.empty()) {
            body1_node = joint.connected_node.get();
            body1_path = planned_path_of(joint.connected_node.get());
        }
        if (!body1_path.empty()) {
            usd_joint.body1.set(lightusd::Path{body1_path, ""});
            write_joint_frame(usd_joint, Joint_frame::second, body1_node, joint.connected_node.get(), plan_prim.path);
        } else if (joint.connected_node) {
            add_warning(
                fmt::format("the joint of prim '{}' names a prim that is not written - it is written with one body", plan_prim.path)
            );
        }
        if (joint.enable_collision) {
            usd_joint.collisionEnabled.set_value(true);
        }
        const erhe::scene::Physics_description* description = physics_description();
        if (joint.joint_index < description->joints.size()) {
            write_joint_limits_and_drives(usd_joint.props, usd_joint.meta, description->joints[joint.joint_index]);
            const Usd_save_physics_record* const record = (joint.joint_index < m_arguments.physics.joint_settings.size())
                ? &m_arguments.physics.joint_settings[joint.joint_index]
                : nullptr;
            const std::string settings_path = (record != nullptr) ? planned_path_of(record->item.get()) : std::string{};
            if (!settings_path.empty()) {
                add_relationship_property(
                    usd_joint.props,
                    std::string{c_node_joint_settings_relationship},
                    {settings_path},
                    Attribute_form::custom_attribute
                );
            }
        }
        return lightusd::Prim{usd_joint};
    }

    // The limits and drives of one joint-settings item, as the multi-apply
    // instances they are: one `PhysicsLimitAPI:<axis>` instance per axis of a
    // limit - a limit over several axes is several instances of one value,
    // which the reader joins back - and one `PhysicsDriveAPI:<axis>` instance
    // per drive.
    void write_joint_limits_and_drives(
        std::map<std::string, lightusd::Property>&    props,
        lightusd::PrimMeta&                           meta,
        const erhe::scene::Physics_joint_description& settings
    )
    {
        for (const erhe::scene::Physics_joint_limit& limit : settings.limits) {
            for (const int axis : limit.linear_axes) {
                write_joint_limit(props, meta, limit, axis_instance_name(axis, Limit_axis_kind::linear), Limit_axis_kind::linear);
            }
            for (const int axis : limit.angular_axes) {
                write_joint_limit(props, meta, limit, axis_instance_name(axis, Limit_axis_kind::angular), Limit_axis_kind::angular);
            }
        }
        for (const erhe::scene::Physics_joint_drive& drive : settings.drives) {
            const Limit_axis_kind kind = (drive.type == erhe::scene::Physics_drive_type::e_angular)
                ? Limit_axis_kind::angular
                : Limit_axis_kind::linear;
            const std::string instance = axis_instance_name(drive.axis, kind);
            apply_api_schema(meta, lightusd::APISchemas::APIName::PhysicsDriveAPI, instance);
            const std::string prefix = std::string{c_physics_drive_prefix} + instance + ":";
            if (drive.mode == erhe::scene::Physics_drive_mode::e_acceleration) {
                add_token_attribute(props, prefix + "type", "acceleration");
            }
            if (std::isfinite(drive.max_force)) {
                add_float_attribute(props, prefix + "maxForce", drive.max_force, Attribute_form::schema_attribute);
            }
            if (drive.position_target != 0.0f) {
                add_float_attribute(
                    props, prefix + "targetPosition",
                    (kind == Limit_axis_kind::angular) ? to_degrees(drive.position_target) : drive.position_target,
                    Attribute_form::schema_attribute
                );
            }
            if (drive.velocity_target != 0.0f) {
                add_float_attribute(
                    props, prefix + "targetVelocity",
                    (kind == Limit_axis_kind::angular) ? to_degrees(drive.velocity_target) : drive.velocity_target,
                    Attribute_form::schema_attribute
                );
            }
            if (drive.stiffness != 0.0f) {
                add_float_attribute(props, prefix + "stiffness", drive.stiffness, Attribute_form::schema_attribute);
            }
            if (drive.damping != 0.0f) {
                add_float_attribute(props, prefix + "damping", drive.damping, Attribute_form::schema_attribute);
            }
        }
    }

    void write_joint_limit(
        std::map<std::string, lightusd::Property>& props,
        lightusd::PrimMeta&                        meta,
        const erhe::scene::Physics_joint_limit&    limit,
        const std::string&                         instance,
        const Limit_axis_kind                      kind
    )
    {
        apply_api_schema(meta, lightusd::APISchemas::APIName::PhysicsLimitAPI, instance);
        const std::string prefix = std::string{c_physics_limit_prefix} + instance + ":";
        if (limit.min.has_value()) {
            add_float_attribute(
                props, prefix + "low",
                (kind == Limit_axis_kind::angular) ? to_degrees(limit.min.value()) : limit.min.value(),
                Attribute_form::schema_attribute
            );
        }
        if (limit.max.has_value()) {
            add_float_attribute(
                props, prefix + "high",
                (kind == Limit_axis_kind::angular) ? to_degrees(limit.max.value()) : limit.max.value(),
                Attribute_form::schema_attribute
            );
        }
        const std::string erhe_prefix = std::string{c_physics_limit_erhe_prefix} + instance;
        if (limit.stiffness.has_value()) {
            add_float_attribute(
                props, erhe_prefix + std::string{c_physics_limit_stiffness_suffix}, limit.stiffness.value(), Attribute_form::custom_attribute
            );
        }
        if (limit.damping != 0.0f) {
            add_float_attribute(
                props, erhe_prefix + std::string{c_physics_limit_damping_suffix}, limit.damping, Attribute_form::custom_attribute
            );
        }
    }

    [[nodiscard]] static auto axis_instance_name(const int axis, const Limit_axis_kind kind) -> std::string
    {
        const char* const axis_name = (axis == 1) ? "Y" : ((axis == 2) ? "Z" : "X");
        return ((kind == Limit_axis_kind::angular) ? std::string{"rot"} : std::string{"trans"}) + axis_name;
    }

    // A physics material item as the `Material` prim it is: a material prim
    // with no surface output, carrying `PhysicsMaterialAPI` and nothing else.
    // That is the prim a file authors a physics material on, and the prim an
    // import gives the item the record names.
    [[nodiscard]] auto write_physics_material_prim(
        const erhe::Typed& item,
        const std::string& prim_name,
        const std::string& prim_path
    ) -> lightusd::Prim
    {
        lightusd::Material usd_material;
        usd_material.name = prim_name;
        // Every value of the item travels through its record: the schema
        // attributes of the description and the `erhe:Physics_material:`
        // attributes of the record's own properties.
        lightusd::Prim prim{usd_material};
        write_physics_material_on_prim(item, prim, prim_path);
        return prim;
    }

    // A physics material as the `Material` prim it is: the four schema
    // attributes and the erhe-only values beside them.
    void write_physics_material_on_prim(const erhe::Item_base& item, lightusd::Prim& prim, const std::string& prim_path)
    {
        const Usd_save_physics_record* const record =
            find_physics_record(m_arguments.physics.materials, m_physics_material_record, item);
        if (record == nullptr) {
            return;
        }
        const erhe::scene::Physics_description* description = physics_description();
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator index = m_physics_material_record.find(&item);
        if ((description == nullptr) || (index->second >= description->materials.size())) {
            return;
        }
        std::map<std::string, lightusd::Property>* props = mutable_props_of(prim);
        if (props == nullptr) {
            add_warning(fmt::format("prim '{}' holds a physics material that its prim class cannot carry", prim_path));
            return;
        }
        const erhe::scene::Physics_material_description& material = description->materials[index->second];
        apply_api_schema(prim, lightusd::APISchemas::APIName::PhysicsMaterialAPI, std::string{});
        add_float_attribute(*props, "physics:staticFriction",  material.static_friction,  Attribute_form::schema_attribute);
        add_float_attribute(*props, "physics:dynamicFriction", material.dynamic_friction, Attribute_form::schema_attribute);
        add_float_attribute(*props, "physics:restitution",     material.restitution,      Attribute_form::schema_attribute);
        const Usd_physics_property* const density = find_record_property(record, "Physics_material.density");
        if (density != nullptr) {
            add_float_attribute(*props, "physics:density", std::strtof(density->value.c_str(), nullptr), Attribute_form::schema_attribute);
        }
        add_combine_mode_attribute(*props, "erhe:Physics_material:friction_combine",    material.friction_combine);
        add_combine_mode_attribute(*props, "erhe:Physics_material:restitution_combine", material.restitution_combine);
        for (const Usd_physics_property& property : record->properties) {
            if (property.name == "Physics_material.density") {
                continue;
            }
            props->emplace(
                fmt::format("erhe:{}:{}", property.name.substr(0, property.name.find('.')), property.name.substr(property.name.find('.') + 1)),
                lightusd::Property{make_node_graph_attribute(property.usd_type, property.value, prim_path), true}
            );
        }
    }

    static void add_combine_mode_attribute(
        std::map<std::string, lightusd::Property>& props,
        const std::string&                         name,
        const erhe::scene::Physics_combine_mode    mode
    )
    {
        if (mode == erhe::scene::Physics_combine_mode::e_average) {
            return; // the erhe default
        }
        lightusd::Attribute attribute;
        attribute.set_value(
            (mode == erhe::scene::Physics_combine_mode::e_minimum ) ? std::string{"minimum" } :
            (mode == erhe::scene::Physics_combine_mode::e_maximum ) ? std::string{"maximum" } :
                                                                      std::string{"multiply"}
        );
        props.emplace(name, lightusd::Property{std::move(attribute), true});
    }

    // A collision filter as the `PhysicsCollisionGroup` prim it is: the three
    // erhe lists it states exactly, the group's own members, and the groups
    // its members do not collide with.
    [[nodiscard]] auto write_collision_group_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::PhysicsCollisionGroup group;
        group.name = prim_name;
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator index = m_physics_filter_record.find(&item);
        const erhe::scene::Physics_description* description = physics_description();
        if ((description != nullptr) && (index != m_physics_filter_record.end()) && (index->second < description->collision_filters.size())) {
            const erhe::scene::Physics_collision_filter_description& filter = description->collision_filters[index->second];
            if (!filter.collision_systems.empty()) {
                add_string_array_attribute(group.props, std::string{c_collision_filter_systems_attribute}, filter.collision_systems);
            }
            if (!filter.collide_with_systems.empty()) {
                add_string_array_attribute(group.props, std::string{c_collision_filter_collide_attribute}, filter.collide_with_systems);
            }
            if (!filter.not_collide_with_systems.empty()) {
                add_string_array_attribute(group.props, std::string{c_collision_filter_not_collide_attribute}, filter.not_collide_with_systems);
            }
            add_relationship_property(
                group.props,
                std::string{c_physics_colliders_includes},
                collect_filter_members(index->second),
                Attribute_form::schema_attribute
            );
            const std::vector<std::string> filtered = collect_filtered_groups(filter);
            if (!filtered.empty()) {
                lightusd::Relationship relationship;
                std::vector<lightusd::Path> paths;
                paths.reserve(filtered.size());
                for (const std::string& path : filtered) {
                    paths.emplace_back(path, "");
                }
                relationship.set(std::move(paths));
                group.filteredGroups = lightusd::RelationshipProperty{relationship};
            }
        }
        write_erhe_properties(item, group);
        return lightusd::Prim{group};
    }

    // The prims of the bodies that name one filter: what the group's
    // collection holds.
    [[nodiscard]] auto collect_filter_members(const std::size_t filter_index) -> std::vector<std::string>
    {
        std::vector<std::string>                      paths;
        const erhe::scene::Physics_description* const description = physics_description();
        if (description == nullptr) {
            return paths;
        }
        for (const erhe::scene::Physics_node_description& entry : description->node_physics) {
            if (!entry.collider.has_value() || !entry.collider.value().filter_index.has_value()) {
                continue;
            }
            if (entry.collider.value().filter_index.value() != filter_index) {
                continue;
            }
            const std::string path = planned_path_of(entry.node.get());
            if (!path.empty()) {
                paths.push_back(path);
            }
        }
        return paths;
    }

    // The groups one filter does not collide with, by the name its list
    // names them with: a group of the scene whose name is in the list.
    [[nodiscard]] auto collect_filtered_groups(
        const erhe::scene::Physics_collision_filter_description& filter
    ) -> std::vector<std::string>
    {
        std::vector<std::string>                      paths;
        const erhe::scene::Physics_description* const description = physics_description();
        if (description == nullptr) {
            return paths;
        }
        for (const std::string& name : filter.not_collide_with_systems) {
            for (std::size_t index = 0, end = description->collision_filters.size(); index < end; ++index) {
                if (description->collision_filters[index].name != name) {
                    continue;
                }
                if (index >= m_arguments.physics.collision_filters.size()) {
                    continue;
                }
                const std::string path = planned_path_of(m_arguments.physics.collision_filters[index].item.get());
                if (!path.empty()) {
                    paths.push_back(path);
                }
            }
        }
        return paths;
    }

    // A joint-settings item as the typeless prim it is: the limits and drives
    // of the settings as the multi-apply instances of the mapping, on a prim
    // that states nothing else.
    [[nodiscard]] auto write_joint_settings_prim(const erhe::Typed& item, const std::string& prim_name) -> lightusd::Prim
    {
        lightusd::Model model;
        model.name = prim_name;
        const std::map<const erhe::Item_base*, std::size_t>::const_iterator index = m_physics_settings_record.find(&item);
        const erhe::scene::Physics_description* description = physics_description();
        if ((description != nullptr) && (index != m_physics_settings_record.end()) && (index->second < description->joints.size())) {
            write_joint_limits_and_drives(model.props, model.meta, description->joints[index->second]);
        }
        write_active(item, model);
        write_erhe_properties(item, model, Native_property_form::custom_attributes);
        return lightusd::Prim{model};
    }

    [[nodiscard]] static auto to_degrees(const float radians) -> float
    {
        return radians * (180.0f / glm::pi<float>());
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

    // The physics records of the arguments, by the item each names, and where
    // every prim of the tree landed (doc/usd_compatibility.md, "Physics").
    std::map<const erhe::Item_base*, std::size_t>              m_physics_body_index;
    std::map<const erhe::Item_base*, std::size_t>              m_physics_body_record;
    std::map<const erhe::Item_base*, std::size_t>              m_physics_material_record;
    std::map<const erhe::Item_base*, std::size_t>              m_physics_filter_record;
    std::map<const erhe::Item_base*, std::size_t>              m_physics_settings_record;
    std::map<const erhe::Item_base*, std::vector<std::size_t>> m_physics_synthesized_indices;
    std::map<const erhe::Item_base*, std::string>              m_planned_paths;
    std::string                                                m_physics_scene_parent_path;
    std::string                                                m_physics_scene_name;

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
