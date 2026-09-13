#pragma once

// Internal header: it includes LightUSD headers and is included only by
// erhe::usd's own translation units, never by a client of erhe::usd.

#include "erhe_usd/usd.hpp"

#include "erhe_scene/animation.hpp"
#include "erhe_scene/xform_op.hpp"

#include "layer.hh"
#include "stage.hh"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::usd {

// How an OpenPBR network travels in a USD file
// (doc/usd-compatibility-plan.md E2). A `Material` prim offers it beside its
// `UsdPreviewSurface` through `outputs:mtlx:surface`, and it is a `Shader`
// child whose `info:id` is one of the surface-node spellings Tydra converts
// into `RenderMaterial::openPBRShader`. The reader and the writer both spell
// them from here.
constexpr std::string_view c_open_pbr_shader_prim_name  {"open_pbr"};
constexpr std::string_view c_open_pbr_info_id           {"ND_open_pbr_surface_surfaceshader"};
constexpr std::string_view c_open_pbr_standard_info_id  {"ND_standard_surface_surfaceshader"};
constexpr std::string_view c_open_pbr_schema_info_id    {"OpenPBRSurface"};

// How a brush travels in a USD file (doc/usd-compatibility-plan.md E4a). USD
// has no schema for a brush, so the prim's `typeName` is the erhe class token
// - the same token the writer gives every `Typed` prim - its geometry is a
// child `Mesh` prim of a fixed name, and the two fields no erhe property
// carries are `erhe:Brush:` custom attributes named as the glTF fields are.
// The reader and the writer both spell them from here.
constexpr std::string_view c_brush_prim_type_name           {"Brush"};
constexpr std::string_view c_brush_geometry_prim_name       {"geometry"};
constexpr std::string_view c_brush_density_attribute        {"erhe:Brush:density"};
constexpr std::string_view c_brush_normal_style_attribute   {"erhe:Brush:normal_style"};
// The same two in the neutral `Owner.name` form read_spec_values hands back.
constexpr std::string_view c_brush_density_value_name       {"Brush.density"};
constexpr std::string_view c_brush_normal_style_value_name  {"Brush.normal_style"};

// How a node graph travels in a USD file (doc/usd-texture-graphs-plan.md 2.1
// and section 4). The graph is a `NodeGraph` prim carrying the marker
// attribute that says it is erhe's - a `NodeGraph` without it is a foreign
// shading network (R5) - each node is a `Shader` child whose `info:id` is the
// node's factory type name under the namespace prefix its graph's format
// names (`node_graph_node_id_prefix`), and the node's editor position is one
// custom attribute. A geometry graph holds its evaluated geometry as a child
// `Mesh` of a fixed name, the way a brush holds its own. The reader and the
// writer both spell them from here.
constexpr std::string_view c_node_graph_prim_type_name     {"NodeGraph"};
constexpr std::string_view c_node_graph_format_attribute   {"erhe:graph:format"};
constexpr std::string_view c_node_graph_position_attribute {"erhe:ui:position"};
constexpr std::string_view c_node_graph_result_prim_name   {"result"};
constexpr std::string_view c_node_graph_input_prefix       {"inputs:"};
constexpr std::string_view c_node_graph_output_prefix      {"outputs:"};
constexpr std::string_view c_node_graph_shader_prim_type_name{"Shader"};
constexpr std::string_view c_node_graph_info_id_attribute  {"info:id"};

// The USD schema token of a point instancer (doc/usd-compatibility-plan.md
// S1). The reader dispatches on it and the writer spells it, so both name it
// from here; the erhe class is erhe::scene::Point_instancer.
constexpr std::string_view c_point_instancer_prim_type_name {"PointInstancer"};

// The USD schema token of a skeleton (doc/usd-compatibility-plan.md K1). A
// `Skeleton` prim is a transformable prim of the erhe tree carrying this
// token, holding one `Xform` prim per joint; the reader dispatches on the
// token and the writer spells it, so both name it from here.
constexpr std::string_view c_skeleton_prim_type_name {"Skeleton"};

// How physics travels in a USD file (doc/usd_compatibility.md, "Physics").
// The schema tokens the reader dispatches on and the writer spells, the
// namespaces of the multi-apply limit and drive instances, and the erhe-only
// attributes and relationships of the mapping. The reader and the writer both
// name them from here.
constexpr std::string_view c_physics_scene_prim_type_name           {"PhysicsScene"};
constexpr std::string_view c_physics_joint_prim_type_name           {"PhysicsJoint"};
constexpr std::string_view c_physics_revolute_joint_prim_type_name  {"PhysicsRevoluteJoint"};
constexpr std::string_view c_physics_prismatic_joint_prim_type_name {"PhysicsPrismaticJoint"};
constexpr std::string_view c_physics_spherical_joint_prim_type_name {"PhysicsSphericalJoint"};
constexpr std::string_view c_physics_fixed_joint_prim_type_name     {"PhysicsFixedJoint"};
constexpr std::string_view c_physics_distance_joint_prim_type_name  {"PhysicsDistanceJoint"};
constexpr std::string_view c_physics_collision_group_prim_type_name {"PhysicsCollisionGroup"};
// The name the writer gives the child prim holding a body's implicit shape.
constexpr std::string_view c_physics_collider_prim_name             {"collider"};
// The multi-apply instances of a joint: `physics:limit:<axis>:low` and
// `physics:drive:<axis>:targetPosition` are the full attribute spellings the
// `limit` / `drive` property namespace prefixes of the schema produce.
constexpr std::string_view c_physics_limit_prefix                   {"physics:limit:"};
constexpr std::string_view c_physics_drive_prefix                   {"physics:drive:"};
// The physics-purpose material binding of a body or collider prim.
constexpr std::string_view c_physics_material_binding               {"material:binding:physics"};
// The collection of body prims a `PhysicsCollisionGroup` prim filters.
constexpr std::string_view c_physics_colliders_includes             {"collection:colliders:includes"};
// The erhe-only values of the mapping. The two tapered-shape radii and the
// soft-limit spring of one axis have no UsdPhysics attribute, and the exact
// collision-system lists of a filter are more than `filteredGroups` says.
constexpr std::string_view c_physics_shape_radius_bottom_attribute  {"erhe:Physics_shape:radius_bottom"};
constexpr std::string_view c_physics_shape_radius_top_attribute     {"erhe:Physics_shape:radius_top"};
constexpr std::string_view c_physics_limit_erhe_prefix              {"erhe:limit:"};
constexpr std::string_view c_physics_limit_stiffness_suffix         {":stiffness"};
constexpr std::string_view c_physics_limit_damping_suffix           {":damping"};
constexpr std::string_view c_collision_filter_systems_attribute     {"erhe:Collision_filter:collision_systems"};
constexpr std::string_view c_collision_filter_collide_attribute     {"erhe:Collision_filter:collide_with_systems"};
constexpr std::string_view c_collision_filter_not_collide_attribute {"erhe:Collision_filter:not_collide_with_systems"};
constexpr std::string_view c_node_physics_gravity_factor_attribute  {"erhe:Node_physics:gravity_factor"};
constexpr std::string_view c_node_physics_is_trigger_attribute      {"erhe:Node_physics:is_trigger"};
constexpr std::string_view c_node_joint_settings_relationship       {"erhe:Node_joint:joint_settings"};

// One prim a variant block authors as a `def` child and the loader hoisted
// out of it, into the tree below the prim carrying the set
// (doc/usd-compatibility-plan.md X4). USD builds such a prim when its variant
// is selected and takes it
// away again on a switch; erhe keeps every variant's prims in the tree and
// flips `active`, so a switch is a property write like every other one.
class Variant_prim_record final
{
public:
    // Stage path of the prim carrying the variant set.
    std::string carrier_path;
    std::string set_name;
    std::string variant_name;
    // The block the set is declared in, both empty when the prim declares the
    // set itself: what keeps a nested set's prims apart from a top-level
    // set's of the same name.
    std::string enclosing_set_name;
    std::string enclosing_variant_name;
    // The name the hoisted prim has below the carrier, sibling-unique by the
    // M2 rule: two variants of one set are free to author the same name and
    // the tree is not.
    std::string prim_name;
    // The name the variant block gave it, which is what a save writes back.
    std::string authored_name;
};

// One `UsdPreviewSurface` input a `UsdPrimvarReader` feeds, as load_stage
// read it off the composed layer before it built the stage
// (src/erhe/usd/notes.md, "UsdPreviewSurface fallbacks and channel outputs").
// Tydra resolves a shading input to a `UsdUVTexture` or fails the whole
// material over it, so load_stage takes the connection out of the layer copy
// the stage is built from - the way it takes the erhe texture-graph wiring
// out - and records what it took here for the importer to apply.
class Primvar_input_record final
{
public:
    // Stage path of the `Material` prim the shader belongs to: what the
    // importer has in hand, the shader prim sitting an unknown number of
    // `NodeGraph` levels below it.
    std::string material_path;
    // The input the connection was on, without the `inputs:` prefix
    // (`diffuseColor` or `opacity`).
    std::string input_name;
    // `inputs:varname` of the `UsdPrimvarReader` prim it named.
    std::string primvar_name;
};

class Stage::Impl final
{
public:
    lightusd::Stage       stage;
    std::filesystem::path source_path;
    // The layer `stage` was built from, kept by load_stage: the root layer of
    // `source_path` composed with its `subLayers`, with the prims of every
    // variant block hoisted into it - exactly the spec tree behind the prims
    // of `stage`. It is the source of everything LightUSD does not compose -
    // the `class` prims, the `over` opinions, the `variantSet` blocks - which
    // the importer reads back off it rather than re-reading the file for
    // itself, so a prim any layer of the stack authors contributes those the
    // way a root-layer prim does.
    lightusd::Layer       layer;
    bool                  layer_ok{false};
    // The prims the variant blocks of `layer` authored, in the tree of
    // `stage` since load_stage hoisted them there.
    std::vector<Variant_prim_record> variant_prims;
    // The `variants` selection a composition arc carried into this load, as
    // load_stage validated it against `layer`: the entries naming a set the
    // prim declares and a variant that set holds. The hoist applied it and the
    // importer applies it again for the opinions and the arcs of the selected
    // variant, both before the prim's own `variants` metadatum.
    Usd_variant_selections           variant_selections;
    // The `UsdPrimvarReader` connections load_stage took out of the layer the
    // stage was built from, so Tydra converts the rest of those materials.
    std::vector<Primvar_input_record> primvar_inputs;
};

// Whether every variant block a nested `variantSet` is declared inside is the
// selected one of its own set: what decides whether the nested set's blocks
// contribute at all. A set the prim declares itself is always `selected`.
enum class Variant_branch_state : unsigned int {
    selected   = 0,
    unselected = 1
};

// Which variant of one `variantSet` a prim composes to, in the strength order
// USD resolves it in: the selection a composition arc carried into this load
// (section 2 C7), then the `variants` metadatum of the variant block the set
// is declared inside - null for a set the prim declares itself - then the
// prim's own `variants` metadatum, then the first block. load_stage's hoist
// and the importer both take the selection from here, so both read one rule.
// A carried selection is validated before the hoist, so it names a variant
// the set holds.
[[nodiscard]] auto resolve_selected_variant_name(
    const Usd_variant_selections&              variant_selections,
    const std::string&                         absolute_prim_path,
    const lightusd::VariantSelectionMap*       enclosing_block_selection,
    const lightusd::VariantSelectionMap&       prim_selection,
    const std::string&                         set_name,
    const lightusd::VariantSetSpec&            set
) -> std::string;

// A USDA literal rewritten in erhe's property text form (D16), defined by
// usd_import_physics.cpp and used by both import translation units.
[[nodiscard]] auto usd_literal_to_property_text(const std::string& literal) -> std::string;

// Everything read_usd_physics needs from the scene conversion that ran
// before it: the stage, and the prims of the erhe tree the physics records
// name, by stage path.
class Usd_physics_read_arguments final
{
public:
    const lightusd::Stage&                                           stage;
    // The composed layer the stage was built from, null when load_stage kept
    // none. LightUSD's prim reconstruction keeps an applied API schema's
    // attributes on the prim and drops its relationships, so every
    // relationship a physics record names is read off the layer's prim specs,
    // which carry what the file spells.
    const lightusd::Layer*                                           layer;
    const std::map<std::string, std::shared_ptr<erhe::scene::Node>>& nodes_by_path;
    const std::map<std::string, std::shared_ptr<erhe::scene::Mesh>>& meshes_by_path;
    std::string                                                      file_name;
};

// Fill `data.physics` and `data.physics_prims` from the `UsdPhysics` content
// of the stage (doc/usd_compatibility.md, "Physics", and
// src/erhe/usd/notes.md, "Physics"). Every issue the read reports is
// appended to `warnings` as one line, for the caller to put into
// `Usd_load_result::warning`.
void read_usd_physics(
    const Usd_physics_read_arguments& arguments,
    Usd_data&                         data,
    std::vector<std::string>&         warnings
);

// The sampled-transform vocabulary the reader and the writer share
// (src/erhe/usd/notes.md, "Time samples"). The reader (usd_import.cpp) owns
// the definitions; the writer reads them to reconcile an edited clip's keys
// with the samples the file authored.

// Which erhe animation path an op drives, and in which order the three may
// appear. False when the op drives none - a `transform` matrix op.
[[nodiscard]] auto get_xform_op_animation_path(
    erhe::scene::Xform_op_type   type,
    erhe::scene::Animation_path& out_path
) -> bool;

// Why a stack's time samples cannot become one channel per op, or an empty
// string when they can. A stack this refuses is baked into three channels of
// its composed pose instead.
[[nodiscard]] auto get_xform_op_stack_animation_refusal(const erhe::scene::Xform_op_stack& stack) -> std::string;

// The value an op has at one time code: USD's time sample semantics for a
// floating-point attribute.
[[nodiscard]] auto get_xform_op_value_at(const erhe::scene::Xform_op& op, double time_code) -> erhe::scene::Xform_op_value;

// The rotation one sample of a rotate / orient op holds, as a quaternion.
[[nodiscard]] auto get_xform_op_sample_rotation(
    const erhe::scene::Xform_op&        op,
    const erhe::scene::Xform_op_sample& sample
) -> glm::dquat;

} // namespace erhe::usd
