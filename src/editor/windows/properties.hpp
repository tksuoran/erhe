#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include "erhe_scene/transform.hpp"

#include <vector>
#include <memory>

namespace erhe {
    class Item_base;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Skin;
}
namespace erhe::geometry {
    class Geometry;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Primitive_raytrace;
    class Primitive_shape;
}

namespace editor {

class Brush_placement;
class Editor_context;
class Node_physics;
class Rendertarget_mesh;

class Properties : public erhe::imgui::Imgui_window
{
public:
    Properties(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context);

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void animation_properties         (erhe::scene::Animation& animation) const;
    void camera_properties            (erhe::scene::Camera& camera) const;
    void light_properties             (erhe::scene::Light& light) const;
    void geometry_properties          (const erhe::geometry::Geometry* geometry) const;
    void buffer_mesh_properties       (const char* label, const erhe::primitive::Buffer_mesh* buffer_mesh) const;
    void primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace) const;
    void shape_properties             (const char* label, erhe::primitive::Primitive_shape* shape) const;
    void mesh_properties              (erhe::scene::Mesh& mesh) const;
    void skin_properties              (erhe::scene::Skin& skin) const;
    void material_properties          ();
    void rendertarget_properties      (Rendertarget_mesh& rendertarget) const;
    void brush_placement_properties   (Brush_placement& brush_placement) const;
    void node_physics_properties      (Node_physics& node_physics) const;
    void item_flags                   (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties              (const std::shared_ptr<erhe::Item_base>& item);

    Editor_context& m_context;
};

} // namespace editor
