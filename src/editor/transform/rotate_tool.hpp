#pragma once

#include "transform/subtool.hpp"
#include "tools/tool.hpp"

#include <glm/gtc/quaternion.hpp>

#include <string_view>

namespace editor {

class Icon_set;
class Transform_tool;

enum class Handle : unsigned int;

class Rotate_tool : public Subtool
{
public:
    static constexpr int c_priority{1};

    Rotate_tool(App_context& app_context, Icon_set& icon_set, Tools& tools);
    ~Rotate_tool() noexcept override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implemennts Subtool
    void imgui (Property_editor& property_editor)                       override;
    auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool override;
    auto update(Scene_view* scene_view) -> bool                         override;

    // Public API (mostly for Transform_tool
    void render(const Render_context& context);

private:
    auto update_circle_around(Scene_view* scene_view) -> bool;
    auto update_parallel     (Scene_view* scene_view) -> bool;
    void update_final        ();

    auto update_arcball(Scene_view* scene_view) -> bool;

    [[nodiscard]] auto snap          (float angle_radians) const -> float;
    [[nodiscard]] auto initial_twist () const -> float;

    int                      m_rotate_snap_index{2};
    glm::vec3                m_normal              {0.0f}; // also rotation axis
    glm::vec3                m_reference_direction {0.0f};
    glm::vec3                m_axis_side           {0.0f}; // active-space plane side at drag start (axis-absolute protractor frame)
    bool                     m_view_mode           {false}; // rotating around the viewing axis (outer ring)
    bool                     m_free_mode           {false}; // arcball rotation (inside the rotate sphere)
    // Incremental screen-plane trackball state (see update_arcball()).
    glm::vec3                m_arcball_prev        {0.0f};  // previous pointer point in the view plane, relative to the center
    bool                     m_arcball_prev_valid  {false};
    glm::quat                m_arcball_rotation    {1.0f, 0.0f, 0.0f, 0.0f}; // accumulated drag rotation
    float                    m_arcball_radius      {1.0f};
    glm::vec3                m_center_of_rotation  {0.0f};
    std::optional<glm::vec3> m_intersection        {};
    float                    m_start_rotation_angle{0.0f};
    float                    m_current_angle       {0.0f};
};

}
