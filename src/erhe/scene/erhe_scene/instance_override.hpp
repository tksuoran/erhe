#pragma once

#include "erhe_scene/xform_op.hpp"

#include "erhe_property/owner_type.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe {
    class Hierarchy;
    class Item_base;
}

namespace erhe::primitive {
    class Material;
}

namespace erhe::property {
    class Dependency_object;
    class Dependency_property;
}

namespace erhe::scene {

class Node_attachment;

// Whether an override entry supplies a value at all. `cleared` is the state
// of a property that has no local value: an override list that describes what
// a prim held before a variant's opinions were applied needs to say "this
// property was not authored here", so restoring it clears the local value
// instead of writing one.
enum class Instance_override_value_state : unsigned int {
    supplied = 0,
    cleared  = 1
};

// One property value an instance item overrides, by the qualified name the
// property registry gives it (`Owner.name`, or `name` for a property of the
// item's own class) and the D16 text form of the value.
class Instance_override_value final
{
public:
    std::string                  name;
    std::string                  text;
    Instance_override_value_state state{Instance_override_value_state::supplied};
};

// One material binding an instance item overrides, as the item form carries
// it: the index of the primitive in Mesh::get_primitives() whose material
// differs from the counterpart's, and the material bound there. A writer
// names the group of facets the index stands for the way it names the mesh's
// own groups (a USD GeomSubset, one per primitive).
class Instance_override_material final
{
public:
    std::size_t                                primitive_index{0};
    std::shared_ptr<erhe::primitive::Material> material;
};

// The overrides one item inside a prefab instance holds
// (doc/usd_compatibility_design.md X2). `relative_path` is the M1 path of the
// item below the arc's target clone, so an empty path is the target clone
// itself; the values are the item's own overrides and `transform` is its
// local transform, carried only when it differs from the counterpart's.
class Instance_override final
{
public:
    std::string                          relative_path;
    std::vector<Instance_override_value> values;
    bool                                 transform_overridden{false};
    glm::mat4                            transform           {1.0f};
    // The material the item's mesh binds, by the path of the material item -
    // an absolute stage path in a USD file, the M1 path of the material item
    // in a glTF file. Empty when the override binds nothing. A binding that
    // covers one group of facets rather than the whole mesh is an entry of
    // its own whose relative path ends in the name of the group, the way a
    // USD GeomSubset is a prim below its mesh and X4's variant bindings name
    // one.
    std::string                          material_path;
    // The authored xformOp stack of the transform, when the item carries one
    // (doc/usd_compatibility_design.md M8). An item without one is described by
    // `transform` alone.
    std::optional<Xform_op_stack>        xform_op_stack;
};

// One item inside a prefab instance that holds overrides, as the item itself:
// what a writer that reads the values in its own file format needs.
class Instance_override_item final
{
public:
    std::string            relative_path;
    const erhe::Item_base* item                {nullptr};
    bool                   transform_overridden{false};
    // The primitives of the item's mesh whose material differs from the
    // counterpart's.
    std::vector<Instance_override_material> materials;
};

// What an override of an instance item is (doc/usd_compatibility_design.md X2,
// doc/property_system.md D33), stated once:
//
// - a local value of a serializable, non-bridged, non-computed property
//   without an expression: it shadows what the reference layer supplies;
// - a local transform that differs from the counterpart's. The transform is
//   bridged, so it always has a local value and only a difference is an
//   override;
// - a material bound to a primitive of the item's mesh that differs from the
//   material the counterpart's primitive at the same index binds.
//
// The item name is structure rather than value and is never an override.
//
// `carrier` is the referencing item: its children are the arcs' clones of the
// target prims, and every item below one that names a counterpart
// (Dependency_object::get_reference) is instance content. An item that names
// none was parented under the carrier by hand and is not instance content, so
// neither it nor its subtree is reported.
[[nodiscard]] auto collect_instance_override_items(const erhe::Hierarchy& carrier) -> std::vector<Instance_override_item>;

// The same walk, with every override read out into the format-neutral value
// form above, so it survives the items it was read from.
[[nodiscard]] auto collect_instance_overrides(const erhe::Hierarchy& carrier) -> std::vector<Instance_override>;

// Set the named values of `values` on `item`: each entry names a property the
// way a file spells it (qualified `Owner.name`, or the bare name of a property
// of the item's own class) and carries the D16 text form of the value. `owner`
// names the source of the values in a warning - the instance, the class prim -
// and a value naming no property, or one that does not parse, costs one
// warning each. This is what both a sparse instance override and an imported
// USD `class` prim's opinions (doc/usd_compatibility_design.md X3) are made of.
void apply_property_values(
    erhe::Item_base&                            item,
    const std::vector<Instance_override_value>& values,
    std::string_view                            owner
);

// The property `name` addresses on `object`: the registry's own lookup for
// the object, and a qualified `Owner.name` that a file spells for a property
// the object holds under its bare name. Null when the name reaches no
// property. This is how a file's spelling of a property is resolved, wherever
// the spelling comes from - an instance override, a class prim's opinions, a
// variant's opinions.
[[nodiscard]] auto find_override_property(
    const erhe::property::Dependency_object& object,
    const std::string&                       name
) -> const erhe::property::Dependency_property*;

// Where one override value's name lands: the object that holds the property
// and the property itself, both null when the name reaches neither. A name
// qualified with the class name of an applied-schema attachment of the prim
// (`Draw_mode.card_geometry`, `Node_physics.mass`) resolves to that
// attachment's property, which is how USD's spelling - an applied schema
// authors its attributes on the prim itself - reaches the erhe item that
// holds them. Every other name resolves on the item, through
// find_override_property.
class Override_property_target final
{
public:
    erhe::property::Dependency_object*         object  {nullptr};
    const erhe::property::Dependency_property* property{nullptr};
};

[[nodiscard]] auto find_override_property_target(
    erhe::Item_base&   item,
    const std::string& name
) -> Override_property_target;

// How an attachment standing for an applied API schema is made. USD applies a
// schema to a prim - `prepend apiSchemas = ["GeomModelAPI"]` - and the schema
// authors its attributes on that prim, so a file's opinion of one reaches a
// prim erhe has given no such attachment yet: a variant block that applies
// the schema and authors its attributes is exactly that, and the block
// applying the schema is what makes the schema present. A class of attachment
// registers the owner type its properties are registered on and how one is
// made, and find_override_property_target makes one on the prim the first
// time an opinion names it. erhe::scene names no editor class, so the editor
// registers its own from startup, while the process is still single threaded
// and no scene exists.
using Applied_schema_attachment_factory = std::function<std::shared_ptr<Node_attachment>()>;
void register_applied_schema_attachment(
    std::string_view                  class_name,
    erhe::property::Owner_type        owner_type,
    Applied_schema_attachment_factory factory
);

// True while a class of that name has registered a factory. The pairing of an
// instance with its template asks this: an applied schema's attachment is the
// prim's own USD state, so a carrier's one reads its target's through the
// reference layer, while every other attachment is paired by the clone walk
// alone.
[[nodiscard]] auto is_applied_schema_attachment_class(std::string_view class_name) -> bool;

// Put `overrides` back on the items of a freshly attached instance: each
// entry names the item at its relative path below the first of the carrier's
// children that has one, except an entry whose path ends in the name of a
// group of facets, which names a primitive of the mesh the rest of the path
// names. A material binding is resolved below the carrier first and in the
// scene tree the carrier stands in otherwise. An entry naming no item, a
// binding naming no material, a value naming no property and a value that
// does not parse cost one warning each.
void apply_instance_overrides(erhe::Hierarchy& carrier, const std::vector<Instance_override>& overrides);

// The item `relative_path` names below a carrier - a prim a composition arc
// was applied to (doc/usd_compatibility_design.md C6). erhe keeps the arc's
// target clone as one level of its own while USD composes the target's
// content directly under the referencing prim, so the path is resolved one
// segment at a time and the clone of every carrier the path crosses is
// looked through first: the carrier the path starts at, a carrier the path
// reaches below it, and a carrier that is itself the clone of an outer one.
// An empty path is the carrier's first clone. Null when the path reaches no
// item.
[[nodiscard]] auto find_instance_item(erhe::Hierarchy& carrier, const std::string& relative_path) -> erhe::Hierarchy*;

} // namespace erhe::scene
