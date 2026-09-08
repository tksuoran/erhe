#pragma once

#include "erhe_scene/xform_op.hpp"

#include <glm/glm.hpp>

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

namespace erhe::scene {

// One property value an instance item overrides, by the qualified name the
// property registry gives it (`Owner.name`, or `name` for a property of the
// item's own class) and the D16 text form of the value.
class Instance_override_value final
{
public:
    std::string name;
    std::string text;
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
// (doc/usd-compatibility-plan.md X2). `relative_path` is the M1 path of the
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
    // (doc/usd-compatibility-plan.md M8). An item without one is described by
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

// What an override of an instance item is (doc/usd-compatibility-plan.md X2,
// doc/property-system.md D33), stated once:
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
// USD `class` prim's opinions (doc/usd-compatibility-plan.md X3) are made of.
void apply_property_values(
    erhe::Item_base&                            item,
    const std::vector<Instance_override_value>& values,
    std::string_view                            owner
);

// Put `overrides` back on the items of a freshly attached instance: each
// entry names the item at its relative path below the first of the carrier's
// children that has one, except an entry whose path ends in the name of a
// group of facets, which names a primitive of the mesh the rest of the path
// names. A material binding is resolved below the carrier first and in the
// scene tree the carrier stands in otherwise. An entry naming no item, a
// binding naming no material, a value naming no property and a value that
// does not parse cost one warning each.
void apply_instance_overrides(erhe::Hierarchy& carrier, const std::vector<Instance_override>& overrides);

} // namespace erhe::scene
