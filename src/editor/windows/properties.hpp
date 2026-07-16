#pragma once

#include "app_message.hpp"
#include "windows/property_editor.hpp"

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
    class Layout_item;
    class Light;
    class Mesh;
    class Node;
    class Scene;
    class Skin;
}
namespace erhe::geometry {
    class Geometry;
}
namespace erhe::physics {
    class Collision_filter;
    class Physics_joint_settings;
    class Physics_material;
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

private:

    // The items whose properties to show: { target } when pinned, else the
    // global selection. Reuses m_target_items scratch for the pinned case.
    [[nodiscard]] auto effective_items() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;
    // Draws the target-item selector row (item_reference_imgui, any type) plus
    // a "pinned" indicator. Bound to m_target.
    void target_selector_imgui();


    void animation_properties         (const std::shared_ptr<erhe::scene::Animation>& animation);
    void scene_properties             (erhe::scene::Scene& scene);
    void camera_properties            (erhe::scene::Camera& camera);
    void light_properties             (erhe::scene::Light& light);
    void layout_properties            (erhe::scene::Layout& layout);
    void layout_item_properties       (erhe::scene::Layout_item& layout_item);
    void texture_properties           (const std::shared_ptr<erhe::graphics::Texture>& texture);
    void geometry_properties          (erhe::geometry::Geometry& geometry);
    void buffer_mesh_properties       (const char* label, const erhe::primitive::Buffer_mesh* buffer_mesh);
    void primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace);
    void shape_properties             (const char* label, erhe::primitive::Primitive_shape* shape);
    void mesh_properties              (erhe::scene::Mesh& mesh);
    void skin_properties              (erhe::scene::Skin& skin);
    void material_properties          (const std::vector<std::shared_ptr<erhe::Item_base>>& items);
    void rendertarget_properties      (Rendertarget_mesh& rendertarget);
    void brush_properties             (const std::shared_ptr<Brush>& brush);
    void brush_placement_properties   (Brush_placement& brush_placement);
    void geometry_graph_mesh_properties(Geometry_graph_mesh& geometry_graph_mesh);
    void node_physics_properties      (Node_physics& node_physics);
    void node_joint_properties        (Node_joint& node_joint);
    void physics_material_properties  (const std::shared_ptr<erhe::physics::Physics_material>& physics_material);
    void collision_filter_properties  (const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter);
    void physics_joint_settings_properties(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings);
    void item_flags                   (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties              (const std::shared_ptr<erhe::Item_base>& item);

    void end_material_inspect();

    // Scene-hosted references (see CLAUDE.md "Scene-hosted references in
    // editor parts"): drop the pinned target and the material-edit latch
    // when their host scene closes - the window's own strong references
    // would otherwise keep the closed scene's items alive (a weak_ptr
    // target alone does not expire while m_target_items pins the item).
    void on_close_scene(erhe::Item_host* closing_host);

    App_context& m_context;

    erhe::message_bus::Subscription<Close_scene_message> m_close_scene_subscription;

    // Issue #252: the explicit pinned target (weak_ptr so a deleted item
    // reverts the window to selection mode), and the reused single-element
    // list handed to the property renderers when pinned.
    std::weak_ptr<erhe::Item_base>                m_target;
    std::vector<std::shared_ptr<erhe::Item_base>> m_target_items;

    Editor_state                               m_material_state{Editor_state::clean};
    std::shared_ptr<erhe::primitive::Material> m_inspected_material;
    erhe::primitive::Material_data             m_inspected_material_initial_state;

    std::vector<std::string> m_vertex_stream_labels;
    std::vector<std::string> m_primitive_labels;
    std::vector<std::string> m_rt_primitive_labels;
    std::vector<std::string> m_ngon_labels;

    // Reused scratch for the mesh-primitive material picker (cleared + refilled each use).
    std::vector<std::shared_ptr<erhe::Item_base>> m_material_candidates;
};

}
