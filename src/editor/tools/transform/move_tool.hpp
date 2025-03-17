#pragma once

#include "tools/tool.hpp"
#include "tools/transform/subtool.hpp"

#include <string_view>

namespace editor {

class Icon_set;
class Transform_tool;

enum class Handle : unsigned int;

class Move_tool : public Subtool
{
public:
    static constexpr int c_priority{1};

    Move_tool(Editor_context& editor_context, Icon_set& icon_set, Tools& tools);
    ~Move_tool() noexcept override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implemennts Subtool
    void imgui (Property_editor& property_editor)                      override;
    auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool override;
    auto update(Scene_view* scene_view) -> bool                         override;

private:
    void update(const glm::vec3 drag_position);

    [[nodiscard]] auto snap(const glm::vec3 translation) const -> glm::vec3;

    int m_translate_snap_index{2};
};

} // namespace editor
