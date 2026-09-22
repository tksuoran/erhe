#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

#include <geogram/mesh/mesh.h>

#include <memory>
#include <optional>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Brush;

// The effective brush-placement values of one node: which brush the node is
// an instance of, and the facet and corner of the hovered geometry the
// instance was seated on. A plain record, read with read_brush_placement().
class Brush_placement_data
{
public:
    std::shared_ptr<Brush> brush;
    GEO::index_t           facet {GEO::NO_INDEX};
    GEO::index_t           corner{GEO::NO_INDEX};
};

// The brush placement of a node as an attached value group of the node
// itself (doc/erhe/property_system.md sections 4.11 and 4.23).
//
// Brush_placement is a registration holder with static members only, not an
// item and not a Dependency_object: it owns the property registrations
// (owner type Brush_placement, so the qualified names are
// Brush_placement.brush, .facet and .corner) and the holder of every value
// is an erhe::scene::Node.
//
// Brush_placement.brush is the group's KEY property, with the null reference
// as its default: the node is a brush placement exactly while something
// names a brush on it. None of the three values is saved (D5), so all three
// are registered without Property_flags::serialize and no file carries them.
class Brush_placement
{
public:
    Brush_placement() = delete;

    // The registering class's owner type id. Brush_placement has no
    // instances, so it is allocated directly under the root rather than by
    // Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property, registered first (D1). A strong object reference,
    // like every other reference naming a content-library resource: the
    // node's reference is an ordinary resource usership and it dies with the
    // node. Validated to null or a Brush.
    static const erhe::property::Property<erhe::property::Object_reference> brush_property;

    // The seat of the instance on the geometry it was placed against, -1
    // being GEO::NO_INDEX. Developer-only rows, listed on the nodes that
    // carry the group (attached_group_visible_when).
    static const erhe::property::Property<int>                              facet_property;
    static const erhe::property::Property<int>                              corner_property;

    // Every Brush_placement.* property, registration order, for generic walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// True while the node carries the group: the key property's effective value
// differs from its default (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_brush_placement(const erhe::scene::Node& node) -> bool;

// The effective values of one node, or nothing when the node names no brush.
[[nodiscard]] auto read_brush_placement(const erhe::scene::Node& node) -> std::optional<Brush_placement_data>;

// Seats the node on a brush. Local values only where the argument differs
// from the property default, so a placement without a facet stays open to a
// holder supplying one.
void set_brush_placement(
    erhe::scene::Node&            node,
    const std::shared_ptr<Brush>& brush,
    GEO::index_t                  facet,
    GEO::index_t                  corner
);

// Rotates the instance to another corner of its facet.
void set_brush_placement_corner(erhe::scene::Node& node, GEO::index_t corner);

} // namespace editor
