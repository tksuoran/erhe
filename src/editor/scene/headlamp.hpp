#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene {
    class Light;
    class Xformable; using Node = Xformable;
}

namespace editor {

class App_context;
class Scene_root;
struct Removed_items;

// A session-only spot light carried by a camera (doc/editor/tools.md
// "Headlamp"): the headset's root camera under OpenXR, the fly camera's
// camera otherwise. Main thread, prepare phase (ImGui edits are queued).
class Headlamp
{
public:
    explicit Headlamp(App_context& context);

    void enable ();
    void disable();
    // Parents the light to camera_node (in its scene) at parent_from_light,
    // first in the scene's light layer; does nothing while disabled.
    void attach (const std::shared_ptr<erhe::scene::Node>& camera_node, const glm::mat4& parent_from_light);
    void detach ();
    void set_intensity(float intensity);
    void set_spot_angles(float inner_spot_angle, float outer_spot_angle);
    // Scene-hosted reference rules (doc/editor/coding_rules.md): the owner forwards these.
    void on_close_scene  (const Scene_root& scene_root);
    void on_items_removed(const Removed_items& removed);

    [[nodiscard]] auto is_enabled      () const -> bool { return m_enabled; }
    [[nodiscard]] auto get_cast_shadow () const -> bool { return m_cast_shadow; }
    [[nodiscard]] auto get_scene_root  () const -> Scene_root*;

    // Add / Enable / Disable button, intensity and shadow controls.
    void imgui(const char* tooltip);

private:
    void reattach();

    App_context&                        m_context;
    std::shared_ptr<erhe::scene::Light> m_light;
    std::weak_ptr<erhe::scene::Node>    m_camera_node;
    glm::mat4                           m_parent_from_light{1.0f};
    bool                                m_enabled          {false};
    float                               m_intensity        {500.0f};
    // Full cone angles in radians (the shader takes cos(angle / 2)).
    float                               m_inner_spot_angle {0.25f};
    float                               m_outer_spot_angle {1.50f};
    bool                                m_cast_shadow      {true};
};

}
