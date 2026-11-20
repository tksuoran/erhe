#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace editor {

class IPoint_set;
class Transform_tool_shared;
enum class Component_edit_origin : unsigned int;

// Drives the transform gizmo from the selected points of an IPoint_set
// (doc/editor/point_set_editing.md): update_anchor() places the gizmo at the
// hovered point, else at the selection's centroid, each idle frame; begin() / apply() / commit() /
// cancel() hand one gesture's world-space delta to the point set.
class Point_set_transform
{
public:
    auto update_anchor(IPoint_set& point_set, Transform_tool_shared& shared) -> bool;
    void begin        (IPoint_set& point_set, Transform_tool_shared& shared, Component_edit_origin origin);
    void apply        (Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor);
    void commit       ();
    void cancel       ();

    [[nodiscard]] auto is_active    () const -> bool { return m_point_set != nullptr; }
    [[nodiscard]] auto get_point_set() const -> IPoint_set* { return m_point_set; }

private:
    IPoint_set*                           m_point_set{nullptr}; // set from begin() to commit() / cancel()
    std::vector<std::optional<glm::vec3>> m_positions;          // scratch of update_anchor() (capacity kept)
    bool                                  m_pointer_drag{false}; // begin() to commit() / cancel()
    bool                                  m_moved       {false}; // a step of the gesture moved the points
};

}
