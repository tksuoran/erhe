#pragma once

#include "tools/tool.hpp"
#include "transform/subtool.hpp"

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <string_view>

namespace editor {

class Icon_set;
class Property_editor;
class Transform_tool;

enum class Handle : unsigned int;

class Scale_tool : public Subtool
{
public:
    static constexpr int c_priority{1};

    Scale_tool(App_context& app_context, Icon_set& icon_set, Tools& tools);

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implemennts Subtool
    //void imgui (Property_editor& property_editor)                       override;
    auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool override;
    auto update(Scene_view* scene_view) -> bool                         override;

private:
    void update              (glm::vec3 drag_position);
    auto update_box          (Scene_view* scene_view) -> bool;
    auto box_axis_projection (Scene_view* scene_view) const -> std::optional<float>;
    auto is_selection_singular() const -> bool;
    auto apply_box_per_node  (float pivot, float old_size, float new_size) -> bool;

    bool             m_box_mode             {false};
    glm::mat4        m_box_frame            {1.0f};
    erhe::math::Aabb m_box_aabb             {};
    int              m_box_axis             {0};
    bool             m_box_positive         {true};
    float            m_box_initial_proj     {0.0f};
    bool             m_box_need_initial_proj{false};
};

}
