#pragma once

#include "tools/tool.hpp"
#include "tools/brushes/reference_frame.hpp"
#include "scene/scene_view.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_geometry/types.hpp"
#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>

namespace editor {

class Brush;
class Brush_tool;
class Content_library;
class App_message;
class App_message_bus;
class App_scenes;
class Icon_set;
class Item_tree;
class Operation_stack;
class Operations;
class Render_context;
class Tools;
class Headset_view;

class Brush_tool_preview_command : public erhe::commands::Command
{
public:
    Brush_tool_preview_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Brush_tool_insert_command : public erhe::commands::Command
{
public:
    Brush_tool_insert_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

class Brush_tool_pick_command : public erhe::commands::Command
{
public:
    Brush_tool_pick_command(erhe::commands::Commands& commands, App_context& context);

    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Brush_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Brush_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );

    // Implements Tool
    void tool_render           (const Render_context& context) override;
    void tool_properties       (erhe::imgui::Imgui_window& imgui_window) override;
    void handle_priority_update(int old_priority, int new_priority) override;

    // Commands
    auto try_insert_ready     () -> bool;
    auto try_insert           (Brush* brush = nullptr) -> bool;
    auto try_pick             () -> bool;
    void on_motion            ();
    void preview_drag_and_drop(std::shared_ptr<Brush> brush);

    [[nodiscard]] auto get_hover_brush() const -> std::shared_ptr<Brush>;

private:
    void on_message                        (App_message& app_message);
    void update_preview_mesh               ();
    void do_insert_operation               (Brush& brush);
    void add_preview_mesh                  (Brush& brush);
    void remove_preview_mesh               ();
    void update_preview_mesh_node_transform();

    [[nodiscard]] auto get_world_from_grid_hover_point() const -> glm::mat4;

    [[nodiscard]] auto update_hover_frame_from_mesh() -> bool;
    [[nodiscard]] auto update_hover_frame_from_grid() -> bool;
    [[nodiscard]] auto update_brush_frame          (Brush& brush) -> bool;
    [[nodiscard]] auto get_placement_facet  () const -> GEO::index_t;
    [[nodiscard]] auto get_placement_corner0() const -> GEO::index_t;

    Brush_tool_preview_command              m_preview_command;
    Brush_tool_insert_command               m_insert_command;
    Brush_tool_pick_command                 m_pick_command;
    erhe::commands::Float_threshold_command m_pick_using_float_input_command;

    ERHE_PROFILE_MUTEX(std::mutex,     m_brush_mutex);
    std::shared_ptr<Brush>             m_drag_and_drop_brush   {};
    float                              m_preview_hover_distance{0.001f};
    bool                               m_snap_to_hover_polygon {true};
    bool                               m_snap_to_grid          {true};
    bool                               m_scale_to_match        {true};
    bool                               m_use_matching_face     {true};
    bool                               m_use_selected_face     {false};
    bool                               m_debug_visualization   {true};
    bool                               m_show_receiver_frame   {true};
    bool                               m_show_brush_frame      {true};
    bool                               m_show_preview          {true};
    Hover_entry                        m_hover;
    std::shared_ptr<erhe::scene::Mesh> m_preview_mesh;
    std::shared_ptr<erhe::scene::Node> m_preview_node;
    bool                               m_with_physics   {true};
    float                              m_scale          {1.0f};
    float                              m_transform_scale{1.0f};
    int                                m_polygon_offset {0};
    int                                m_corner_offset  {0};
    std::optional<std::size_t>         m_selected_corner_count{};
    std::optional<GEO::index_t>        m_hover_facet_corner_count;
    std::optional<glm::mat4>           m_world_from_hover;
    std::optional<Reference_frame>     m_hover_frame;
    std::optional<Reference_frame>     m_brush_placement_frame;
    std::optional<glm::mat4>           m_hover_transform;
    std::optional<glm::mat4>           m_align_transform;
    std::unique_ptr<Item_tree>         m_brush_item_tree;
};

}
