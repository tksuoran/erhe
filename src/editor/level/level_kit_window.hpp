#pragma once

#include "app_message.hpp"
#include "level/kit_connector.hpp"
#include "level/kit_item.hpp"
#include "level/kit_layout_editor.hpp"
#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape.hpp"
#include "level/level_group.hpp"
#include "level/level_queries.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::scene {
    class Curve;
    class Mesh;
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

enum class Spatial_query : unsigned int {
    support_report = 0,
    region_contents,
    height_map,
    overlap_report,
    level_check
};

// A press of the Group section (D4.8).
enum class Group_action : unsigned int {
    group = 0,
    ungroup,
    duplicate,
    mirror_x,
    mirror_z
};

// A press of the Connect rows (D4.11): move the other selected piece, or place the shape to place.
enum class Connect_action : unsigned int {
    move_piece = 0,
    place_shape
};

// A result row of the overlap report or the level check: a click selects its meshes.
class Spatial_query_row
{
public:
    std::string                                     label;
    std::array<std::weak_ptr<erhe::scene::Mesh>, 2> meshes;
    std::size_t                                     mesh_count{0};
};

// The "Level Kit" window (doc/editor/level_tools.md "Level Kit window", D4.1).
class Level_kit_window : public erhe::imgui::Imgui_window
{
public:
    Level_kit_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void level_settings_imgui   (Scene_root& scene_root);
    void shapes_imgui           (const std::shared_ptr<Scene_root>& scene_root);
    void place_parameters_imgui (const Scene_root& scene_root);
    void placement_settings_imgui();
    void selection_imgui        (const std::shared_ptr<Scene_root>& scene_root);
    void select_shape           (const Kit_shape_definition& shape, const Scene_root& scene_root);
    // The row's drag source (doc/editor/level_tools.md "Dragging a kit item"): the kit item a click on the row would choose.
    void kit_item_drag_source   (const Kit_shape_definition& shape, const Scene_root& scene_root, float thumbnail_size);
    [[nodiscard]] auto make_kit_item(const Kit_shape_definition& shape, const Scene_root& scene_root) const -> Kit_item;
    // D3.4: the selected piece's slot takes the palette material in the queued call, one undo entry.
    void queue_slot_material    (const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece, std::size_t slot, const std::string& palette_name);
    void on_kit_values_changed  ();
    // Hands the Brush tool the kit brush of m_place_values and makes it the active tool in kit mode (D4.4).
    void queue_kit_brush        ();
    void update_place_hint      ();
    void queue_snap_selected    (const std::shared_ptr<Scene_root>& scene_root, Surface_orient orient);
    void replace_imgui          (const std::shared_ptr<Scene_root>& scene_root);
    void queue_replace          (const std::shared_ptr<Scene_root>& scene_root, const Kit_shape_definition& shape);
    void connect_imgui          (const std::shared_ptr<Scene_root>& scene_root);
    void connection_rows_imgui  ();
    void detach_imgui           (const std::shared_ptr<Scene_root>& scene_root);
    // C9.2: in the queued call, Level_kits::detach_pieces() of the selected kit pieces and floor layouts of the scene.
    void queue_detach           (const std::shared_ptr<Scene_root>& scene_root);
    void update_connect_pieces  (const Scene_root& scene_root);
    // Hands the chosen pair to the connector indicators (doc/editor/level_tools.md "Connector indicators").
    void publish_connect_choice ();
    // D3.12: in the queued call, joins the moved piece or a new piece of the shape to place to the target's connector.
    void queue_connect          (const std::shared_ptr<Scene_root>& scene_root, Connect_action action);
    void layout_imgui           (const std::shared_ptr<Scene_root>& scene_root);
    void group_imgui            (const std::shared_ptr<Scene_root>& scene_root);
    // D3.8: runs the action in the queued call on the selected nodes (group) or the selected group node.
    void queue_group_action     (const std::shared_ptr<Scene_root>& scene_root, Group_action action, const std::shared_ptr<erhe::scene::Node>& group);
    void spatial_queries_imgui  (const std::shared_ptr<Scene_root>& scene_root);
    void query_entries_imgui    (const char* id, const std::vector<std::weak_ptr<erhe::scene::Mesh>>& meshes);
    void query_rows_imgui       (const char* id, const std::vector<Spatial_query_row>& rows);
    void select_meshes          (const std::vector<std::weak_ptr<erhe::scene::Mesh>>& meshes);
    // D7: computed in the queued call of the button press and kept until the next press.
    void queue_spatial_query    (const std::shared_ptr<Scene_root>& scene_root, Spatial_query query);
    void run_support_report     (Scene_root& scene_root);
    void run_region_contents    (Scene_root& scene_root);
    void run_height_map         (Scene_root& scene_root);
    void run_overlap_report     (Scene_root& scene_root);
    void run_level_check        (Scene_root& scene_root);
    void collect_selected_meshes(const Scene_root& scene_root, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes) const;

    [[nodiscard]] auto find_selected_piece(const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto count_selected_meshes(const Scene_root& scene_root) const -> std::size_t;
    [[nodiscard]] auto count_selected_nodes (const Scene_root& scene_root) const -> std::size_t;
    // The active item when it is a selected group node (an Xform) of the scene, else the first selected one.
    [[nodiscard]] auto find_selected_group  (const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Node>;

    App_context& m_context;

    // The shape to place, its parameter values and its slots' palette materials (names, in slot order).
    const Kit_shape_definition* m_shape{nullptr};
    Kit_parameter_values        m_place_values;
    std::vector<std::string>    m_place_slot_materials;
    std::string                 m_place_message; // validate() of m_place_values; empty when valid

    // The line naming the Brush tool's place and rotate bindings; rebuilt when the bindings change.
    std::string m_place_hint;
    bool        m_place_hint_dirty{true};

    // The level lengths while one of their rows is dragged; committed when the drag ends.
    float m_module     {1.0f};
    float m_wall_height{3.0f};
    bool  m_lengths_editing{false};

    // The selected piece's values as last read from its record.
    Kit_piece_editor m_piece_editor;

    // The Layout section: the initial values of a new kit layout node (R5.12.9).
    Kit_layout_initial_editor m_layout_editor;

    // Replace (D4.10): the target shape and the count of selected pieces, counted again on the selection and active item messages.
    const Kit_shape_definition* m_replace_shape{nullptr};
    std::string                 m_replace_preview{"(choose a shape)"};
    std::vector<std::string>    m_replace_labels; // per library shape, made once
    const Scene_root*           m_replace_scene{nullptr};
    std::size_t                 m_replace_piece_count{0};
    bool                        m_replace_selection_changed{true};
    std::string                 m_replace_message;
    bool                        m_replace_failed{false};

    // Connect (D4.11): the target is the active selected piece (else the first), the moved piece the other of two selected
    // pieces; both and their connectors are read again on the selection, active item and mesh messages.
    std::weak_ptr<erhe::scene::Node> m_connect_target;
    std::weak_ptr<erhe::scene::Node> m_connect_moved;
    std::vector<Kit_connector>       m_connect_target_connectors;
    std::vector<Kit_connector>       m_connect_moved_connectors;
    std::vector<Kit_connector>       m_connect_place_connectors; // of the shape to place, read when it is chosen
    std::string                      m_connect_target_connector;
    std::string                      m_connect_moved_connector;
    std::string                      m_connect_place_connector;
    std::string                      m_connect_target_label;
    std::string                      m_connect_moved_label;
    const Scene_root*                m_connect_scene{nullptr};
    int                              m_connect_twist_steps{0};
    float                            m_connect_gap{0.0f};
    bool                             m_connect_selection_changed{true};
    std::string                      m_connect_message;
    bool                             m_connect_failed{false};
    std::size_t                      m_detach_node_count{0}; // selected kit pieces and floor layouts of the scene
    std::string                      m_detach_message;

    // The Group section: the new group's name, the copy's offset (the group's width by default) and the last result.
    std::string                            m_group_name{"Group"};
    glm::vec3                              m_duplicate_offset{1.0f, 0.0f, 0.0f};
    std::weak_ptr<erhe::scene::Node>       m_duplicate_source;
    bool                                   m_group_selection_changed{true};
    std::string                            m_group_message;
    bool                                   m_group_failed{false};

    // Spatial query results; the scratch vectors keep their capacity between presses.
    std::string                                     m_support_summary;
    std::vector<std::weak_ptr<erhe::scene::Mesh>>   m_unsupported;
    std::string                                     m_region_summary;
    std::vector<std::weak_ptr<erhe::scene::Mesh>>   m_region_meshes;
    std::string                                     m_height_map_summary;
    std::string                                     m_height_map_text;
    std::string                                     m_island_text;
    std::string                                     m_overlap_summary;
    std::vector<Spatial_query_row>                  m_overlap_rows;
    std::string                                     m_check_summary;
    std::vector<Spatial_query_row>                  m_check_rows;
    std::string                                     m_exclude_tags_text; // comma separated, as the Properties window's Tags row
    std::vector<std::string>                        m_exclude_tags;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_query_meshes;
    std::vector<Support_result>                     m_support_results;
    Height_map                                      m_height_map;
    Overlap_report                                  m_overlap_report;
    Level_check                                     m_level_check;

    erhe::message_bus::Subscription<Selection_message>             m_selection_subscription;
    erhe::message_bus::Subscription<Active_item_changed_message>   m_active_item_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Kit_values_changed_message>    m_kit_values_changed_subscription;
    erhe::message_bus::Subscription<Kit_piece_connectors_changed_message> m_kit_piece_connectors_changed_subscription;
};

} // namespace editor
