#pragma once

#include "tools/transform/subtool.hpp"
#include "tools/tool.hpp"

#include <string_view>

namespace editor {

class Icon_set;
class Transform_tool;

enum class Handle : unsigned int;

class Rotate_tool : public Subtool
{
public:
    static constexpr int c_priority{1};

    Rotate_tool(Editor_context& editor_context, Icon_set& icon_set, Tools& tools);
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

    [[nodiscard]] auto snap(float angle_radians) const -> float;

    int                      m_rotate_snap_index{2};
    glm::vec3                m_normal              {0.0f}; // also rotation axis
    glm::vec3                m_reference_direction {0.0f};
    glm::vec3                m_center_of_rotation  {0.0f};
    std::optional<glm::vec3> m_intersection        {};
    float                    m_start_rotation_angle{0.0f};
    float                    m_current_angle       {0.0f};
};

} // namespace editor
