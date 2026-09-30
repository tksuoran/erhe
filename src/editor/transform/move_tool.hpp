#pragma once

#include "tools/screen_snap.hpp"
#include "tools/tool.hpp"
#include "transform/subtool.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace editor {

class Icon_set;
class Transform_tool;

enum class Handle : unsigned int;

class Move_tool : public Subtool
{
public:
    static constexpr int c_priority{1};

    Move_tool(App_context& app_context, Icon_set& icon_set, Tools& tools);
    ~Move_tool() noexcept override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implemennts Subtool
    void imgui (Property_editor& property_editor)                      override;
    auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool override;
    auto update(Scene_view* scene_view) -> bool                         override;

private:
    void update(glm::vec3 drag_position);

    [[nodiscard]] auto snap(glm::vec3 translation) const -> glm::vec3;

    // The move mode's vertex / edge snap (doc/editor/transform.md "Snap to
    // vertices / edges"): the anchor translation that lands the anchor on
    // the nearest vertex or edge of the facet under the pointer, constrained
    // to the drag's axis or plane; nullopt when the snap is off or nothing
    // lies within the radius.
    [[nodiscard]] auto get_component_snap_translation(Scene_view& scene_view) -> std::optional<glm::vec3>;

    int                       m_translate_snap_index{2};
    Screen_snap               m_screen_snap{};
    std::vector<std::uint8_t> m_snap_excluded_vertices{}; // per vertex of the hovered geometry (cleared at use, capacity kept)
};

}
