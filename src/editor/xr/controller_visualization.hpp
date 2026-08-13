#pragma once

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::xr {
    class Xr_action_pose;
    class Xr_actions;
    class Xr_session;
}
namespace erhe::scene {
    class Animation;
    class Mesh;
    class Node;
}

namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

class App_context;
class Content_library_node;
class Scene_root;

// Visualizes the XR controllers: one node per hand. Each hand shows the
// runtime-provided controller model (XR_FB_render_model GLB, following the
// grip pose - the model origin the spec defines) once load_render_models()
// has fetched it, and a torus placeholder following the aim pose until
// then / when no model is available.
class Controller_visualization
{
public:
    Controller_visualization(
        erhe::scene::Node*                 view_root,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Scene_root&                        scene_root
    );

    // Fetch and build the runtime controller models, one attempt per hand.
    // Requires a running Xr_session and an active frame command buffer
    // (App_context::current_command_buffer). Hands whose model is
    // unavailable keep the torus placeholder.
    void load_render_models(App_context& context, erhe::xr::Xr_session& xr_session);

    // Per-frame pose update for one hand; camera_offset is added to the
    // pose position. The hand's node hides when the pose it follows (grip
    // for a render model, aim for the torus) is missing or untracked.
    void update_hand(
        bool                            right_hand,
        const erhe::xr::Xr_action_pose* grip_pose,
        const erhe::xr::Xr_action_pose* aim_pose,
        const glm::vec3&                camera_offset
    );

    // Per-frame control-state update for one hand: poses the render model's
    // control joints (buttons, triggers, thumbstick) from the controller
    // input state. No-op until the hand has a render model.
    void update_hand_controls(bool right_hand, const erhe::xr::Xr_actions* actions);

private:
    // One control joint driven along a neutral -> actuated pose pair,
    // both sampled from the render model's "All Animations" timeline.
    class Control_drive
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        glm::vec3                          neutral_translation {0.0f};
        glm::quat                          neutral_rotation    {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3                          actuated_translation{0.0f};
        glm::quat                          actuated_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    };

    // The thumbstick joint tilts in two axes; the four cardinal deflection
    // poses are stored as rotation vectors (axis * angle) relative to the
    // neutral rotation and blended by the thumbstick x/y input.
    class Thumbstick_drive
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        glm::vec3                          translation     {0.0f};
        glm::quat                          neutral_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3                          tilt_x_positive {0.0f};
        glm::vec3                          tilt_x_negative {0.0f};
        glm::vec3                          tilt_y_positive {0.0f};
        glm::vec3                          tilt_y_negative {0.0f};
    };

    class Hand
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;             // driven by grip or aim pose
        std::shared_ptr<erhe::scene::Mesh> placeholder_mesh; // torus, detached when a render model loads
        bool                               has_render_model{false};
        Control_drive                      primary_button;   // A (right) / X (left)
        Control_drive                      secondary_button; // B (right) / Y (left)
        Control_drive                      trigger;
        Control_drive                      grip_trigger;
        Thumbstick_drive                   thumbstick;
    };

    void load_render_model   (App_context& context, erhe::xr::Xr_session& xr_session, bool right_hand);
    void setup_control_drives(Hand& hand, erhe::scene::Animation& animation, bool right_hand);

    [[nodiscard]] auto get_hand(bool right_hand) -> Hand&;

    erhe::scene_renderer::Mesh_memory&    m_mesh_memory;
    std::shared_ptr<Content_library_node> m_material_library;
    uint64_t                              m_content_layer_id{0};
    Hand                                  m_left_hand;
    Hand                                  m_right_hand;
};

}
