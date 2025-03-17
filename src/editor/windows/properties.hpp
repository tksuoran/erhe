#pragma once

#include "windows/property_editor.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>

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

class Properties : public erhe::imgui::Imgui_window, public Property_editor
{
public:
    Properties(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context);

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:

    void animation_properties         (erhe::scene::Animation& animation);
    void camera_properties            (erhe::scene::Camera& camera);
    void light_properties             (erhe::scene::Light& light);
    void texture_properties           (const std::shared_ptr<erhe::graphics::Texture>& texture);
    void geometry_properties          (erhe::geometry::Geometry& geometry);
    void buffer_mesh_properties       (const char* label, const erhe::primitive::Buffer_mesh* buffer_mesh);
    void primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace);
    void shape_properties             (const char* label, erhe::primitive::Primitive_shape* shape);
    void mesh_properties              (erhe::scene::Mesh& mesh);
    void skin_properties              (erhe::scene::Skin& skin);
    void material_properties          ();
    void rendertarget_properties      (Rendertarget_mesh& rendertarget);
    void brush_placement_properties   (Brush_placement& brush_placement);
    void node_physics_properties      (Node_physics& node_physics);
    void item_flags                   (const std::shared_ptr<erhe::Item_base>& item);
    void item_properties              (const std::shared_ptr<erhe::Item_base>& item);

    Editor_context& m_context;
};

} // namespace editor
