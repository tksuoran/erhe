#pragma once

#include "level/kit_layout.hpp"
#include "level/kit_piece_operation.hpp"
#include "level/level_group.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_queries.hpp"
#include "level/level_repeat.hpp"

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "erhe_property/property_value.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe           { class Hierarchy; class Item_base; class Item_host; }
namespace erhe::property { class Dependency_property; }
namespace erhe::primitive { class Material; }
namespace erhe::scene    { class Mesh; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class App_message_bus;
class Brush;
class Operation;
class Scene_root;

// The material of one slot of a piece (D3.4): a material of the scene, else a palette material by name (found in
// the scene's content library or added to it), else, with both empty, the slot's default palette material.
class Kit_slot_material
{
public:
    std::shared_ptr<erhe::primitive::Material> material;
    std::string                                palette_name;
};

// One piece to place (D3.2); positions and rotations are in the scene root's space.
class Kit_placement
{
public:
    const Kit_shape_definition*                shape{nullptr};
    std::optional<Kit_parameter_values>        values;       // none: the defaults at the scene's lengths
    std::shared_ptr<erhe::Hierarchy>           parent;       // none: the scene root
    std::optional<std::size_t>                 parent_index; // an earlier placement of the same call
    glm::vec3                                  position{0.0f};
    glm::quat                                  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string                                name;         // empty: the shape's display name
    std::vector<Kit_slot_material>             materials;    // by slot index; a slot without an entry takes its default
    Surface_options                            surface;      // applied after the module snap (D7.3)
    std::optional<std::size_t>                 index_in_parent; // none: after the parent's children
};

// Whether the Brush tool's kit values take the next seed after each placed piece (D4.5).
enum class Kit_seed_per_piece : unsigned int {
    same = 0,
    next
};

// The placement settings of the Level Kit window and the Brush tool's kit mode (D4.4).
class Kit_placement_settings
{
public:
    Kit_snap           snap{Kit_snap::none};
    Surface_options    surface;
    float              rotation_step_degrees{90.0f};
    Kit_seed_per_piece seed_per_piece{Kit_seed_per_piece::same};
};

// Where a piece placed at a point rests: the transform the kit mode preview shows and a click places.
class Kit_placement_pose
{
public:
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool      surface_found{false};
};

class Kit_piece_data;

// Whether a piece's mesh and collision shape are its kit brush's (D3.5, R8.3, R8.4).
class Kit_piece_check
{
public:
    bool        follows_kit{false};
    std::string problem;    // why it does not follow; empty when it does
};

class Kit_refresh_report
{
public:
    std::size_t piece_count  {0};
    std::size_t follow_count {0};
    std::size_t problem_count{0};
};

enum class Kit_snap_apply : unsigned int {
    move = 0,
    query_only
};

class Kit_snap_result
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    Surface_placement                  placement;
};

class Kit_placed_piece
{
public:
    std::shared_ptr<erhe::scene::Node>         node;
    std::shared_ptr<Brush>                     brush;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials; // by slot index
    Kit_parameter_values                       values;
    glm::vec3                                  position{0.0f};
    glm::quat                                  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool                                       surface_found{false}; // false also when no surface option was set
};

class Kit_place_result
{
public:
    std::vector<Kit_placed_piece> pieces;
    std::string                   error;       // empty on success; with an error nothing is placed
    std::size_t                   error_index{0};
};

// Where the copy of a group goes (D3.8): its source's world transform moved by an offset, or at a position.
enum class Kit_copy_placement : unsigned int {
    offset = 0,
    position
};

class Kit_group_copy
{
public:
    Kit_copy_placement placement{Kit_copy_placement::offset};
    glm::vec3          value{0.0f}; // the world offset, or the copy's world position
    std::string        name;        // empty: the source's name followed by " Copy"
};

class Kit_group_result
{
public:
    std::shared_ptr<erhe::scene::Node> node;  // the new group node, or the copy; null with an error
    std::string                        error; // empty on success; with an error nothing changed
};

class Kit_ungroup_result
{
public:
    std::shared_ptr<erhe::Hierarchy>              parent;   // the group's parent, which now holds its children
    std::vector<std::shared_ptr<erhe::Hierarchy>> children;
    std::string                                   error;
};

// One node a mirror moved: its new world transform and whether it shows the exact mirror image.
class Kit_mirrored_node
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    glm::vec3                          position{0.0f};
    glm::quat                          rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Kit_mirror_plane                   plane{Kit_mirror_plane::x};
    Mirror_image                       image{Mirror_image::exact};
};

class Kit_mirror_result
{
public:
    glm::vec3                      plane_point {0.0f};
    glm::vec3                      plane_normal{1.0f, 0.0f, 0.0f};
    std::vector<Kit_mirrored_node> nodes;       // the group's node descendants, parents first
    std::size_t                    not_exact_count{0};
    std::string                    error;
};

class Kit_set_material_result
{
public:
    std::shared_ptr<erhe::primitive::Material> material; // the slot's material now
    std::shared_ptr<erhe::primitive::Material> previous;
    bool                                       changed{false};
    std::string                                error;    // empty on success; with an error nothing changed
};

class Kit_set_parameters_result
{
public:
    std::shared_ptr<Brush> brush; // the brush of the new values
    std::string            error; // empty on success; with an error nothing changed
};

// Which children a kit layout regeneration keeps (D3.9): the ones that already match, or none (a lock, R5.12.5).
enum class Kit_layout_reuse : unsigned int {
    keep_matching = 0,
    replace_all
};

// A new kit layout node (D3.9): its values, parent (none: the scene root) and world transform.
class Kit_layout_create
{
public:
    Kit_layout_data                  data;
    std::shared_ptr<erhe::Hierarchy> parent;
    glm::vec3                        position{0.0f};
    glm::quat                        rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string                      name;     // empty: "Kit Layout"
};

// A kit layout node made from a placed piece (D3.9, D4.9): the piece's shape, values and slot materials are the
// one item, the node takes the piece's place (parent, index, world transform) and the piece is removed.
class Kit_layout_from_piece
{
public:
    std::shared_ptr<erhe::scene::Mesh> piece;
    Kit_layout_pattern                 pattern;
    Kit_snap                           snap          {Kit_snap::none};
    Surface_snap                       surface_snap  {Surface_snap::off};
    Surface_orient                     surface_orient{Surface_orient::off};
    Kit_layout_seeds                   seeds         {Kit_layout_seeds::item};
    int                                seed          {0};
    Kit_layout_lock                    lock          {Kit_layout_lock::locked};
    std::string                        name;          // empty: the piece's name followed by " Layout"
};

class Kit_layout_result
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    Kit_layout_run                     run;               // the run the values make
    float                              item_extent{0.0f}; // the largest item extent along x
    std::size_t                        kept_count   {0};
    std::size_t                        added_count  {0};
    std::size_t                        removed_count{0};
    bool                               changed{false};    // an undo entry was recorded
    std::string                        error;             // empty on success; with an error nothing changed
};

// The level kit editor part (D2.2): the shape library, the kit brush cache and piece placement.
class Level_kits
{
public:
    Level_kits(App_context& context, App_message_bus& app_message_bus);
    ~Level_kits() noexcept;
    Level_kits(const Level_kits&) = delete;
    auto operator=(const Level_kits&) -> Level_kits& = delete;

    [[nodiscard]] auto get_library        () const -> const Kit_shape_library&;
    [[nodiscard]] auto get_lengths        (const Scene_root& scene_root) const -> Kit_lengths;
    [[nodiscard]] auto make_default_values(const Scene_root& scene_root, const Kit_shape_definition& shape) const -> Kit_parameter_values;

    // Kit_placement_settings of the editor settings level_kit and surface_snap.
    [[nodiscard]] auto get_placement_settings() const -> Kit_placement_settings;

    // The shared surface pair (Surface_snap_config, D4.6): every UI site reads and writes it here.
    [[nodiscard]] auto get_surface_settings() const -> Surface_options;
    void set_surface_snap  (Surface_snap snap);
    void set_surface_orient(Surface_orient orient);

    // D4.4: the origin at the point (module-rounded), upright, turned by yaw_steps rotation steps
    // about the up axis, then the surface options; no allocation, raytrace scene committed here.
    [[nodiscard]] auto make_placement_pose(
        Scene_root&                   scene_root,
        const erhe::math::Aabb&       local_bounds,
        const glm::vec3&              point,
        int                           yaw_steps,
        const Kit_placement_settings& settings
    ) const -> Kit_placement_pose;

    // A material of the palette material's color, outside every content library; null for a name not in the palette.
    [[nodiscard]] auto get_preview_material(std::string_view palette_name) -> std::shared_ptr<erhe::primitive::Material>;

    // The default palette material names of the shape's slots, in slot order.
    void get_default_slot_materials(const Kit_shape_definition& shape, std::vector<std::string>& out_names) const;

    // The cached brush of the values, made on first use; null when its geometry fails.
    [[nodiscard]] auto get_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // The cached brushes still alive: held by a piece or by an operation of the undo history.
    [[nodiscard]] auto get_cached_brush_count() const -> std::size_t;

    // Validates every placement, then places all of them as one Compound_operation (one undo entry).
    [[nodiscard]] auto place(Scene_root& scene_root, std::span<const Kit_placement> placements, Kit_snap snap) -> Kit_place_result;

    // D3.8: a new group node at the base center of the nodes' combined world bounds, under their common parent
    // (else the scene root), takes the top-level nodes of the set as children, world transforms kept; one undo entry.
    [[nodiscard]] auto group(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> nodes, std::string_view name) -> Kit_group_result;

    // D3.8: the group's children go to its parent, world transforms kept, and the group node is removed; one undo entry.
    [[nodiscard]] auto ungroup(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& group) -> Kit_ungroup_result;

    // D3.8: a copy of the node's subtree beside it, every copied piece a full piece; one undo entry.
    [[nodiscard]] auto duplicate(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_group_copy& copy) -> Kit_group_result;

    // D3.8: reflects the group's content across the plane through its origin normal to its local axis; one undo entry.
    [[nodiscard]] auto mirror(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& group, Mirror_axis axis) -> Kit_mirror_result;

    // The world bounds of the meshes of the node's subtree; without a mesh, of its nodes' origins.
    [[nodiscard]] static auto get_subtree_bounds(const erhe::scene::Node& node) -> erhe::math::Aabb;

    // The brush of the values when the cache holds it alive; never makes one.
    [[nodiscard]] auto find_cached_brush(const Kit_parameter_values& values) const -> std::shared_ptr<Brush>;

    // Reads a piece record into values of its shape; returns the problem (unknown shape, unreadable or rejected values), empty when none.
    [[nodiscard]] auto read_piece_values(const Kit_piece_data& record, Kit_parameter_values& out_values) const -> std::string;

    [[nodiscard]] auto check_piece(const erhe::scene::Node& node) const -> Kit_piece_check;

    // D3.5: after a scene has loaded, gives each piece the mesh and collision shape of its kit brush.
    auto refresh_scene(Scene_root& scene_root) -> Kit_refresh_report;

    // D7.3 on placed meshes (excluded from each other's hits); with move, the meshes that found a
    // surface move as one undo entry. Returns the moved count.
    auto snap_to_surface(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
        const Surface_options&                              options,
        Kit_snap_apply                                      apply,
        std::vector<Kit_snap_result>&                       out_results
    ) -> std::size_t;

    // D3.6: shows the values' mesh on the piece without an operation; the first call of a drag
    // remembers the piece's state. Returns the refusal, empty when shown.
    [[nodiscard]] auto preview_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> std::string;

    // Gives the previewed piece its state from before the drag back; nothing when no preview is live.
    void cancel_parameter_preview();

    [[nodiscard]] auto is_previewing(const erhe::scene::Node& node) const -> bool;

    // D3.3: gives the piece the mesh and collision shape of the values as one undo entry; with a live
    // preview of the piece, from the state before the drag (no entry when the values end where they began).
    [[nodiscard]] auto set_parameters(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values) -> Kit_set_parameters_result;

    // D3.4: gives one slot of the piece the material as one undo entry, a palette material added to the scene's
    // content library in the same entry; no entry when the slot has it already.
    [[nodiscard]] auto set_slot_material(
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& node,
        std::size_t                               slot,
        const Kit_slot_material&                  material
    ) -> Kit_set_material_result;

    [[nodiscard]] static auto get_mesh_key   (const Kit_parameter_values& values) -> std::string;
    [[nodiscard]] static auto snap_to_module (const glm::vec3& position, float module) -> glm::vec3;

    // D3.9: a kit layout node with the children its values make, one undo entry.
    [[nodiscard]] auto create_layout(Scene_root& scene_root, const Kit_layout_create& create) -> Kit_layout_result;

    // D3.9: the piece's removal, the node's insert at its place and the generated children, one undo entry.
    [[nodiscard]] auto make_layout_from_piece(Scene_root& scene_root, const Kit_layout_from_piece& from_piece) -> Kit_layout_result;

    // The layout item of a placed piece: its shape, record values (seed mask included) and its primitives' material names.
    [[nodiscard]] auto read_piece_layout_item(const erhe::scene::Mesh& piece, Kit_layout_item& out_item) const -> std::string;

    // D3.9: new values; a locked node regenerates its children in the same undo entry. The lock is not changed here.
    [[nodiscard]] auto set_layout(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_layout_data& data) -> Kit_layout_result;

    // D3.9: unlocking writes the value alone; locking replaces every child with the generated ones. One undo entry each.
    [[nodiscard]] auto set_layout_lock(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, Kit_layout_lock lock) -> Kit_layout_result;

    // D3.9: a locked node's children made equal to its values; no entry when they are.
    [[nodiscard]] auto refresh_layout(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node) -> Kit_layout_result;

    // D3.9: the regeneration of each locked kit layout node at or below the moved nodes whose surface settings
    // are on, appended to the move's operations (the nodes stand at their moved transforms).
    void append_layout_move_operations(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    );

    // D3.9: a Kit_layout value written outside these entry points (set_item_property, a Properties row), already
    // applied: the generation a locked node's values now need, or the reason they cannot stand. Nothing otherwise.
    [[nodiscard]] auto append_layout_value_follow_ups(
        erhe::Item_base&                           item,
        const erhe::property::Dependency_property& property,
        std::vector<std::shared_ptr<Operation>>&   out_operations
    ) -> std::string;

    // The refusal a write of the value would meet in append_layout_value_follow_ups(), checked without a change.
    [[nodiscard]] auto check_layout_value_write(
        erhe::Item_base&                           item,
        const erhe::property::Dependency_property& property,
        const erhe::property::Property_value&      value
    ) -> std::string;

    // An item's values (its seed mask among them) and slot materials as a placement reads them; returns the problem.
    [[nodiscard]] auto read_layout_item(Scene_root& scene_root, const Kit_layout_item& item, Kit_placement& out_placement) const -> std::string;

    // The item's piece bounds in its own frame; invalid when the item does not read or makes no geometry.
    [[nodiscard]] auto get_layout_item_bounds(Scene_root& scene_root, const Kit_layout_item& item) -> erhe::math::Aabb;

private:
    [[nodiscard]] auto make_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // place() without the refusals and without executing: the operations in order (materials, then pieces).
    [[nodiscard]] auto prepare_placements(
        Scene_root&                              scene_root,
        std::span<const Kit_placement>           placements,
        Kit_snap                                 snap,
        Kit_place_result&                        result,
        std::vector<std::shared_ptr<Operation>>& out_operations
    ) -> bool;

    // D3.9: the operations that make the node's children what the values state, from the node's world transform;
    // the excluded meshes are no surface for the elements, nor are the node's current children.
    [[nodiscard]] auto build_layout_generation(
        Scene_root&                                 scene_root,
        const std::shared_ptr<erhe::scene::Node>&   node,
        const Kit_layout_data&                      data,
        const glm::mat4&                            world_from_layout,
        Kit_layout_reuse                            reuse,
        std::span<const erhe::scene::Mesh* const>   excluded,
        std::vector<std::shared_ptr<Operation>>&    out_operations,
        Kit_layout_result&                          result
    ) -> std::string;

    // create_layout() with the node inserted at index_in_parent (none: last) after the given leading operations.
    [[nodiscard]] auto create_layout_after(
        Scene_root&                              scene_root,
        const Kit_layout_create&                 create,
        std::optional<std::size_t>               index_in_parent,
        std::span<const erhe::scene::Mesh* const> excluded,
        std::vector<std::shared_ptr<Operation>>& leading_operations
    ) -> Kit_layout_result;
    void on_selection    ();
    void on_items_removed(const Removed_items& removed);
    void on_close_scene  (const erhe::Item_host* closing_host);

    App_context&                                m_context;
    Kit_shape_library                           m_library;
    // Weak: pieces (Brush_placement.brush) and undo entries hold the brushes (doc/editor/level_tools.md).
    std::map<std::string, std::weak_ptr<Brush>> m_brushes;
    // By palette material name.
    std::map<std::string, std::shared_ptr<erhe::primitive::Material>> m_preview_materials;

    // The live parameter drag (D3.6): the piece, its state from before the drag and the brush shown.
    class Parameter_preview
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        Kit_piece_mesh_state               before;
        std::shared_ptr<Brush>             brush;
    };
    std::optional<Parameter_preview> m_parameter_preview;

    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
};

} // namespace editor
