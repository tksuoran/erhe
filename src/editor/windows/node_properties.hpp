#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Node;
}

namespace editor
{

class Scene_manager;
class Selection_tool;

class Node_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Node_properties"};
    static constexpr std::string_view c_title{"Node Properties"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Node_properties ();
    ~Node_properties() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    void camera_properties   (erhe::scene::Camera& camera) const;
    void light_properties    (erhe::scene::Light& light) const;
    void mesh_properties     (erhe::scene::Mesh& mesh) const;
    void transform_properties(erhe::scene::Node& node) const;

    Scene_manager*  m_scene_manager {nullptr};
    Selection_tool* m_selection_tool{nullptr};
};

} // namespace editor
