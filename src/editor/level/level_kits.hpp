#pragma once

#include "level/kit_connector.hpp"
#include "level/kit_connector_indicators.hpp"
#include "level/kit_layout_indicators.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_piece_light.hpp"
#include "level/kit_piece_operation.hpp"
#include "level/kit_replace.hpp"
#include "level/level_group.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_queries.hpp"
#include "level/level_repeat.hpp"

#include "app_message.hpp"
#include "config/generated/connector_snap_axis_mode.hpp"
#include "config/generated/connector_snap_orientation.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/trs_transform.hpp"

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
namespace erhe::geometry { class Geometry; }
namespace erhe::property { class Dependency_property; }
namespace erhe::primitive { class Material; }
namespace erhe::math     { class Curve_path_table; }
namespace erhe::scene    { class Curve; class Mesh; class Xformable; using Node = Xformable; }

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

// A connector join (R5.3, D3.12): the piece's connector meets a connector of the target piece.
class Kit_connect
{
public:
    std::string                        connector;        // of the new or moved piece
    std::shared_ptr<erhe::scene::Node> target;           // a kit piece of the scene; not edited
    std::string                        target_connector;
    int                                twist_steps{0};   // quarter turns about the target connector's outward axis
    float                              gap        {0.0f}; // along the target connector's outward axis
};

// Which of a piece's connectors a query returns (U33).
enum class Kit_connector_set : unsigned int
{
    all = 0, // every connector of the shape
    enabled  // the connectors the piece's switches enable
};

// An edit of a piece's connector switches (U33); what is not given stays.
class Kit_connector_switch_edit
{
public:
    class Connector
    {
    public:
        std::string         name;
        Kit_connector_state state{Kit_connector_state::enabled};
    };
    std::optional<Kit_connector_state> piece;      // the switch of all of the piece's connectors
    std::vector<Connector>             connectors; // a name of the piece's shape each
};

class Kit_connector_switch_result
{
public:
    Kit_connector_switches switches; // after the edit
    bool                   changed{false}; // an undo entry was recorded
    std::string            error;          // empty on success; with an error nothing changed
};

class Kit_connect_result
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    glm::vec3                          position{0.0f}; // the piece's new world pose
    glm::quat                          rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool                               changed{false}; // an undo entry was recorded
    std::string                        error;          // empty on success; with an error nothing changed
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
    glm::vec3                                  scale{1.0f};  // the node's own scale (a segments layout's piece); 1 with a connect
    std::string                                name;         // empty: the shape's display name
    std::vector<Kit_slot_material>             materials;    // by slot index; a slot without an entry takes its default
    Surface_options                            surface;      // applied after the module snap (D7.3)
    std::optional<std::size_t>                 index_in_parent; // none: after the parent's children
    std::optional<Kit_connect>                 connect;      // the pose is the join (D3.12): position, rotation, module snap and surface unused
};

// What a kit layout generation starts from (D3.9): the items read, their brushes and bounds, the run and its poses in
// the node's frame, and with per-element seeds each element's values.
class Kit_layout_preparation
{
public:
    std::vector<Kit_placement>          items;
    std::vector<erhe::math::Aabb>       bounds; // get_kit_geometry_bounds(): no brush is made
    Kit_layout_item_sizes               sizes;
    Kit_layout_run                      run;
    std::vector<Repeat_pose>            poses;
    std::vector<Kit_parameter_values>   element_values; // empty unless the layout's seeds are per element

    // Releases the held items (their materials) and values; capacity kept.
    void clear();
};

// D3.12: the local transform of a joined piece, keeping its stored scale when the matrix allows, else decomposed.
[[nodiscard]] auto make_joined_local_transform(const glm::mat4& parent_from_node, const glm::vec3& scale) -> erhe::scene::Trs_transform;

// Whether the layout's world matrix keeps scale 1, as a generation requires.
[[nodiscard]] auto is_layout_scale_kept(const glm::mat4& world_from_layout) -> bool;

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

// Where a piece placed at a point goes after the snaps: the transform the kit mode preview shows and a click places.
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
    std::size_t light_change_count{0}; // light nodes moved to, added at or removed from their frames (D3.15)
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

// One piece to give another shape (D3.10).
class Kit_replacement
{
public:
    std::shared_ptr<erhe::scene::Node>  node;
    std::optional<Kit_parameter_values> values;    // none: the values carried over (carry_parameter_values())
    std::vector<Kit_slot_material>      materials; // per slot of the new shape; an empty entry: the same-named slot's material, else the default
};

class Kit_replaced_piece
{
public:
    std::shared_ptr<erhe::scene::Mesh>                      mesh;
    std::string                                             old_shape;
    bool                                                    replaced{false}; // false: the piece has the shape already
    Kit_parameter_values                                    values;
    std::vector<Kit_parameter_carry>                        parameters;      // per parameter of the new shape
    std::vector<std::optional<std::size_t>>                 slot_matches;    // per new slot: the old slot whose material it kept, else none
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;       // per new slot
    Surface_placement                                       surface;         // the snap of a replaced piece when the replace snapped
    bool                                                    snapped{false};  // the snap moved the piece
};

// A node replace_shape() left out (U24).
class Kit_replace_skipped
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    Kit_replace_skip                   skip{Kit_replace_skip::not_a_kit_piece};
    std::string                        message;
};

class Kit_replace_result
{
public:
    std::vector<Kit_replaced_piece>  pieces;         // per replaced or unchanged node, in the order given
    std::vector<Kit_replace_skipped> skipped;        // per skipped node, in the order given
    std::size_t                      replaced_count{0};
    std::size_t                      snapped_count {0};
    std::optional<std::size_t>       failed_index;   // the replacement the error names
    std::string                      error;          // empty on success; with an error nothing changed
};

// Which children a kit layout regeneration keeps (D3.9): the ones that already match, or none (a lock, R5.12.5).
enum class Kit_layout_reuse : unsigned int {
    keep_matching = 0,
    replace_all
};

// A new kit layout node (D3.9): its values, parent (none: the scene root) and world transform; a path layout is a
// curve node (R5.12.8) whose path is given, else straight along its x axis.
class Kit_layout_create
{
public:
    Kit_layout_data                       data;
    std::shared_ptr<erhe::Hierarchy>      parent;
    glm::vec3                             position{0.0f};
    glm::quat                             rotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::string                           name;     // empty: "Kit Layout"
    std::optional<erhe::math::Curve_path> path;     // a path layout's curve in the node's frame
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
    float                              surface_max_distance{c_default_surface_max_distance};
    Kit_layout_seeds                   seeds         {Kit_layout_seeds::item};
    int                                seed          {0};
    Kit_layout_lock                    lock          {Kit_layout_lock::locked};
    Kit_layout_connector_hiding        connector_hiding;
    std::string                        name;          // empty: the piece's name followed by " Layout"
    std::optional<erhe::math::Curve_path> path;          // a path layout's curve; none: straight along the piece's x axis
};

// What Wrap with Path (U45) wraps: the pieces in hierarchy order, their distinct items and the largest item x extent.
class Kit_wrap_plan
{
public:
    std::vector<std::shared_ptr<erhe::scene::Mesh>> pieces;
    std::vector<Kit_layout_item>                    items;
    float                                           extent{0.0f};
};

// Whether a new kit layout node's line step and grid spacings are as given or its item's extent (a contiguous run).
enum class Kit_layout_spacing : unsigned int {
    as_given = 0,
    from_item
};

// Whether a Brush tool click in kit mode places one piece or a kit layout node of the initial values (R5.12.9).
enum class Kit_place_as : unsigned int {
    piece = 0,
    layout
};

// The initial values of a kit layout node the author adds (R5.12.9): the Level Kit window's Layout rows, saved in the
// editor setting level_kit. The items come from what is added (a piece, the shape the Brush tool places).
class Kit_layout_initial_values
{
public:
    Kit_layout_data    data;
    Kit_layout_spacing spacing {Kit_layout_spacing::from_item};
    Kit_place_as       place_as{Kit_place_as::piece};
};

class Kit_layout_result
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    Kit_layout_run                     run;               // the run the values make
    Stairs_report                      stairs;            // a stairs layout's fit; empty for another kind
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

    // R5.12.9: the initial values of a new kit layout node (Level_kit_config); setting them changes no node.
    [[nodiscard]] auto get_layout_initial_values() const -> Kit_layout_initial_values;
    void set_layout_initial_values(const Kit_layout_initial_values& values);

    // The shared surface pair (Surface_snap_config, D4.6): every UI site reads and writes it here.
    [[nodiscard]] auto get_surface_settings() const -> Surface_options;
    void set_surface_snap  (Surface_snap snap);
    void set_surface_max_distance(float distance); // the distance must pass check_surface_max_distance()
    // Reports and replaces a surface max distance of the loaded editor settings that check_surface_max_distance() refuses.
    void validate_surface_settings();
    void set_surface_orient(Surface_orient orient);
    // D3.13: the connector snap distance of Transform tool node drags in meters, 0 = off; set clamps a negative or non-finite value to 0.
    [[nodiscard]] auto get_connector_snap_distance() const -> float;
    void set_connector_snap_distance(float distance);
    // U27: whether an axis or plane drag applies the full connector snap offset or only its part along the handle.
    [[nodiscard]] auto get_connector_snap_axis_mode() const -> Connector_snap_axis_mode;
    void set_connector_snap_axis_mode(Connector_snap_axis_mode mode);
    // U44: whether a translate drag's connector snap also turns the moved nodes so the connector frames face each other.
    [[nodiscard]] auto get_connector_snap_orientation() const -> Connector_snap_orientation;
    void set_connector_snap_orientation(Connector_snap_orientation orientation);

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

    // The connector frames the views draw, with their per-piece cache (doc/editor/level_tools.md "Connector indicators").
    [[nodiscard]] auto get_connector_indicators() -> Kit_connector_indicators&;
    // The kit layout outlines the views draw, with their per-node cache (doc/editor/level_tools.md "Kit layout drawing").
    [[nodiscard]] auto get_layout_indicators() -> Kit_layout_indicators&;
    // The scene's Level lengths were written (the Level Kit window, MCP set_scene_settings).
    void on_level_lengths_changed();
    // A Kit_piece record value or connector switch of the node was written (App_context::on_item_property_changed()).
    void on_kit_piece_record_changed(const erhe::scene::Node& node);

    // D3.12: the connectors of the piece's record values in its local space, all or the enabled ones (U33) in the
    // shape's order (parses the record: once per drag, not per step).
    [[nodiscard]] auto get_local_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors, Kit_connector_set set) const -> std::string;

    // The piece's connector switches with the connectors its locked path or segments layout hides (U39): the one query
    // every connector consumer reads. local_connectors: the piece's connectors (all, the shape's order) when at hand.
    [[nodiscard]] auto get_connector_switches(const erhe::scene::Node& node) const -> Kit_connector_switches;
    [[nodiscard]] auto get_connector_switches(const erhe::scene::Node& node, std::span<const Kit_connector> local_connectors) const -> Kit_connector_switches;

    // U39: the node as a child of a locked path or segments layout (a Curve node); nullopt for any other node.
    [[nodiscard]] auto get_layout_child(const erhe::scene::Node& node) const -> std::optional<Kit_layout_child>;

    // D1.12: the piece's connectors under its world matrix; returns the refusal (not a kit piece, unreadable record, a zero scale axis), empty on success.
    [[nodiscard]] auto get_world_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors, Kit_connector_set set) const -> std::string;

    // D3.12: the rigid world motion putting the connector of a piece of the values and switches, under world_from_node,
    // on the target's; returns the refusal (a disabled connector of either named), empty on success. With
    // world_from_node identity the motion is the new piece's world pose.
    [[nodiscard]] auto resolve_connect(
        const Scene_root&             scene_root,
        const Kit_parameter_values&   values,
        const Kit_connector_switches& switches,
        const Kit_connect&            connect,
        const glm::mat4&              world_from_node,
        Kit_join_pose&                out_motion
    ) const -> std::string;

    // U33: why a raw write of a connector switch value (set_item_property, a raw row, Paste Properties) with the piece's
    // record as given is refused: a value of a shape without connectors, a list naming a connector the shape has not or
    // not canonical; empty when accepted or when the property is no connector switch.
    [[nodiscard]] auto check_connector_switch_write(
        const Kit_piece_data&                      record,
        const erhe::property::Dependency_property& property,
        const erhe::property::Property_value&      value
    ) const -> std::string;
    // As above with the node's own record; a node that is not a kit piece is refused.
    [[nodiscard]] auto check_connector_switch_write(
        const erhe::Item_base&                     item,
        const erhe::property::Dependency_property& property,
        const erhe::property::Property_value&      value
    ) const -> std::string;

    // U33: writes the piece's connector switches as one undo entry (none when nothing changes); a name the shape does not
    // have, a shape without connectors, a node that is not a kit piece and a child of a locked kit layout node are refused.
    [[nodiscard]] auto set_connector_switches(
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& node,
        const Kit_connector_switch_edit&          edit
    ) -> Kit_connector_switch_result;

    // D3.12: moves the piece, its children riding with it, so its connector meets the target's; one undo entry.
    [[nodiscard]] auto connect_piece(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_connect& connect) -> Kit_connect_result;

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

    // D3.10: the values a piece carries over to the shape at the scene's lengths; returns the refusal, empty on success.
    [[nodiscard]] auto make_replace_values(
        const Scene_root&                 scene_root,
        const erhe::scene::Node&          node,
        const Kit_shape_definition&       shape,
        Kit_parameter_values&             out_values,
        std::vector<Kit_parameter_carry>& out_carry
    ) const -> std::string;

    // U24: why replace_shape() leaves the node (of the scene) out; none when it can be replaced.
    [[nodiscard]] auto find_replace_skip(const std::shared_ptr<erhe::scene::Node>& node) const -> std::optional<Kit_replace_skipped>;

    // D3.10, U24: gives the pieces the shape and, with a surface option, snaps them, as one undo entry (none when
    // nothing changes); nodes that cannot be replaced are skipped, a refused argument changes nothing.
    [[nodiscard]] auto replace_shape(
        Scene_root&                       scene_root,
        const Kit_shape_definition&       shape,
        std::span<const Kit_replacement>  replacements,
        const Surface_options&            surface
    ) -> Kit_replace_result;

    [[nodiscard]] static auto get_mesh_key   (const Kit_parameter_values& values) -> std::string;
    [[nodiscard]] static auto snap_to_module (const glm::vec3& position, float module) -> glm::vec3;

    // D3.9: a kit layout node with the children its values make, one undo entry.
    [[nodiscard]] auto create_layout(Scene_root& scene_root, const Kit_layout_create& create) -> Kit_layout_result;

    // D3.9: the piece's removal, the node's insert at its place and the generated children, one undo entry.
    [[nodiscard]] auto make_layout_from_piece(Scene_root& scene_root, const Kit_layout_from_piece& from_piece) -> Kit_layout_result;

    // R5.12.9: make_layout_from_piece() with the initial values, the spacings from the piece's extent when they say so.
    [[nodiscard]] auto make_layout_from_piece_initial(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece) -> Kit_layout_result;

    // U47: why a new kit layout node of the kind cannot go under the parent (a kit layout node, a scaled parent, a tilted
    // parent for stairs); null when it can. Static text, no allocation.
    [[nodiscard]] static auto find_layout_parent_refusal(const erhe::Hierarchy& parent, Repeat_pattern_kind kind) -> const char*;

    // U47: a new kit layout node of the kind with the initial values (the Level Kit window's, the kind's own values),
    // under the parent (none: the scene root) at the world pose, the item given or basic.block at its defaults (stairs:
    // none); one undo entry; selects the node.
    [[nodiscard]] auto add_layout_of_kind(
        Scene_root&                             scene_root,
        Repeat_pattern_kind                     kind,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::vec3&                        position,
        const glm::quat&                        rotation,
        const std::optional<Kit_layout_item>&   item
    ) -> Kit_layout_result;

    // U47: the initial values on a plain curve node (no layout values, no children) with the curve kind; the item given or
    // basic.block at its defaults (stairs: none); one undo entry; selects the node.
    [[nodiscard]] auto make_initial_layout_on_curve(
        Scene_root&                                scene_root,
        const std::shared_ptr<erhe::scene::Curve>& curve,
        Repeat_pattern_kind                        kind,
        const std::optional<Kit_layout_item>&      item
    ) -> Kit_layout_result;

    // U47: a piece of the shape at its defaults under the parent (none: the scene root) at the world pose (the Create
    // menu); one undo entry; selects the piece.
    [[nodiscard]] auto add_piece_of_shape(
        Scene_root&                             scene_root,
        const Kit_shape_definition&             shape,
        const std::shared_ptr<erhe::Hierarchy>& parent,
        const glm::vec3&                        position,
        const glm::quat&                        rotation
    ) -> Kit_place_result;

    // U45 (Wrap with Path): why wrap_with_path() refuses the nodes; none when it takes them. Reads the items' brushes.
    [[nodiscard]] auto find_wrap_with_path_refusal(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> std::optional<std::string>;

    // U45: one locked path layout in the place of the first piece in hierarchy order, the pieces its distinct items, the
    // pieces removed; one undo entry; selects the node.
    [[nodiscard]] auto wrap_with_path(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> Kit_layout_result;

    // R5.12.9: a new kit layout node of the initial values with the one item at a world pose (the Brush tool's
    // Place As Layout click), one undo entry.
    [[nodiscard]] auto add_layout_initial(Scene_root& scene_root, const Kit_layout_item& item, const glm::vec3& position, const glm::quat& rotation) -> Kit_layout_result;

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

    // D3.9: the items' translation, rotation or scale written outside the Transform tool (a Properties row, a paste,
    // set_item_property), already applied: one move hook (append_layout_move_operations()) per scene for the item nodes.
    void append_layout_transform_follow_ups(
        std::span<const std::shared_ptr<erhe::Item_base>> items,
        const erhe::property::Dependency_property&        property,
        std::vector<std::shared_ptr<Operation>>&          out_operations
    );

    // D3.9: the regeneration of the curve when it is a locked path layout and its path has just changed; appended to the
    // curve edit's operations (Curve_set_operation). A layout the new curve refuses keeps its children (a warning).
    void append_curve_follow_ups(const erhe::scene::Curve& curve, std::vector<std::shared_ptr<Operation>>& out_operations);

    // The live layout preview of a drag (doc/editor/level_tools.md "Live layout preview"): begin with the layouts,
    // step at each change of the drag, end before the gesture's commit, which then records one entry.
    void begin_layout_preview(const std::shared_ptr<erhe::scene::Node>& layout);
    void begin_layout_preview(std::span<const std::shared_ptr<erhe::scene::Node>> layouts);

    // Begins a preview of every locked layout at or below the moved nodes whose surface settings are on (the move hook's set).
    void begin_layout_move_preview(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes);

    // The move preview of a Properties transform row drag (doc/editor/level_tools.md "Live layout preview").
    void begin_layout_transform_preview(
        std::span<const std::shared_ptr<erhe::Item_base>> items,
        const erhe::property::Dependency_property&        property
    );
    void step_layout_transform_preview();
    void end_layout_transform_preview();

    // Lays every previewed layout out again from its live values, transform and curve; returns the first refusal.
    auto step_layout_preview() -> std::string;

    // Gives the layout the values without an entry (beginning its preview) and lays it out; refused values keep the last
    // valid step and are not written. Returns the refusal; the result holds the run of shown values.
    [[nodiscard]] auto preview_layout_values(const std::shared_ptr<erhe::scene::Node>& layout, const Kit_layout_data& data, Kit_layout_result& out_result) -> std::string;

    // Gives every previewed layout its children and values from before the drag back; nothing when no preview is live.
    void end_layout_preview();

    [[nodiscard]] auto is_layout_previewing(const erhe::scene::Node& node) const -> bool;

    // R5.12.8: a plain curve node (no children, no layout values) becomes a path layout with the values and the children
    // a locked layout makes; one undo entry.
    [[nodiscard]] auto make_layout_on_curve(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Curve>& curve, const Kit_layout_data& data) -> Kit_layout_result;

    // D3.9: the items, run and poses a generation of the values on the node under world_from_layout starts from,
    // refused as a generation refuses them and in its order, except for a brush that makes no geometry; makes no brush.
    // Returns the refusal, empty when the values stand.
    [[nodiscard]] auto prepare_layout_generation(
        Scene_root&              scene_root,
        const erhe::scene::Node& node,
        const Kit_layout_data&   data,
        const glm::mat4&         world_from_layout,
        Kit_layout_preparation&  out_preparation
    ) -> std::string;

    // A Material was added to, removed from or renamed in a scene's content library: layout items name materials.
    void on_scene_materials_changed();

    // The run the values make on the node (a path: along its own curve); the run's pattern refers to no table and no
    // items. Returns the refusal (the kind not of the node's type, ...), empty when resolved.
    [[nodiscard]] auto resolve_layout_run(
        const erhe::scene::Node&     layout,
        const Kit_layout_data&       data,
        const Kit_layout_item_sizes& items,
        Kit_layout_run&              out_run
    ) const -> std::string;

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

    // Each item's weight and footprint and the largest extent along x, from the items' brushes; cleared and filled.
    void get_layout_item_sizes(Scene_root& scene_root, std::span<const Kit_layout_item> items, Kit_layout_item_sizes& out_sizes);

private:
    [[nodiscard]] auto make_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // U47: the initial values with the kind and the item (basic.block at its defaults when none; stairs: no item).
    [[nodiscard]] auto make_initial_layout_data(Scene_root& scene_root, Repeat_pattern_kind kind, const std::optional<Kit_layout_item>& item) -> Kit_layout_data;

    // U45: the pieces in hierarchy order, their distinct items and the largest item x extent; returns the refusal.
    [[nodiscard]] auto prepare_wrap_with_path(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> nodes, Kit_wrap_plan& out_plan) -> std::string;

    // place() without the refusals and without executing: the operations in order (materials, then pieces).
    [[nodiscard]] auto prepare_placements(
        Scene_root&                              scene_root,
        std::span<const Kit_placement>           placements,
        Kit_snap                                 snap,
        Kit_place_result&                        result,
        std::vector<std::shared_ptr<Operation>>& out_operations
    ) -> bool;

    // D3.9: the operations that make the node's children what the values state, from the node's world transform (a
    // path: along its own curve); the excluded meshes are no surface for the elements, nor are the node's current
    // children.
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

    // R5.12.13: the generation of a stairs layout: the one generated child the curve and the values make.
    [[nodiscard]] auto build_stairs_generation(
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& node,
        const Kit_layout_data&                    data,
        const glm::mat4&                          world_from_layout,
        Kit_layout_reuse                          reuse,
        std::vector<std::shared_ptr<Operation>>&  out_operations,
        Kit_layout_result&                        result
    ) -> std::string;

    // The insert of a stairs layout's child at the layout's transform: a static triangle mesh body, the material by
    // name (empty: the stone slot's default), the source text on the node. Appends; returns the refusal.
    [[nodiscard]] auto make_stairs_insert(
        Scene_root&                                      scene_root,
        const std::shared_ptr<erhe::scene::Node>&        layout,
        const glm::mat4&                                 world_from_layout,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const std::string&                               material_name,
        const std::string&                               source,
        std::vector<std::shared_ptr<Operation>>&         out_operations
    ) -> std::string;

    // The refusal of a stairs material name: neither a scene material nor a palette material.
    [[nodiscard]] auto check_stairs_material(const Scene_root& scene_root, const std::string& material_name) const -> std::string;

    // The regeneration of each locked layout of the list, warned about and skipped where its values refuse.
    void append_layout_regenerations(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> layouts,
        const char*                                         reason,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    );

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
    // Gives the kit lights of the previewed piece their transforms from before the drag back (D3.15).
    void restore_preview_lights(const erhe::scene::Mesh& mesh);

    // The live layout preview: per layout the operations that laid it out (executed in order) and its values from before
    // the drag once a step wrote values.
    class Layout_preview_entry
    {
    public:
        std::shared_ptr<erhe::scene::Node>                      node;
        bool                                                    selected_at_begin{false};
        std::vector<std::optional<erhe::property::Local_state>> values_before;
        std::vector<std::shared_ptr<Operation>>                 operations;
        std::vector<std::shared_ptr<Operation>>                 generation;
        Kit_layout_result                                       result;
        std::string                                             refusal;
        bool                                                    built{false};
    };
    // One step of every entry; the values, when given, are the given node's instead of its own.
    auto step_layout_preview_entries(const erhe::scene::Node* values_node, const Kit_layout_data* values) -> std::string;
    void collect_moved_layouts(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::vector<std::shared_ptr<erhe::scene::Node>>&    out_layouts
    ) const;
    void end_layout_preview_of(const erhe::scene::Node& node);
    void queue_end_layout_preview();
    // A previewed node left the selection it had at the start, or its scene.
    [[nodiscard]] auto is_layout_preview_lost() const -> bool;
    // The preview's operations restore the selection they were made under; the preview keeps the current one.
    void hold_selection();
    void restore_held_selection();
    std::vector<std::shared_ptr<erhe::Item_base>>   m_held_selection;
    std::shared_ptr<erhe::Item_base>                m_held_active_item;
    bool                                            m_layout_preview_end_queued{false};
    bool                                            m_layout_transform_preview{false}; // the live preview is begin_layout_transform_preview()'s
    std::vector<Layout_preview_entry>               m_layout_preview;
    std::vector<const erhe::scene::Mesh*>           m_layout_preview_excluded;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_layout_preview_layouts;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_layout_preview_collected;
    Kit_layout_preparation                          m_generation_preparation; // build_layout_generation() scratch

    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Curve_changed_message> m_curve_changed_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Kit_layout_changed_message>    m_kit_layout_changed_subscription;
    Kit_connector_indicators                                m_connector_indicators;
    Kit_piece_light_sync                                    m_light_sync; // D3.15, scratch kept across calls
    Kit_layout_indicators                                   m_layout_indicators;
};

} // namespace editor
