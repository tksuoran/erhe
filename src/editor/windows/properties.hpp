#pragma once

#include "erhe/imgui/imgui_window.hpp"

#include "erhe/scene/transform.hpp"

#include <vector>
#include <memory>

namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Item;
    class Skin;
}

namespace editor
{

class Editor_context;
class Node_physics;
class Rendertarget_mesh;

class Properties
    : public erhe::imgui::Imgui_window
{
public:
    Properties(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void animation_properties   (erhe::scene::Animation& animation) const;
    void camera_properties      (erhe::scene::Camera& camera) const;
    void light_properties       (erhe::scene::Light& light) const;
    void mesh_properties        (erhe::scene::Mesh& mesh) const;
    void skin_properties        (erhe::scene::Skin& skin) const;
    void material_properties    ();
    void rendertarget_properties(Rendertarget_mesh& rendertarget) const;
    void node_physics_properties(Node_physics& node_physics) const;
    void item_flags             (const std::shared_ptr<erhe::scene::Item>& item);
    void item_properties        (const std::shared_ptr<erhe::scene::Item>& item);

    Editor_context& m_context;
};

} // namespace editor
