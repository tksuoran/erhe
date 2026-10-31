#pragma once

#include "erhe_property/dependency_property.hpp"

#include <cstddef>
#include <optional>

namespace erhe::scene {

class Xformable; using Node = Xformable;

// How the meshes at and below a node pick their level of detail
// (doc/erhe/scene.md "Level of detail control", doc/plans/lod.md section 2.5).
enum class Lod_mode : unsigned int {
    automatic = 0, // runtime selection (doc/plans/lod.md step 4); level 0 until then
    forced,        // draw Lod_data::forced_level, clamped to min / max and to the chain
    off            // draw level 0
};

extern const erhe::property::Enum_info c_lod_mode_enum_info;

// The effective values of the `Lod` group for one mesh: a plain record, so the
// draw-list objects it is mirrored into hold no reference to the scene. The
// default record is what a mesh with no carrier at or above it resolves to.
class Lod_data
{
public:
    [[nodiscard]] auto operator==(const Lod_data& other) const -> bool = default;

    Lod_mode mode        {Lod_mode::automatic};
    int      forced_level{0};
    int      bias        {0};
    int      min_level   {0};
    int      max_level   {7};
};

// The level a record asks every draw of the mesh to use before automatic
// selection exists: clamp(forced_level, min_level, max_level) under
// Lod_mode::forced (a min_level below 0 counts as 0, a max_level below
// min_level as min_level), 0 under automatic and off. The caller clamps it to
// the chain the draw reads.
[[nodiscard]] auto get_forced_lod_level(const Lod_data& data) -> std::size_t;

// Level of detail control as an attached value group of the node
// (doc/erhe/property_system.md sections 4.23 and 4.29): owner type `Lod`,
// holder type `erhe::scene::Node`, qualified `Lod.apply_lod` .. `Lod.max_level`.
// A registration holder with static members only, like `Layout`. The runtime
// state the group implies - the effective record of every mesh, mirrored into
// the draw lists through Scene_host::on_mesh_lod_changed - is owned by
// `Lod_system`, one per scene.
class Lod
{
public:
    Lod() = delete;

    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The KEY property, registered first and with inherits = false: the node
    // carries its own LOD opinion exactly while it is true.
    static const erhe::property::Property<bool>     apply_lod_property;

    // The values. Each one inherits, so a group node carrying the key applies
    // them to the meshes of its subtree.
    static const erhe::property::Property<Lod_mode> mode_property;
    static const erhe::property::Property<int>      forced_level_property;
    static const erhe::property::Property<int>      bias_property;
    static const erhe::property::Property<int>      min_level_property;
    static const erhe::property::Property<int>      max_level_property;
};

// True while the node carries the group: Lod.apply_lod is true on it
// (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_lod(const Node& node) -> bool;

// The node's own effective values, or nothing when the node does not carry
// the group.
[[nodiscard]] auto read_lod(const Node& node) -> std::optional<Lod_data>;

// The record that applies to the meshes of `node`: the node's effective values
// when the node or one of its ancestors carries the group, the default record
// otherwise. The values inherit through the node chain on their own, so the
// carrier only decides whether they apply; this is a change-time reader that
// walks the ancestors, never a per-frame one.
[[nodiscard]] auto resolve_lod(const Node& node) -> Lod_data;

} // namespace erhe::scene
