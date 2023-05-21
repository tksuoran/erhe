#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/scene/transform.hpp"

#include <vector>
#include <memory>

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Node;
    class Item;
}

namespace editor
{

class Node_physics;
class Rendertarget_mesh;

class Properties
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Properties"};
    static constexpr std::string_view c_title{"Properties"};
    static constexpr uint32_t         c_type_hash{compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {})};

    Properties ();
    ~Properties() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void camera_properties      (erhe::scene::Camera& camera) const;
    void light_properties       (erhe::scene::Light& light) const;
    void mesh_properties        (erhe::scene::Mesh& mesh) const;
    void transform_properties   (erhe::scene::Node& node);
    void rendertarget_properties(Rendertarget_mesh& rendertarget) const;
    void node_physics_properties(Node_physics& node_physics) const;
    void item_flags             (const std::shared_ptr<erhe::scene::Item>& item);
    void item_properties        (const std::shared_ptr<erhe::scene::Item>& item);
};

extern Properties* g_properties;

} // namespace editor
