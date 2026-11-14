#include "transform/point_set_transform.hpp"

#include "tools/point_set_tool.hpp"
#include "transform/transform_tool.hpp"

#include <cmath>
#include <optional>

namespace editor {

namespace {

[[nodiscard]] auto is_nontrivial_delta(const glm::mat4& world_delta) -> bool
{
    const glm::mat4 identity{1.0f};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(world_delta[column][row] - identity[column][row]) > 1e-6f) {
                return true;
            }
        }
    }
    return false;
}

} // anonymous namespace

auto Point_set_transform::update_anchor(IPoint_set& point_set, Transform_tool_shared& shared) -> bool
{
    glm::vec3   centroid{0.0f};
    std::size_t count = 0;
    point_set.get_point_world_positions(m_positions);
    for (std::size_t i = 0, end = m_positions.size(); i < end; ++i) {
        if (m_positions[i].has_value() && point_set.is_point_selected(i)) {
            centroid += m_positions[i].value();
            ++count;
        }
    }
    if (count == 0) {
        return false;
    }
    centroid /= static_cast<float>(count);
    shared.world_from_anchor_initial_state.set_trs(centroid, point_set.get_point_frame_rotation(), glm::vec3{1.0f});
    shared.entries.clear();
    shared.component_mode = true;
    shared.apply_reference_frame();
    return true;
}

void Point_set_transform::begin(IPoint_set& point_set)
{
    m_point_set = point_set.begin_point_edit() ? &point_set : nullptr;
}

void Point_set_transform::apply(Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (m_point_set == nullptr) {
        return;
    }
    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    // A click without motion restores the snapshot exactly instead of round-tripping it through the delta.
    m_point_set->apply_point_edit(is_nontrivial_delta(world_delta) ? std::optional<glm::mat4>{world_delta} : std::nullopt);
}

void Point_set_transform::commit()
{
    if (m_point_set == nullptr) {
        return;
    }
    IPoint_set* const point_set = m_point_set;
    m_point_set = nullptr;
    point_set->commit_point_edit();
}

void Point_set_transform::cancel()
{
    if (m_point_set == nullptr) {
        return;
    }
    IPoint_set* const point_set = m_point_set;
    m_point_set = nullptr;
    point_set->cancel_point_edit();
}

}
