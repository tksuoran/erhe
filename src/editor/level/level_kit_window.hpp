#pragma once

#include "app_message.hpp"
#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape.hpp"
#include "level/level_queries.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

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
    void shapes_imgui           (const Scene_root& scene_root);
    void place_parameters_imgui (const Scene_root& scene_root);
    void placement_settings_imgui();
    void selection_imgui        (const std::shared_ptr<Scene_root>& scene_root);
    void select_shape           (const Kit_shape_definition& shape, const Scene_root& scene_root);
    void on_kit_values_changed  ();
    // Hands the Brush tool the kit brush of m_place_values and makes it the active tool in kit mode (D4.4).
    void queue_kit_brush        ();
    void update_place_hint      ();
    void queue_snap_selected    (const std::shared_ptr<Scene_root>& scene_root, Surface_orient orient);

    [[nodiscard]] auto find_selected_piece(const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Mesh>;
    [[nodiscard]] auto count_selected_meshes(const Scene_root& scene_root) const -> std::size_t;

    App_context& m_context;

    // The shape to place and its parameter values.
    const Kit_shape_definition* m_shape{nullptr};
    Kit_parameter_values        m_place_values;
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

    erhe::message_bus::Subscription<Selection_message>             m_selection_subscription;
    erhe::message_bus::Subscription<Active_item_changed_message>   m_active_item_subscription;
    erhe::message_bus::Subscription<Mesh_geometry_changed_message> m_mesh_geometry_changed_subscription;
    erhe::message_bus::Subscription<Kit_values_changed_message>    m_kit_values_changed_subscription;
};

} // namespace editor
