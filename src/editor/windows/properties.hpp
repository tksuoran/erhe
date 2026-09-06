#pragma once

#include "app_message.hpp"
#include "windows/property_editor.hpp"
#include "windows/dependency_property_rows.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace GEO {
    class Mesh;
}
namespace erhe {
    class Item_base;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Animation;
    class Camera;
    class Layout;
    class Light;
    class Mesh;
    class Xformable; using Node = Xformable;
    class Scene;
    class Skin;
}
namespace erhe::geometry {
    class Geometry;
}
namespace erhe::physics {
    class Collision_filter;
    class Physics_joint_settings;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive_raytrace;
    class Primitive_shape;
}

namespace editor {

class Brush;
class Brush_placement;
class App_context;
class App_message_bus;
class Geometry_graph_mesh;
class Node_joint;
class Node_physics;
class Rendertarget_mesh;

class Properties : public erhe::imgui::Imgui_window, public Property_editor
{
public:
    // title / ini_label default to the primary singleton's values; the
    // Editor_windows manager passes a unique title + empty ini_label for the
    // extra "Open Properties" instances (issue #252).
    Properties(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus,
        std::string_view             title     = "Properties",
        std::string_view             ini_label = "properties"
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

    // Issue #252: pin this Properties window to an explicit target item. When
    // set, the window shows only the target; when unset (or the target was
    // deleted - stored as a weak_ptr) it falls back to the global selection
    // (the original behavior). Used by the "Open Properties" context menu and
    // the target selector row at the top of the window.
    void set_target(const std::shared_ptr<erhe::Item_base>& item);

    // Cached reference, for the MCP get_editor_references query
    // (doc/import-undo-reference-clearing.md).
    [[nodiscard]] auto get_target            () const -> std::shared_ptr<erhe::Item_base>;
    [[nodiscard]] auto get_target_items      () const -> const std::vector<std::shared_ptr<erhe::Item_base>>&;
    [[nodiscard]] auto get_inspected_material() const -> const std::shared_ptr<erhe::primitive::Material>&;
private:

    // The items whose properties to show: { target } when pinned, else the
    // global selection. Reuses m_target_items scratch for the pinned case.
    [[nodiscard]] auto effective_items() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;
    // Draws the target-item selector row (item_reference_imgui, any type) plus
    // a "pinned" indicator. Bound to m_target.
    void target_selector_imgui();

    // How a multi-selection is shown (the selector next to Pin): every
    // item on its own with its diagnostics and attachments, or one section
    // per property owner type editing the items together (mixed values
    // shown per component, one operation per edit). A single item always
    // draws the individual form.
    enum class Selection_mode : unsigned int {
        individual = 0,
        combined   = 1
    };


    void animation_properties         (const std::shared_ptr<erhe::scene::Animation>& animation);
    void scene_properties             (erhe::scene::Scene& scene);
    void light_properties             (erhe::scene::Light& light);
    void layout_properties            (erhe::scene::Layout& layout);
    void texture_properties           (const std::shared_ptr<erhe::graphics::Texture>& texture);
    void geometry_properties          (erhe::geometry::Geometry& geometry);
    void buffer_mesh_properties       (const char* label, const erhe::primitive::Buffer_mesh* buffer_mesh);
    void primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace);
    void shape_properties             (const char* label, erhe::primitive::Primitive_shape* shape);
    void mesh_properties              (erhe::scene::Mesh& mesh);
    void skin_properties              (erhe::scene::Skin& skin);
    void material_properties          (const std::vector<std::shared_ptr<erhe::Item_base>>& items);
    void brush_placement_properties   (Brush_placement& brush_placement);
    void node_physics_properties      (Node_physics& node_physics);
    void node_joint_properties        (Node_joint& node_joint);
    // Generic rows for the item's registered properties
    // (doc/property-system.md D12), inside the item's group.
    void dependency_properties        (const std::shared_ptr<erhe::Item_base>& item);
    void collision_filter_properties  (const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter);
    void physics_joint_settings_properties(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings);
    void item_flags                   (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties              (const std::shared_ptr<erhe::Item_base>& item);
    void item_diagnostics             (const std::shared_ptr<erhe::Item_base>& item);


    // Scene-hosted references (see AGENTS.md "Scene-hosted references in
    // editor parts"): drop the pinned target and the material-edit latch
    // when their host scene closes - the window's own strong references
    // would otherwise keep the closed scene's items alive (a weak_ptr
    // target alone does not expire while m_target_items pins the item).
    void on_close_scene(erhe::Item_host* closing_host);
    // Content removed without a scene closing (undo of a glTF import).
    void on_items_removed(const Removed_items& removed);

    App_context& m_context;

    Dependency_property_rows m_dependency_rows;

    Selection_mode                     m_selection_mode{Selection_mode::combined};

    erhe::message_bus::Subscription<Close_scene_message>   m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message> m_items_removed_subscription;

    // Issue #252: the explicit pinned target (weak_ptr so a deleted item
    // reverts the window to selection mode), and the reused single-element
    // list handed to the property renderers when pinned.
    std::weak_ptr<erhe::Item_base>                m_target;
    std::vector<std::shared_ptr<erhe::Item_base>> m_target_items;

    // Multi-selection: the selected items partitioned by property owner
    // type, one row set per group (scratch, cleared each frame with the
    // capacity kept).
    std::vector<std::vector<std::shared_ptr<erhe::Item_base>>> m_type_groups;

    // The material whose preview the window renders (the MCP
    // get_editor_references query reports it).
    std::shared_ptr<erhe::primitive::Material> m_inspected_material;

    std::vector<std::string> m_vertex_stream_labels;
    std::vector<std::string> m_primitive_labels;
    std::vector<std::string> m_rt_primitive_labels;
    std::vector<std::string> m_ngon_labels;

    // Reused scratch for the mesh-primitive material picker (cleared + refilled each use).
};

}
