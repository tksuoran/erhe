#pragma once

#include "level/kit_connection_flow.hpp"
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
#include "config/generated/surface_drag_direction.hpp"
#include "config/generated/surface_snap_rest.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_math/curve_path.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_property/expression.hpp"
#include "erhe_property/property_value.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform_op.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spdlog::level  { enum level_enum : int; }
namespace erhe           { class Hierarchy; class Item_base; class Item_host; }
namespace erhe::geometry { class Geometry; }
namespace erhe::property { class Dependency_property; }
namespace erhe::primitive { class Material; }
namespace erhe::math     { class Curve_path_table; }
namespace erhe::scene    { class Curve; class Mesh; class Xformable; using Node = Xformable; }
namespace tf             { class Executor; }
namespace erhe::graphics { class Device; }

namespace editor {

class App_context;
class App_message_bus;
class Brush;
class Kit_connection_end;
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

// Whether a connector join also records an attachment the joined piece follows (R5.3, plan step 41).
enum class Kit_attach : unsigned int {
    none = 0,
    attach
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
    Kit_attach                         attach{Kit_attach::attach};
};

// Whether a piece's attachment is followed (doc/editor/level_tools.md "Attachments").
enum class Kit_attachment_state : unsigned int {
    none = 0, // no record of its own
    active,
    dormant   // kept, not followed: the reason says why
};

class Kit_attachment_status
{
public:
    Kit_attachment_state               state{Kit_attachment_state::none};
    Kit_attachment                     attachment;
    std::shared_ptr<erhe::scene::Node> target; // null when the target is not in the piece's scene
    std::string                        reason; // dormant: why
};

// Whether a piece's surface attachment holds (doc/editor/level_tools.md "Surface attachments").
enum class Kit_surface_attachment_state : unsigned int {
    none = 0,    // no record
    active,      // the record names the piece's parent, a mesh node
    off_surface, // active, but the last carry found no face of the support on the piece's line (S3.4)
    inert        // kept, not followed: the reason says why
};

[[nodiscard]] auto c_str(Kit_surface_attachment_state state) -> const char*;

// Active or off_surface: the piece is attached to its parent and follows it.
[[nodiscard]] auto is_surface_attached(Kit_surface_attachment_state state) -> bool;

class Kit_surface_attachment_status
{
public:
    Kit_surface_attachment_state       state{Kit_surface_attachment_state::none};
    Kit_surface_attachment             attachment;
    std::shared_ptr<erhe::scene::Node> target; // the support: the parent when active, else the scene's node of the uid or null
    std::string                        reason; // inert: why
};

// Why a join records no attachment (step 41).
enum class Kit_attach_refusal : unsigned int {
    none = 0,
    locked_layout // a child of a locked kit layout node on either side: the join is made without an attachment
};

class Kit_attach_check
{
public:
    Kit_attach_refusal refusal{Kit_attach_refusal::none};
    std::string        message;
};

class Kit_detach_result
{
public:
    bool        changed{false}; // an undo entry was recorded
    std::string error;          // empty on success; with an error nothing changed
};

using Kit_remove_connection_result = Kit_detach_result;


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
    std::string                        not_attached;   // with attach asked: why no attachment was recorded (the join was made)
};

// One piece to place (D3.2); positions and rotations are in the scene root's space.
class Kit_placement
{
public:
    const Kit_shape_definition*                shape{nullptr};
    std::optional<Kit_parameter_values>        values;       // none: the defaults at the scene's lengths
    std::shared_ptr<erhe::Hierarchy>           parent{};     // none: the scene root
    std::optional<std::size_t>                 parent_index{}; // an earlier placement of the same call
    glm::vec3                                  position{0.0f};
    glm::quat                                  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                                  scale{1.0f};  // the node's own scale (a segments layout's piece); 1 with a connect
    std::string                                name{};       // empty: the shape's display name
    std::vector<Kit_slot_material>             materials;    // by slot index; a slot without an entry takes its default
    Surface_options                            surface{};    // applied after the module snap (D7.3)
    std::optional<std::size_t>                 index_in_parent{}; // none: after the parent's children
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
    glm::vec3 surface_direction{c_default_surface_direction}; // the search direction the pose was found along
    bool      surface_found{false};
    const erhe::scene::Mesh* support_mesh{nullptr}; // Surface_placement::support_mesh; valid until the scene changes
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

// S5.2: an attached piece of a removed layout child, parked until the child's successor (element `element`) is inserted.
class Kit_parked_piece
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    std::size_t                        element{0};
};

// Whether the surface carry of a change writes the records (a release) or moves the pieces only (a preview step, S3.7).
enum class Kit_surface_carry_record : unsigned int {
    write = 0,
    skip
};

// S3.2 - S3.4: a piece's local transform carried onto its support's bounds and rested again on the support's faces, and the
// record that pose writes (none when the record stays as it is).
class Kit_surface_carry
{
public:
    erhe::scene::Trs_transform            local;
    std::optional<Kit_surface_attachment> record;
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
    std::shared_ptr<erhe::scene::Node>         support; // S2.1: the support the piece was inserted under; null when none
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

// K11.2 (C6.5): a connection of a replaced piece that Replace removed.
class Kit_dropped_connection
{
public:
    std::string                        connector;       // of the replaced piece
    std::shared_ptr<erhe::scene::Node> other;           // the other end; null when it is not in the scene
    std::string                        other_connector;
    std::string                        reason;
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
    std::vector<Kit_dropped_connection>                     dropped_connections; // the others are kept (K11.2)
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
    std::size_t                      dropped_connection_count{0}; // each dropped connection once
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
    glm::vec3                          surface_direction        {c_default_surface_direction};
    float                              surface_forward_distance {c_default_surface_forward_distance};
    float                              surface_backward_distance{c_default_surface_backward_distance};
    Surface_support                    surface_support          {Surface_support::five_lines};
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

// A new material of the palette material's color, owned by no content library.
[[nodiscard]] auto make_palette_material(const Kit_palette_material& palette) -> std::shared_ptr<erhe::primitive::Material>;

// The whole mesh of the values, every slot. Any thread: the generators read only the values and the library.
[[nodiscard]] auto make_kit_geometry(const Kit_parameter_values& values) -> std::shared_ptr<erhe::geometry::Geometry>;
// One geometry per material slot of the shape (D2.1); returns the problem, empty when every slot has facets. Any thread.
[[nodiscard]] auto make_kit_part_geometries(
    const Kit_parameter_values&                             values,
    std::vector<std::shared_ptr<erhe::geometry::Geometry>>& out_parts
) -> std::string;
// The normal style the shape's renderable meshes are built with.
[[nodiscard]] auto get_kit_normal_style(const Kit_shape_definition& shape) -> erhe::primitive::Normal_style;

class Kit_thumbnails;

// The level kit editor part (D2.2): the shape library, the kit brush cache and piece placement.
// C5.5 (K10.1): the Auto-connect setting's value.
enum class Connector_auto_connect : unsigned int { off, on };

// C3.8: the most rounds a connection flow runs (the Connection Step Limit setting's default).
inline constexpr std::size_t c_connection_step_limit = 64;

// The other end of a connection at a connector: the node and its connector.
class Kit_connection_end_at
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    std::string                        connector;
};

// K6: a floor kit layout node (a Curve carrying a floor layout), a connection end like a kit piece.
[[nodiscard]] auto is_floor_layout(const erhe::scene::Node& node) -> bool;
// K6.1, K13: a wall-floor connection is recorded on its wall and a rail connection on its rider: true when the node is
// the floor end of a wall-floor join or the host end of a rail join, whose record goes on the other end.
[[nodiscard]] auto is_host_end_of_join(const erhe::scene::Node& node, const Kit_connector& own, const erhe::scene::Node& other, const Kit_connector& other_connector) -> bool;
// The record of the same connection kept on its other end (the join's twist and gap read the same from both ends).
[[nodiscard]] auto make_reversed_attachment(const Kit_attachment& attachment) -> Kit_attachment;
// Its edge connectors in its local frame (get_floor_connectors()); the refusal when its outline or values do not read.
[[nodiscard]] auto get_floor_layout_connectors(const erhe::scene::Node& node, std::vector<Kit_connector>& out_connectors) -> std::string;

// The snap pair of a flow preview's run, by id and name: the state outlives the drag and holds no node.
class Kit_flow_preview_pair
{
public:
    std::uint64_t piece_id {0};
    std::string   piece;
    std::string   connector;
    std::uint64_t target_id{0};
    std::string   target;
    std::string   target_connector;
};

// K9.1 (C3.10): the flow preview of the current or last Transform drag (doc/editor/level_tools.md "Flow preview").
class Kit_flow_preview_state
{
public:
    bool                         active       {false};
    std::size_t                  steps        {0}; // preview steps of the drag
    std::size_t                  runs         {0}; // steps that ran the flow again
    std::size_t                  held_steps   {0}; // steps whose inputs were the shown run's: nothing ran
    std::size_t                  refused_steps{0}; // steps whose flow was refused: the last valid run kept
    std::string                  refusal{};        // of the last step; empty when it stood
    std::optional<Kit_flow_preview_pair> snap{};     // the snap pair of the run shown
};

class Level_kits
{
public:
    // The thumbnails build on background_executor; graphics_device tells whether it can (worker contexts).
    Level_kits(App_context& context, App_message_bus& app_message_bus, erhe::graphics::Device& graphics_device, tf::Executor& background_executor);
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
    // The Snap To Surface setting as stored; get_surface_settings() also snaps while Attach To Surface is on.
    [[nodiscard]] auto get_surface_snap() const -> Surface_snap;
    void set_surface_snap  (Surface_snap snap);
    void set_surface_max_distance(float distance); // the distance must pass check_surface_max_distance()
    // Reports and replaces a surface max distance of the loaded editor settings that check_surface_max_distance() refuses.
    void validate_surface_settings();
    void set_surface_orient(Surface_orient orient);
    // S2.7 Attach To Surface; get_surface_settings() carries it in Surface_options::attach.
    void set_surface_attach(Surface_attach attach);
    // U60: what Transform tool drags and Brush tool kit placement rest with orient off; get_surface_settings() leaves it leading_face.
    [[nodiscard]] auto get_surface_rest() const -> Surface_snap_rest;
    void set_surface_rest(Surface_snap_rest rest);
    // U51: the search direction of Transform tool drags: the explicit vector (stored as given, normalized on use; set must pass check_surface_direction()) or the camera ray.
    [[nodiscard]] auto get_surface_drag_direction_mode() const -> Surface_drag_direction;
    void set_surface_drag_direction_mode(Surface_drag_direction mode);
    [[nodiscard]] auto get_surface_drag_direction() const -> glm::vec3;
    void set_surface_drag_direction(const glm::vec3& direction);
    // D3.13: the connector snap distance of Transform tool node drags in meters, 0 = off; set clamps a negative or non-finite value to 0.
    [[nodiscard]] auto get_connector_snap_distance() const -> float;
    void set_connector_snap_distance(float distance);
    // U27: whether an axis or plane drag applies the full connector snap offset or only its part along the handle.
    [[nodiscard]] auto get_connector_snap_axis_mode() const -> Connector_snap_axis_mode;
    void set_connector_snap_axis_mode(Connector_snap_axis_mode mode);
    // U44: whether a translate drag's connector snap also turns the moved nodes so the connector frames face each other.
    [[nodiscard]] auto get_connector_snap_orientation() const -> Connector_snap_orientation;
    // C5.5 (L1): Auto-connect, on by default.
    [[nodiscard]] auto get_connector_auto_connect() const -> bool;
    void set_connector_auto_connect(Connector_auto_connect auto_connect);
    // C3.9 (L17, L21): the connector types values and poses flow over; all by default.
    [[nodiscard]] auto get_connector_flow_mask() const -> Kit_connector_types;
    void set_connector_flow_mask(Kit_connector_types mask);
    // C3.8 (L13): the most rounds of a change's flow and auto-connection; 64 by default.
    [[nodiscard]] auto get_connection_step_limit() const -> std::size_t;
    void set_connection_step_limit(std::size_t limit);
    // C6.2: the overrides every flow reads until cleared (one MCP call); nullopt clears them.
    void set_flow_overrides(const std::optional<Kit_flow_overrides>& overrides);
    [[nodiscard]] auto get_flow_overrides() const -> const std::optional<Kit_flow_overrides>&;
    // The settings with the call's overrides: what the flows read.
    [[nodiscard]] auto get_effective_flow_mask() const -> Kit_connector_types;
    [[nodiscard]] auto get_effective_auto_connect() const -> bool;
    // C3.13: the report the flows of the entry being recorded fill (cold, allocates); null outside the recording of an
    // entry (a preview, undo, redo). The first fill of a new entry clears it.
    [[nodiscard]] auto get_flow_report() -> Kit_flow_report*;
    // The report of the last recorded entry that ran a flow, and Operation_stack::get_recording_serial() of that entry.
    [[nodiscard]] auto get_last_flow_report() const -> const Kit_flow_report&;
    [[nodiscard]] auto get_last_flow_report_serial() const -> std::size_t;
    // The one-line summary of the last report, for the Level Kit window: made at the end of each follow operation's first execute.
    void update_flow_summary();
    // An undo, a redo, a new entry or a closed scene makes the summary stale.
    void clear_flow_summary();
    [[nodiscard]] auto get_flow_summary() const -> const std::string&;
    [[nodiscard]] static auto make_flow_piece(const erhe::scene::Node& node) -> Kit_flow_piece;
    // Executes the entry now; returns the refusal of a refused entry (nothing recorded), empty otherwise.
    [[nodiscard]] auto execute_entry(const std::shared_ptr<Operation>& operation) -> std::string;
    void report_new_connection(const Kit_new_connection& connection);
    void set_connector_snap_orientation(Connector_snap_orientation orientation);

    // D4.4, U51: the origin at the point (module-rounded), upright, turned by yaw_steps rotation steps about the
    // up axis, then the surface options searching against surface_normal (none: the settings' direction); no allocation.
    [[nodiscard]] auto make_placement_pose(
        Scene_root&                     scene_root,
        const erhe::math::Aabb&         local_bounds,
        const glm::vec3&                point,
        const std::optional<glm::vec3>& surface_normal,
        int                             yaw_steps,
        const Kit_placement_settings&   settings
    ) const -> Kit_placement_pose;

    // A material of the palette material's color, outside every content library; null for a name not in the palette.
    [[nodiscard]] auto get_preview_material(std::string_view palette_name) -> std::shared_ptr<erhe::primitive::Material>;
    // The scene's material of the name; null when its content library has none.
    [[nodiscard]] static auto find_scene_material(const Scene_root& scene_root, std::string_view name) -> std::shared_ptr<erhe::primitive::Material>;
    // K9.2: the values and slot material names a new piece joined to the target's connector takes from its supply, as the
    // placement's flow gives them; returns whether any changed. Allocates.
    [[nodiscard]] auto get_join_supplied_values(
        const erhe::scene::Node&  target,
        std::string_view          target_connector,
        std::string_view          connector,
        Kit_parameter_values&     io_values,
        std::vector<std::string>& io_slot_materials
    ) -> bool;
    // K9.1: the pieces a move of the nodes' subtrees moves through its flow (the floors it fits, the walls it fits and the
    // followers of all of them, by the follow operation's rules), and the joined connectors of the partners that stay. Allocates.
    void collect_flow_moved(
        const Scene_root&                                   scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> nodes,
        std::vector<std::shared_ptr<erhe::scene::Node>>&    out_moved,
        std::vector<Kit_connection_end>&                    out_joined
    ) const;

    // The default palette material names of the shape's slots, in slot order.
    void get_default_slot_materials(const Kit_shape_definition& shape, std::vector<std::string>& out_names) const;

    // The cached brush of the values, made on first use; null when its geometry fails.
    [[nodiscard]] auto get_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // The cached brushes still alive: held by a piece or by an operation of the undo history.
    [[nodiscard]] auto get_cached_brush_count() const -> std::size_t;

    // A new placement brush of the values outside the cache, with its shape's physics body (D2.3) and every primitive
    // type; null when its geometry fails.
    [[nodiscard]] auto make_brush(const Kit_parameter_values& values) -> std::shared_ptr<Brush>;

    // The Level Kit window's shape thumbnails (doc/editor/level_tools.md "Shape thumbnails").
    [[nodiscard]] auto get_thumbnails() -> Kit_thumbnails&;

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

    // D3.12: moves the piece, its children riding with it, so its connector meets the target's; one undo entry. With
    // attach the piece also records the attachment (step 41); a cycle of connections is allowed (C3.2).
    [[nodiscard]] auto connect_piece(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const Kit_connect& connect) -> Kit_connect_result;

    // K6.0 (doc/editor/level_tools.md "Connections"): one status per connection the node records, in record order. Allocates.
    void get_attachment_statuses(const erhe::scene::Node& node, std::vector<Kit_attachment_status>& out_statuses) const;
    // The status of one entry of the node's record: its target and whether it is followed.
    [[nodiscard]] auto get_attachment_status(const erhe::scene::Node& node, const Kit_attachment& attachment) const -> Kit_attachment_status;
    // S1.2: the node's surface attachment is active while its parent is the mesh node the record names. Allocates.
    [[nodiscard]] auto get_surface_attachment_status(const erhe::scene::Node& node) const -> Kit_surface_attachment_status;
    // Whether the node may record an attachment to the target.
    [[nodiscard]] auto check_attach(const erhe::scene::Node& node, const erhe::scene::Node& target) const -> Kit_attach_check;
    // The record of the join, owned by the node.
    [[nodiscard]] static auto make_connect_attachment(const erhe::scene::Node& node, const Kit_connect& connect) -> Kit_attachment;
    // K6.1, K13: the connection a join of the node (at the values) to the target records, on the end that holds it: the
    // node, or the target where the node is the host end (a wall's top joined onto a railing's base).
    [[nodiscard]] auto make_join_connection(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values, const Kit_connect& connect) const -> Kit_new_connection;
    // The write of the node's attachment (none: removed) as an operation; null when the node has it already.
    [[nodiscard]] static auto make_attachment_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::optional<Kit_attachment>& attachment) -> std::shared_ptr<Operation>;
    // K6.1: the node's connection of the connector removed, the others kept; none when it has no such connection.
    [[nodiscard]] static auto make_connection_removal_operation(const std::shared_ptr<erhe::scene::Node>& node, std::string_view connector) -> std::shared_ptr<Operation>;
    // The node's whole list of connections written; none when it is the list there already.
    [[nodiscard]] static auto make_attachments_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::vector<Kit_attachment>& attachments) -> std::shared_ptr<Operation>;
    // Removes the node's attachment as one undo entry; none when it has none.
    [[nodiscard]] auto detach_piece(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node) -> Kit_detach_result;
    // C9: removes every connection with an end in the nodes (kit pieces or floor layouts) as one undo entry.
    [[nodiscard]] auto detach_pieces(Scene_root& scene_root, std::span<const std::shared_ptr<erhe::scene::Node>> nodes) -> Kit_detach_result;
    // C6.2: removes the connection at the node's connector, whichever end records it, as one undo entry.
    [[nodiscard]] auto remove_connection(Scene_root& scene_root, const std::shared_ptr<erhe::scene::Node>& node, const std::string& connector) -> Kit_remove_connection_result;
    // K6.2: the other end of the connection at the node's connector, from either end's record; null when it is free (a record
    // of the node whose target is gone leaves it free). Allocates.
    [[nodiscard]] auto find_connection_at(const Scene_root& scene_root, const erhe::scene::Node& node, std::string_view connector) const -> std::shared_ptr<erhe::scene::Node>;
    // K6.7, C6.2: the scene's connected groups, the components of the graph of the records with both ends in the scene (dormant
    // ones included), in scene order of their first node; a node without a connection is in none. Cold, allocates.
    void get_connected_groups(const Scene_root& scene_root, std::vector<std::vector<std::shared_ptr<erhe::scene::Node>>>& out_groups) const;
    // K6.7: whether the record's two connectors are joined now, by the one rule of the connection's kind (a wall or floor
    // join: the connectors share a place; another: the pose is the join's). Allocates.
    [[nodiscard]] auto is_connection_joined(const erhe::scene::Node& owner, const Kit_attachment& attachment, const erhe::scene::Node& target) const -> bool;
    // The same with the other end's connector; nullopt when the connector is free. Allocates.
    [[nodiscard]] auto find_connection_end(const Scene_root& scene_root, const erhe::scene::Node& node, std::string_view connector) const -> std::optional<Kit_connection_end_at>;
    // K6.2 (C3.1): why joining the node's connector to the target's would give a connector a second connection, empty when it
    // would not. Each end must be free or held by this same pair, but for a record of the node's own on its connector, which
    // the join replaces. Allocates.
    [[nodiscard]] auto check_one_connection_per_connector(
        const Scene_root&        scene_root,
        const erhe::scene::Node& node,
        const std::string&       connector,
        const erhe::scene::Node& target,
        const std::string&       target_connector
    ) const -> std::string;
    // Whether any node of the scene carries an attachment value.
    [[nodiscard]] static auto has_attachments(const Scene_root& scene_root) -> bool;
    // The operation, appended last to a change's entry, whose first execute records the followers' moves, the detaches
    // and the snapped release's attachment (build_attachment_follow_ups()); null when there is nothing to follow or attach.
    [[nodiscard]] auto make_attachment_follow_operation(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
        const Kit_snap_pair*                                snap = nullptr,
        std::span<const std::shared_ptr<erhe::scene::Node>> requesting = {},
        std::span<const Kit_new_connection>                 new_connections = {},
        std::span<const Kit_surface_snap>                   surface_snaps = {}
    ) -> std::shared_ptr<Operation>;

    // Surface attachments (doc/editor/level_tools.md "Surface attachments", level_kits_surface_attachment.cpp).
    [[nodiscard]] static auto has_surface_attachments(const Scene_root& scene_root) -> bool;
    // S2.3: the node a piece resting on the mesh attaches to; null when the mesh is no support.
    [[nodiscard]] static auto find_surface_support(const erhe::scene::Mesh* mesh) -> std::shared_ptr<erhe::scene::Node>;
    // P1: the first ancestor of the node that is neither a kit piece nor a kit layout node (the scene root at the latest).
    [[nodiscard]] static auto get_surface_home(const erhe::scene::Node& node) -> std::shared_ptr<erhe::Hierarchy>;
    // S2.4: the node holds a connector connection: a record of its own, or another node's record naming it.
    [[nodiscard]] static auto has_connection(const Scene_root& scene_root, const erhe::scene::Node& node) -> bool;
    // The record of the node at its world pose under the support, the direction in world space; none for a pose without bounds.
    [[nodiscard]] static auto make_surface_record(
        const erhe::math::Aabb&  piece_bounds,
        const glm::mat4&         world_from_node,
        const erhe::scene::Node& support,
        const glm::vec3&         direction,
        Surface_orient           orient,
        Surface_snap_rest        rest
    ) -> std::optional<Kit_surface_attachment>;
    // Writes or (none) removes the node's record; null when it holds that already.
    [[nodiscard]] static auto make_surface_record_operation(const std::shared_ptr<erhe::scene::Node>& node, const std::optional<Kit_surface_attachment>& record) -> std::shared_ptr<Operation>;
    // S6.5: the author's parent change of the child; the removal of a record the new parent makes inert, null when none.
    [[nodiscard]] static auto make_reparent_surface_follow_up(const erhe::Hierarchy& child, const erhe::Hierarchy* new_parent) -> std::shared_ptr<Operation>;
    // The surface pass of a change's follow operation (S1.2, S2.1, S2.4, S2.5, S6): appended, not executed.
    void build_surface_attachment_follow_ups(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::span<const Kit_surface_snap>                   surface_snaps,
        std::span<const std::shared_ptr<erhe::scene::Node>> connected,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    );
    // S3.2 - S3.4: the carry of the piece (a child of the support, attached to it) at the support's and its own present bounds.
    [[nodiscard]] auto compute_surface_carry(
        Scene_root&                   scene_root,
        const erhe::scene::Mesh&      piece,
        const erhe::scene::Mesh&      support,
        const Kit_surface_attachment& record
    ) const -> std::optional<Kit_surface_carry>;
    // S3, S4, S6.4: carries the attached children of the changed pieces, parents before children, and the changed attached
    // pieces themselves, executing each operation as it is appended (a carried piece's children rest on its new pose). The
    // nodes the user moved are left to the surface judgement; out_carried lists the carried pieces.
    void build_surface_carry_follow_ups(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        Kit_surface_carry_record                            record_mode,
        std::vector<std::shared_ptr<Operation>>&            out_operations,
        std::vector<const erhe::scene::Node*>&              out_carried
    );
    // A layout generation's removal of the children not kept, from the last back, each subtree deepest first without the
    // attached pieces: those of child j park under the scene root when element j has a successor (j < successor_count),
    // else detach to their home (S5.2, S5.3, P7). Counts the removed children.
    void append_generation_removals(
        Scene_root&                                          scene_root,
        const std::vector<std::shared_ptr<erhe::Hierarchy>>& children,
        const std::vector<bool>&                             kept,
        std::size_t                                          successor_count,
        std::vector<std::shared_ptr<Operation>>&             out_operations,
        std::vector<Kit_parked_piece>&                       out_parked,
        std::size_t&                                         io_removed_count
    );
    // S5.2: after the inserts, the parked pieces go under their successors (local transforms kept, records naming them) and a
    // follow operation carries them on the new shapes.
    void append_generation_arrivals(
        Scene_root&                                            scene_root,
        std::span<const Kit_parked_piece>                      parked,
        std::span<const std::shared_ptr<erhe::scene::Node>>    successors,
        std::vector<std::shared_ptr<Operation>>&               out_operations
    );
    // S6.3: the detach of a node's surface attachment to its home, appended; nothing when it has no record.
    void append_surface_detach(const std::shared_ptr<erhe::scene::Node>& node, std::vector<std::shared_ptr<Operation>>& out_operations);
    [[nodiscard]] auto is_flow_previewing() const -> bool;
    // The follow operation of each scene of the nodes, whose moves are earlier operations of the same entry.
    void append_attachment_follow_ups(std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes, std::vector<std::shared_ptr<Operation>>& out_operations);
    // A write of Kit_piece.parameters or Kit_piece.shape outside these entry points (a paste, a raw row): the follow operation.
    void append_kit_piece_record_follow_ups(
        std::span<const std::shared_ptr<erhe::Item_base>> items,
        const erhe::property::Dependency_property&        property,
        std::vector<std::shared_ptr<Operation>>&          out_operations
    );
    // The moved nodes (moved by the user) stand at their new transforms and the changed pieces carry their new records:
    // joins again, in dependency order, each attached piece whose target is at or below one of them or a follower,
    // applied, one Node_transform_operation each; removes the attachment of a piece the change moved off its join; with
    // a snap pair the moved piece attaches when it now sits on a join of the pair (doc/editor/level_tools.md "Attachments").
    // C3 value flow (design K7, step 5): the pieces of new connections (requesting) and the neighbours of the edited
    // pieces take the values their neighbours supply, by priority, propagating in rounds; the operations and the pieces
    // they change. A floor takes no values; an edited piece keeps its own.
    void build_connection_flow(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> edited,
        std::span<const std::shared_ptr<erhe::scene::Node>> requesting,
        const Kit_snap_pair*                                snap,
        std::vector<std::shared_ptr<Operation>>&            out_operations,
        std::vector<std::shared_ptr<erhe::scene::Node>>&    out_changed
    );
    // Whether the node is at or below one of the moved nodes: the one "moved" rule of the floor fits, the wall fits and the
    // re-join of K5.5 (follow_attachments() keeps it as the pieces the user moved).
    [[nodiscard]] static auto is_moved(const erhe::scene::Node* node, std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes) -> bool;
    // C3.15, K5.3: one outline edit per floor under moved walls standing on its edges, all its changed edges solved together
    // (not executed); returns the refusal of an outline the floor refuses (K0.3), with no operations.
    [[nodiscard]] auto build_floor_fits(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    ) -> std::string;
    // C3.16: the length and pose change of each wall joined end to end to a moved wall (not executed); the following of
    // the same change leaves the pairs it fits alone (m_fitted_pairs, cleared by clear_fitted_pairs()).
    void build_wall_fits(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    );
    // C5: the connections of co-located free compatible connectors of the pieces (not executed); their owners.
    void build_auto_connections(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> pieces,
        std::span<const std::shared_ptr<erhe::scene::Node>> excluded, // pieces of an attach-none join: they declined a record
        std::vector<std::shared_ptr<Operation>>&            out_operations,
        std::vector<std::shared_ptr<erhe::scene::Node>>&    out_owners
    );
    void clear_fitted_pairs() { m_fitted_pairs.clear(); m_fitted_walls.clear(); }
    [[nodiscard]] auto get_fitted_walls() const -> const std::vector<std::shared_ptr<erhe::scene::Node>>& { return m_fitted_walls; }
    // The parameter change of set_parameters() as an operation, not executed; null when the values are the piece's.
    [[nodiscard]] auto make_parameters_operation(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values, std::string& out_error) -> std::shared_ptr<Operation>;
    void build_attachment_follow_ups(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
        const Kit_snap_pair*                                snap,
        std::vector<std::shared_ptr<Operation>>&            out_operations
    );

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

    // D3.5: after a scene has loaded, queues each piece to take the mesh and collision shape of its kit brush;
    // update_scene_refreshes() applies them over the next frames and then calls on_done.
    void refresh_scene(const std::shared_ptr<Scene_root>& scene_root, std::function<void()> on_done);
    // Once per frame: refreshes queued pieces, at most one of them making a new brush.
    void update_scene_refreshes();
    [[nodiscard]] auto get_pending_refresh_count() const -> std::size_t;

    // D7.3 on placed meshes (excluded from each other's hits); with move, the meshes that found a
    // surface move as one undo entry. Returns the moved count.
    auto snap_to_surface(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
        const Surface_options&                              options,
        Kit_snap_apply                                      apply,
        std::vector<Kit_snap_result>&                       out_results,
        std::string*                                        out_refusal = nullptr // a refused entry's reason (K0.3); then 0 is returned
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
    // surface_direction is the layout's search direction (the placement's, U51).
    [[nodiscard]] auto add_layout_initial(
        Scene_root&            scene_root,
        const Kit_layout_item& item,
        const glm::vec3&       position,
        const glm::quat&       rotation,
        const glm::vec3&       surface_direction
    ) -> Kit_layout_result;

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
    // Step 41: last the follow operation of the attachments, with the snapped release's pair when given.
    void append_layout_move_operations(
        Scene_root&                                         scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::vector<std::shared_ptr<Operation>>&            out_operations,
        const Kit_snap_pair*                                snap = nullptr,
        std::span<const Kit_surface_snap>                   surface_snaps = {}
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
    // before: the curve's path before the edit (a floor's shared corners move with it in its connected floors).
    void append_curve_follow_ups(const erhe::scene::Curve& curve, const erhe::math::Curve_path& before, std::vector<std::shared_ptr<Operation>>& out_operations);
    // The corner edits of the floors connected to the floor by floor connectors: each corner that shared a moved corner's
    // place moves with it (not executed; each edit's own follow-ups carry it on through the connected floors).
    void append_shared_corner_moves(Scene_root& scene_root, const erhe::scene::Curve& curve, const erhe::math::Curve_path& before, std::vector<std::shared_ptr<Operation>>& out_operations);
    // C8.4: the renames of the floor's edge connections an edit adding or removing corners needs (not executed).
    void append_floor_edge_renames(Scene_root& scene_root, const erhe::scene::Curve& curve, const erhe::math::Curve_path& before, std::vector<std::shared_ptr<Operation>>& out_operations);

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
    // Before a row writes one transform component: the flow preview's run is taken back, so the write goes over the drag's
    // own pose and not over a pose the run gave a moved node (K5.5).
    void prepare_layout_transform_write();
    void end_layout_transform_preview();

    // Lays every previewed layout out again from its live values, transform and curve; returns the first refusal. A move
    // preview also runs the property flow of the step (K9.1) with the step's connector snap pair.
    auto step_layout_preview(const Kit_snap_pair* snap = nullptr) -> std::string;
    [[nodiscard]] auto get_flow_preview_state() const -> const Kit_flow_preview_state&;
    // A move preview's flow runs or is taken back now: its writes to the moved nodes are transient, not edits of them.
    [[nodiscard]] auto is_move_flow_previewing() const -> bool { return m_move_flow_previewing; }

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
        std::vector<std::shared_ptr<Operation>>&         out_operations,
        std::shared_ptr<erhe::scene::Node>*              out_node = nullptr // the inserted child
    ) -> std::string;

    // The refusal of a stairs material name: neither a scene material nor a palette material.
    [[nodiscard]] auto check_stairs_material(const Scene_root& scene_root, const std::string& material_name) const -> std::string;
    // The same for any slot's palette default (empty name) or a named material; what names the value in the message.
    [[nodiscard]] auto check_slot_material_name(const Scene_root& scene_root, const char* slot, const std::string& material_name, const char* what) const -> std::string;

    // A generated layout child (the staircase, a floor's slab): its node name, its material slot and its crease angle.
    class Generated_child
    {
    public:
        const char* name          {""};
        const char* slot          {""};
        const char* what          {""};
        float       crease_degrees{0.0f};
    };
    [[nodiscard]] auto make_generated_child_insert(
        const Generated_child&                           child,
        Scene_root&                                      scene_root,
        const std::shared_ptr<erhe::scene::Node>&        layout,
        const glm::mat4&                                 world_from_layout,
        const std::shared_ptr<erhe::geometry::Geometry>& geometry,
        const std::string&                               material_name,
        const std::string&                               source,
        std::vector<std::shared_ptr<Operation>>&         out_operations,
        std::shared_ptr<erhe::scene::Node>*              out_node = nullptr
    ) -> std::string;

    // C8: the generation of a floor layout: the one generated slab the closed curve and the values make.
    [[nodiscard]] auto build_floor_generation(
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& node,
        const Kit_layout_data&                    data,
        const glm::mat4&                          world_from_layout,
        Kit_layout_reuse                          reuse,
        std::vector<std::shared_ptr<Operation>>&  out_operations,
        Kit_layout_result&                        result
    ) -> std::string;

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

    // Step 41: an attached piece of a scene with its connectors resolved for following.
    class Attached_piece
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> target{};     // null: not in the scene, or its uid names more than one node
        Kit_attachment                     attachment;
        Kit_connector                      own{};        // local to node
        Kit_connector                      target_local{}; // local to target
        const char*                        link_dormant  {nullptr}; // the target missing, ambiguous or owned by a locked layout
        const char*                        own_dormant   {nullptr}; // the piece's connector missing or disabled
        const char*                        target_dormant{nullptr}; // the target's connector missing or disabled
        bool                               affected{false};
        bool                               done    {false};
        bool                               rigid   {true};  // followed by a rigid join (not a wall join, not masked off)
        bool                               joined  {false}; // moved onto its join in this change

        // Why it is not followed; null when it is.
        [[nodiscard]] auto dormant() const -> const char*
        {
            return (link_dormant != nullptr) ? link_dormant : ((own_dormant != nullptr) ? own_dormant : target_dormant);
        }
    };
    class Follow_move
    {
    public:
        std::shared_ptr<erhe::scene::Node>         node;
        erhe::scene::Trs_transform                 before;
        erhe::scene::Trs_transform                 after;
        std::optional<erhe::scene::Xform_op_stack> xform_op_stack_before;
    };
    class Follow_detach
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> target;
        std::string                        connector; // the piece's connection that came apart
        const char*                        reason{""};
    };
    // The live following of a drag: the attached pieces gathered at its begin and the followers' poses from before it.
    class Follow_preview
    {
    public:
        Scene_root*                                     scene_root{nullptr};
        std::vector<std::shared_ptr<erhe::scene::Node>> moved;
        std::vector<std::shared_ptr<erhe::scene::Node>> changed;
        std::vector<Attached_piece>                     attached;
        std::vector<Follow_move>                        moves;
        Kit_connector_switches                          changed_switches; // a parameter drag's piece, read at its begin
    };
    // K9.2: the shape's mapped properties take the supply (lengths by take_supplied_length(), slot materials by name).
    [[nodiscard]] static auto apply_join_supply(
        const Kit_shape_definition& shape,
        const Kit_supply_list&      supply,
        Kit_parameter_values&       io_values,
        std::vector<std::string>&   io_slot_materials
    ) -> bool;
    // A join followed by a rigid re-join: not a join of two wall connectors, a type the mask lets through.
    [[nodiscard]] static auto is_rigid_join(const Attached_piece& piece, Kit_connector_types mask) -> bool;
    // Marks transitively every rigid follower whose target is at or below a dirty node (none at or below a user node) and
    // appends it to dirty.
    static void mark_followers(std::span<Attached_piece> attached, const std::vector<const erhe::scene::Node*>& user, std::vector<const erhe::scene::Node*>& dirty);
    // C3.16: the wall the entry's join of two wall connectors fits when exactly one end moved: the other end, joined by its
    // start or end, standing on no floor; null otherwise.
    [[nodiscard]] static auto get_wall_fit_wall(
        const Attached_piece&                                 entry,
        const std::function<bool(const erhe::scene::Node*)>& is_moved,
        std::span<const Attached_piece>                       attached
    ) -> const std::shared_ptr<erhe::scene::Node>*;
    // C3.15: a join onto a floor layout's edge connector.
    [[nodiscard]] static auto is_floor_edge_join(const erhe::scene::Node& target, std::string_view target_connector) -> bool;
    // The attached pieces of the scene with their connectors resolved from their records (uids looked up once).
    void gather_attachments(const Scene_root& scene_root, std::vector<Attached_piece>& out_attached) const;
    // K11.2 (C6.5): the record writes (not executed) dropping each connection of a replaced piece (at its new values) whose
    // connector the new shape lacks or whose types no longer meet the other end's; each drop is listed on its pieces.
    void append_replace_connection_drops(const Scene_root& scene_root, Kit_replace_result& result, std::vector<std::shared_ptr<Operation>>& out_operations) const;
    // The connector of the piece's values by name, enabled by the switches; the dormant reason otherwise. No allocation at capacity.
    auto resolve_connector_of_values(
        const Kit_parameter_values&   values,
        const Kit_connector_switches& switches,
        const std::string&            name,
        Kit_connector&                out_connector
    ) -> const char*;
    // Joins the followers of the change in dependency order (see build_attachment_follow_ups()); a node first moved here
    // adds its pose from before to out_moves. With out_detached, the pieces the change moved off their join go there.
    void follow_attachments(
        Scene_root&                                         scene_root,
        std::span<Attached_piece>                           attached,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_nodes,
        std::span<const std::shared_ptr<erhe::scene::Node>> changed_pieces,
        std::vector<Follow_move>&                           out_moves,
        std::vector<Follow_detach>*                         out_detached
    );
    // The attachment of the snapped pair when the piece now sits on a join of it and may attach; none otherwise.
    [[nodiscard]] auto find_snap_attachment(const Kit_snap_pair& snap) const -> std::optional<Kit_attachment>;
    // Gives the preview's followers their poses from before it; forgets them.
    void restore_follow_preview(Follow_preview& preview);
    void step_parameter_follow_preview(const std::shared_ptr<erhe::scene::Node>& node, const Kit_parameter_values& values);
    // S3.7, S6.4: a parameter drag step rests the dragged piece on its support and carries its attached children; a node
    // first moved adds its pose to io_moves.
    void step_surface_carry_preview(Scene_root& scene_root, const erhe::scene::Mesh& dragged, std::vector<Follow_move>& io_moves);
    Kit_connector_types m_follow_flow_mask{Kit_connector_types::all()}; // C3.9: read at each following
    // The piece of the attach-none join connect_piece() is building; each follow operation made meanwhile keeps a copy.
    std::vector<std::shared_ptr<erhe::scene::Node>> m_auto_connect_excluded;
    std::vector<std::pair<const erhe::scene::Node*, const erhe::scene::Node*>> m_fitted_pairs; // C3.16: (attached piece, target) entries a wall fit set
    std::vector<std::shared_ptr<erhe::scene::Node>>                            m_fitted_walls; // C3.16: the walls it fitted (changed pieces of the following)
    Follow_preview                        m_parameter_follow_preview; // a live parameter drag
    std::vector<const erhe::scene::Node*> m_follow_user;              // the change's moved nodes: moved by the user
    std::vector<const erhe::scene::Node*> m_follow_dirty;             // nodes whose connectors move in this change
    std::vector<const erhe::scene::Node*> m_follow_joined;            // the followers joined in this change
    std::vector<Kit_connector>            m_follow_connectors;        // scratch of resolve_connector_of_values()
    std::vector<Kit_follow_entry>         m_follow_entries;           // scratch of follow_attachments()
    Kit_follow_order                      m_follow_order;
    std::optional<Kit_flow_overrides>     m_flow_overrides;
    bool                                  m_flow_previewing     {false}; // a preview runs a flow: no report, trace logs
    bool                                  m_move_flow_previewing{false}; // of those, a move preview's run (K9.1)
    // A flow's log line runs at every preview step: trace while a preview runs it.
    [[nodiscard]] auto flow_log_level(spdlog::level::level_enum level) const -> spdlog::level::level_enum;
    std::size_t                           m_flow_report_serial{0};
    std::string                           m_flow_summary;
    Kit_flow_report                       m_flow_report;

    App_context&                                m_context;
    Kit_shape_library                           m_library;
    // Weak: pieces (Brush_placement.brush) and undo entries hold the brushes (doc/editor/level_tools.md).
    std::map<std::string, std::weak_ptr<Brush>> m_brushes;
    // By palette material name.
    std::map<std::string, std::shared_ptr<erhe::primitive::Material>> m_preview_materials;
    std::unique_ptr<Kit_thumbnails>                                   m_thumbnails;

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
        std::optional<erhe::math::Curve_path>                   curve_before; // a floor's path at the begin (shared corners)
    };
    // K9.1: the property flow of a move preview (a Transform tool drag, a Properties transform row drag). Each step runs
    // the release's follow operation on the step's poses and keeps it applied until the next step undoes it.
    class Flow_preview
    {
    public:
        Scene_root*                                     scene_root{nullptr};
        std::vector<std::shared_ptr<erhe::scene::Node>> moved;
        std::vector<erhe::scene::Trs_transform>         inputs;     // the moved nodes' poses the applied run started from
        std::vector<erhe::scene::Trs_transform>         outputs;    // their poses after it
        std::vector<erhe::scene::Trs_transform>         tool_poses; // per step: the poses the drag gave them
        std::optional<Kit_snap_pair>                    snap;       // of the applied run
        std::shared_ptr<Operation>                      run;        // the last valid run; null when it had nothing to do
        bool                                            applied{false}; // run is executed in the scene
        bool                                            ran    {false}; // inputs, outputs and snap describe a run
    };
    // The drag's poses of the moved nodes into tool_poses: a node the run moved that stands at the run's output keeps the
    // run's input. No allocation at capacity.
    void read_flow_preview_tool_poses();
    [[nodiscard]] auto is_flow_preview_held(const Kit_snap_pair* snap) const -> bool;
    // Undoes the applied run; the moved nodes take tool_poses.
    void undo_flow_preview_run();
    void run_flow_preview(const Kit_snap_pair* snap);
    // Undoes the applied run, gives the moved nodes the last step's poses and forgets the preview.
    void end_flow_preview();
    // Drops the preview without touching the scene (its scene closes).
    void forget_flow_preview();
    void set_flow_preview_poses(std::span<const erhe::scene::Trs_transform> poses);
    Flow_preview                                    m_flow_preview;
    Kit_flow_preview_state                          m_flow_preview_state;
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

    class Scene_refresh
    {
    public:
        std::shared_ptr<Scene_root>                     scene_root;
        std::vector<std::weak_ptr<erhe::scene::Mesh>>   pieces{};
        std::size_t                                     next{0};
        Kit_refresh_report                              report{};
        std::function<void()>                           on_done;
    };
    // Returns true when it made a new brush.
    auto refresh_piece(Scene_refresh& refresh, const std::shared_ptr<erhe::scene::Mesh>& mesh) -> bool;
    void finish_scene_refresh(Scene_refresh& refresh);
    std::vector<Scene_refresh>                              m_scene_refreshes; // in load order
};

} // namespace editor
