#pragma once

#include "tools/tool.hpp"
#include "assets/asset_reference.hpp"
#include "brushes/reference_frame.hpp"
#include "level/kit_item_payload.hpp"
#include "level/kit_shape.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_view.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_geometry/types.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct Scene_config;

namespace erhe { class Item_host; }

namespace editor {

class Brush;
class Brush_tool;
class Content_library;
class App_message_bus;
struct Hover_scene_view_message;
struct Hover_mesh_message;
struct Selection_message;
class App_scenes;
class Icon_set;
class Item_tree;
class Operation_stack;
class Operations;
class Render_context;
class Tools;
class Headset_view;

class Brush_preview_command : public erhe::commands::Command
{
public:
    Brush_preview_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Brush_insert_command : public erhe::commands::Command
{
public:
    Brush_insert_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

class Brush_rotate_command : public erhe::commands::Command
{
public:
    Brush_rotate_command(erhe::commands::Commands& commands, App_context& context, int direction);
    auto try_call() -> bool override;

private:
    App_context& m_context;
    int          m_direction{1};
};

class Brush_pick_command : public erhe::commands::Command
{
public:
    Brush_pick_command(erhe::commands::Commands& commands, App_context& context);

    auto try_call() -> bool override;

private:
    App_context& m_context;
};

// Step 40 (R5.3): the kit mode preview joined at an enabled connector of the hovered piece; a click places through
// Kit_placement::connect (doc/editor/level_tools.md "Placing in the viewport").
class Kit_brush_join
{
public:
    std::weak_ptr<erhe::scene::Node> target;
    std::uint64_t                    target_id       {0};
    const char*                      target_connector{""}; // literal names (Kit_connector::name)
    const char*                      connector       {""}; // of the new piece
    int                              twist_steps     {0};
    float                            distance        {0.0f}; // from the hover point to the target connector
};

// What kit mode places: the tool's own (the Level Kit window's choice) or a dragged kit item's.
class Kit_brush_source
{
public:
    void clear();

    Kit_parameter_values                                    values;
    std::shared_ptr<Brush>                                  brush;             // set while the source is in use
    std::vector<std::string>                                slot_materials;    // palette names, one per slot
    std::vector<std::shared_ptr<erhe::primitive::Material>> preview_materials; // their preview materials
    std::vector<Kit_connector>                              connectors;        // the shape's at the values
    int                                                     yaw_steps{0};
};

class Brush_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Brush_tool(
        const Scene_config&       scene_config,
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );

    // Implements Tool
    void tool_render           (const Render_context& context)           override;
    void tool_properties       (erhe::imgui::Imgui_window& imgui_window) override;
    void handle_priority_update(int old_priority, int new_priority)      override;

    // Active brush (set from hotbar slot)
    void set_active_brush  (const std::shared_ptr<Brush>& brush);
    // Cached reference, for the MCP get_editor_references query
    // (doc/editor/import_undo_reference_clearing.md).
    [[nodiscard]] auto get_active_brush       () const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto get_drag_and_drop_brush() const -> const std::shared_ptr<Brush>&;
    void clear_active_brush();

    // Commands
    auto try_insert_ready     () -> bool;
    auto try_insert           (Brush* brush = nullptr) -> bool;
    auto try_pick             () -> bool;
    auto try_rotate           (int direction) -> bool;
    void on_motion            ();
    void preview_drag_and_drop(std::shared_ptr<Brush> brush);

    [[nodiscard]] auto get_hover_brush() const -> std::shared_ptr<Brush>;

    // Kit mode (doc/editor/brushes.md "Kit mode", D4.4): places the kit brush of the values with the palette
    // materials of the slots (by name, in slot order; a missing or empty name: the slot's default) through
    // Level_kits::place(). A different shape resets the rotation steps.
    void set_kit_brush (const Kit_parameter_values& values, const std::shared_ptr<Brush>& brush, std::span<const std::string> slot_materials);
    void leave_kit_mode();
    [[nodiscard]] auto is_kit_mode         () const -> bool;
    // The shape of kit mode; null outside it.
    [[nodiscard]] auto get_kit_shape       () const -> const Kit_shape_definition*;
    [[nodiscard]] auto get_kit_values      () const -> const Kit_parameter_values&;
    [[nodiscard]] auto get_kit_brush       () const -> const std::shared_ptr<Brush>&;
    [[nodiscard]] auto get_kit_slot_materials() const -> const std::vector<std::string>&;
    [[nodiscard]] auto get_kit_yaw_steps   () const -> int;

    // A kit item dragged over a viewport (doc/editor/level_tools.md "Dragging a kit item"): its preview takes the place
    // of kit mode's until the drag leaves that viewport; the tool's own kit state stays as it was. Prepare phase calls.
    void begin_kit_item_preview(const Kit_item_payload& payload, const void* viewport);
    void end_kit_item_preview  (const void* viewport);
    // Places the dragged item at its preview's pose as a kit mode click would, then ends the preview.
    void drop_kit_item         (const void* viewport);
    [[nodiscard]] auto is_kit_item_preview        () const -> bool;
    [[nodiscard]] auto get_kit_item_values        () const -> const Kit_parameter_values&;
    [[nodiscard]] auto get_kit_item_slot_materials() const -> const std::vector<std::string>&;
    [[nodiscard]] auto get_kit_item_brush         () const -> const std::shared_ptr<Brush>&;
    // The pose the preview shows and a click places; none while there is no preview.
    [[nodiscard]] auto get_kit_pose        () const -> std::optional<Kit_placement_pose>;
    // The connector join the preview shows and a click places; none while there is no joined preview.
    [[nodiscard]] auto get_kit_join        () const -> std::optional<Kit_brush_join>;
    // K9.2: the values and slot materials the joined preview shows, taken from the target's supply; null when the
    // preview shows the kit values (no join, or nothing supplied changes them).
    [[nodiscard]] auto get_kit_join_values() const -> const Kit_parameter_values*;
    [[nodiscard]] auto get_kit_join_slot_materials() const -> const std::vector<std::string>&;
    // Whether the last preview update found the connector snap suspend key held (U27).
    [[nodiscard]] auto is_kit_join_suspended() const -> bool;
    // A connector snap suspend key event: the preview follows the key at its edge.
    void on_connector_snap_suspend_key_edge();
    [[nodiscard]] auto get_hover_world_position() const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_hover_world_normal  () const -> std::optional<glm::vec3>;
    // The world origin of the shown preview mesh's node; none without a preview.
    [[nodiscard]] auto get_preview_world_position() const -> std::optional<glm::vec3>;

private:
    void on_hover_scene_view               (Hover_scene_view_message& message);
    void on_hover_mesh                     (Hover_mesh_message& message);
    // Scene close: drop tool state referring to the closing scene's content -
    // the active / drag-and-drop brush hosted by its content library, and the
    // preview mesh/node parented into it (a close can arrive mid-hover). A
    // hotbar slot holding the same brush intentionally keeps it alive;
    // clicking the slot re-activates the now host-less orphan (brush payloads
    // are self-contained). The active brush is an Asset_reference (declared
    // usership, R3), so keeping it would only report as intentionally pinned -
    // but transient tool state is not a persistence mechanism (the inventory
    // is), so it is still cleared here as belt-and-braces.
    void on_close_scene                    (erhe::Item_host* closing_host);
    // Content removed without a scene closing (undo of a glTF import):
    // drop the reference when it names one of the removed items.
    void on_items_removed(const Removed_items& removed);
    // Choosing a palette brush in a content library leaves kit mode.
    void on_selection(const Selection_message& message);
    [[nodiscard]] auto try_insert_kit_piece() -> bool;
    // The source the preview shows and a click places: the dragged kit item over its viewport, else the tool's own.
    [[nodiscard]] auto kit() -> Kit_brush_source&;
    [[nodiscard]] auto kit() const -> const Kit_brush_source&;
    void fill_kit_source(Kit_brush_source& source, const Kit_parameter_values& values, const std::shared_ptr<Brush>& brush, std::span<const std::string> slot_materials);
    void take_next_kit_seed(const Scene_root& scene_root);
    void update_kit_preview_transform(Brush& brush, const std::optional<Hover_placement_hit>* motion_placement);
    void update_kit_join(Scene_root& scene_root, const Hover_placement_hit& placement);
    [[nodiscard]] auto apply_kit_join(
        Scene_root&                               scene_root,
        const std::shared_ptr<erhe::scene::Node>& piece,
        const char*                               target_connector_name,
        const Kit_connector&                      target_world,
        const Kit_connector&                      moved,
        int                                       twist_steps,
        float                                     distance
    ) -> bool;
    void refresh_kit_join();
    void clear_kit_join();
    // K9.2: the join's moved connector at the values the target supplies (computed when the pair or the target changed);
    // the kit values' own when nothing changes them.
    [[nodiscard]] auto get_kit_join_moved_connector(const erhe::scene::Node& target, const char* target_connector, const Kit_connector& moved) -> const Kit_connector&;
    // The preview mesh shows the joined values' brush while joined with supplied values, the kit brush otherwise.
    void sync_kit_preview_primitives();
    // Another kit piece's enabled connector sits on the world connector and faces it (a join made already).
    [[nodiscard]] auto is_kit_connector_joined(const Scene_root& scene_root, const erhe::scene::Node& piece, const Kit_connector& connector) -> bool;
    // The join's target or a node above it moved, or a piece's connector switches changed: the preview joins again.
    void on_kit_join_target_changed(const erhe::scene::Node* node);
    // motion_placement: the view's placement hit on_motion() computed this motion; null when the caller has none.
    void update_preview_mesh               (const std::optional<Hover_placement_hit>* motion_placement = nullptr);
    void do_insert_operation               (Brush& brush);
    void add_preview_mesh                  (Brush& brush, const std::optional<Hover_placement_hit>* motion_placement = nullptr);
    void remove_preview_mesh               ();
    void update_preview_mesh_node_transform(const std::optional<Hover_placement_hit>* motion_placement = nullptr);

    // The grid hit's point, grid-snapped when Snap To Grid is on.
    [[nodiscard]] auto get_grid_point(const Hover_placement_hit& placement) const -> glm::vec3;

    [[nodiscard]] auto update_hover_frame_from_mesh() -> bool;
    // The placement hit of the view (Level_kits::get_placement_hover()) as a hover entry; empty when there is none.
    [[nodiscard]] auto make_placement_hover_entry(const Scene_view& scene_view, const std::optional<Hover_placement_hit>& placement) const -> Hover_entry;
    [[nodiscard]] auto update_hover_frame_from_grid() -> bool;
    [[nodiscard]] auto update_brush_frame          (Brush& brush) -> bool;
    [[nodiscard]] auto get_placement_facet  () const -> GEO::index_t;
    [[nodiscard]] auto get_placement_corner0() const -> GEO::index_t;

    // The brush the tool acts on: drag-and-drop preview wins, then the
    // active (hotbar-selected) brush, then the last selected brush.
    [[nodiscard]] auto get_effective_brush() const -> std::shared_ptr<Brush>;

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>       m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Close_scene_message>      m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;
    erhe::message_bus::Subscription<Selection_message>     m_selection_subscription;
    erhe::message_bus::Subscription<Node_touched_message>  m_node_touched_subscription;
    erhe::message_bus::Subscription<Kit_piece_connectors_changed_message> m_kit_piece_connectors_changed_subscription;
    erhe::message_bus::Subscription<Curve_changed_message>                m_curve_changed_subscription;
    erhe::message_bus::Subscription<Kit_layout_changed_message>           m_kit_layout_changed_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message>        m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Mesh_materials_changed_message>       m_mesh_materials_changed_subscription;
    Brush_preview_command                   m_preview_command;
    Brush_insert_command                    m_insert_command;
    Brush_pick_command                      m_pick_command;
    Brush_rotate_command                    m_rotate_cw_command;
    Brush_rotate_command                    m_rotate_ccw_command;
    erhe::commands::Lambda_command          m_toggle_brush_preview_command;
    erhe::commands::Float_threshold_command m_pick_using_float_input_command;

    ERHE_PROFILE_MUTEX(std::mutex,     m_brush_mutex);
    Asset_reference                    m_active_brush          {};
    std::shared_ptr<Brush>             m_drag_and_drop_brush   {};
    float                              m_preview_hover_distance{0.001f};
    bool                               m_snap_to_hover_polygon {true};
    bool                               m_snap_to_grid          {true};
    bool                               m_scale_to_match        {true};
    bool                               m_use_matching_face     {true};
    bool                               m_use_selected_face     {false};
    bool                               m_debug_visualization   {false};
    bool                               m_show_receiver_frame   {true};
    bool                               m_show_brush_frame      {true};
    bool                               m_show_preview          {true};

    bool                               m_parent_to_active        {false};
    bool                               m_parent_to_scene_root    {true};
    bool                               m_parent_to_hovered       {false};

    Hover_entry                        m_hover; // the placement hover: foliage passed through
    std::shared_ptr<erhe::scene::Mesh> m_preview_mesh;
    std::shared_ptr<erhe::scene::Node> m_preview_node;
    bool                               m_with_physics   {true};
    float                              m_scale          {1.0f};
    float                              m_transform_scale{1.0f};
    float                              m_grid_scale     {1.0f};
    int                                m_polygon_offset {0};
    int                                m_corner_offset  {0};
    std::optional<std::size_t>         m_selected_corner_count{};
    std::optional<GEO::index_t>        m_hover_facet_corner_count;
    std::optional<glm::mat4>           m_world_from_hover;
    std::optional<Reference_frame>     m_hover_frame;
    std::optional<Reference_frame>     m_brush_placement_frame;
    std::optional<glm::mat4>           m_hover_transform;
    std::optional<glm::mat4>           m_align_transform;
    std::optional<glm::vec3>           m_hover_world_position;
    std::optional<glm::vec3>           m_hover_world_normal;

    // Kit mode: active while m_kit.brush is set.
    Kit_brush_source                           m_kit;
    Kit_brush_source                           m_kit_item;                    // the dragged kit item's, while previewed
    const void*                                m_kit_item_viewport{nullptr};  // the viewport window the drag is over
    std::optional<Kit_placement_pose>          m_kit_pose;
    std::vector<Kit_connector>                 m_kit_target_connectors;   // hover scratch: the hovered piece's in world space
    std::optional<Kit_brush_join>              m_kit_join;
    // K9.2: the supplied values of the last join pair, kept until the pair, the target or the kit values change.
    class Kit_join_flow
    {
    public:
        bool                                                    valid {false};
        bool                                                    flowed{false};
        bool                                                    piece_mode{true}; // not Place As Layout: a layout takes no values
        std::uint64_t                                           target_id{0};
        std::string                                             target_connector;
        std::string                                             connector;
        Kit_parameter_values                                    values;
        std::vector<std::string>                                slot_materials;
        std::vector<std::shared_ptr<erhe::primitive::Material>> materials; // the preview's, per slot
        std::vector<Kit_connector>                              connectors;
        std::shared_ptr<Brush>                                  brush;
    };
    Kit_join_flow                              m_kit_join_flow;
    std::weak_ptr<Brush>                       m_kit_preview_shown; // the brush whose primitives the preview mesh has
    bool                                       m_kit_join_suspended{false};
    ///std::unique_ptr<Item_tree>         m_brush_item_tree;
};

}
