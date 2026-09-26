#pragma once

#include "tools/joint_visualization.hpp"
#include "transform/ik_solver.hpp"

#include "config/generated/debug_visualizations_settings.hpp"
#include "config/generated/debug_visualizations_style.hpp"
#include "config/generated/joint_constraint_filter.hpp"

#include "erhe_physics/joint_limits.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Ik_drag;
class Joint;
class Joint_entry;
class Render_context;
class Scene_root;

// Joint constraint visualization (doc/editor/tools.md "Debug_visualizations"):
// gathers the physics joints and IK-constrained bones of a scene view's scene,
// applies the view's Joint Constraints filter, and draws them with the pure
// builders of tools/joint_visualization.hpp. Owned by Debug_visualizations.

// A physics joint as drawn: its anchor frames in world space, the limits the
// backend enforces and the current joint coordinates. Also what the MCP
// get_joint_constraint_state tool reports.
class Physics_joint_state
{
public:
    const Joint*                       joint        {nullptr};
    bool                               live         {false};   // a live constraint exists
    erhe::physics::Transform           frame_a      {};
    erhe::physics::Transform           frame_b      {};
    const erhe::scene::Node*           frame_node_0 {nullptr};
    const erhe::scene::Node*           frame_node_1 {nullptr};
    const erhe::scene::Node*           body_node_a  {nullptr}; // live only
    const erhe::scene::Node*           body_node_b  {nullptr}; // live only; null = world
    erhe::physics::Joint_limit_shape   shape        {};
    erhe::physics::Joint_coordinates   coordinates  {};
    erhe::physics::Joint_range_check   range_check  {};
};

// Joint coordinate tolerances of the in-range check: 1 mm, 1 degree.
inline constexpr float c_joint_linear_tolerance  = 1.0e-3f;
inline constexpr float c_joint_angular_tolerance = 0.0174533f;

// False when the joint has no first frame node (nothing to draw).
[[nodiscard]] auto get_physics_joint_state(const Joint_entry& entry, Physics_joint_state& state) -> bool;

// An IK-constrained bone as drawn.
class Ik_limit_state
{
public:
    const erhe::scene::Node* node                 {nullptr};
    Ik_joint_constraint      constraint           {};
    glm::quat                local_rotation       {1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat                pinned_local_rotation{1.0f, 0.0f, 0.0f, 0.0f}; // pins locked axes: drag start during a drag, else current
    glm::quat                world_from_rest      {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                head                 {0.0f};
    float                    length               {0.0f};
    float                    twist_sign           {1.0f};
    Ik_rotation_components   current              {};
    bool                     within_limits        {true};
    bool                     from_drag            {false}; // the constraint is the running IK drag's
};

// Resolves the node's IK constraint - the running drag's resolved one when
// ik_drag has the node in its chain, else resolve_ik_constraint() with the
// twist axis from the node's Rig.tail. False when the node has no enabled
// constraint.
[[nodiscard]] auto get_ik_limit_state(const erhe::scene::Node& node, const Ik_drag* ik_drag, Ik_limit_state& state) -> bool;

class Joint_constraint_visualization
{
public:
    // Draws the joint constraints the settings select into the context's
    // x-ray line bucket, and records which ones it drew.
    void render(
        const Render_context&                context,
        const Debug_visualizations_settings& settings,
        const Debug_visualizations_style&    style
    );

    // Item ids of the Joint prims / bone nodes drawn by the last render()
    // (MCP get_joint_constraint_state).
    [[nodiscard]] auto get_drawn_physics_joints() const -> const std::vector<std::size_t>& { return m_drawn_physics_joints; }
    [[nodiscard]] auto get_drawn_ik_bones      () const -> const std::vector<std::size_t>& { return m_drawn_ik_bones; }

private:
    void physics_joints(const Render_context& context, Scene_root& scene_root, Joint_constraint_filter filter, const Joint_line_style& line_style, float joint_size);
    void ik_limits     (const Render_context& context, Scene_root& scene_root, Joint_constraint_filter filter, const Joint_line_style& line_style, float joint_size);
    void add_ik_bone   (const erhe::scene::Node& node, const Ik_drag* ik_drag, const Joint_line_style& line_style, float joint_size);
    void flush         (const Render_context& context);

    Joint_line_buffer                  m_lines;
    Ik_swing_boundary                  m_swing_boundary;
    std::vector<const erhe::scene::Node*> m_hovered_mesh_bones; // scratch of the hovered_mesh filter
    std::vector<std::size_t>           m_drawn_physics_joints;
    std::vector<std::size_t>           m_drawn_ik_bones;
};

} // namespace editor
