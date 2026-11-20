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
    const std::optional<std::size_t> hot = point_set.get_hot_point();
    if (hot.has_value() && (hot.value() < m_positions.size()) && m_positions[hot.value()].has_value()) {
        centroid = m_positions[hot.value()].value();
    } else {
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
    }
    shared.world_from_anchor_initial_state.set_trs(centroid, point_set.get_point_frame_rotation(), glm::vec3{1.0f});
    shared.entries.clear();
    shared.component_mode = true;
    shared.apply_reference_frame();
    return true;
}

void Point_set_transform::begin(IPoint_set& point_set, Transform_tool_shared& shared, const Component_edit_origin origin)
{
    m_pointer_drag = (origin == Component_edit_origin::pointer_drag);
    m_moved        = false;
    if (m_pointer_drag) {
        point_set.begin_pointer_drag();
    } else if (point_set.get_hot_point().has_value()) {
        // A value edit acts on the selection about its centroid.
        point_set.clear_hot_point();
        static_cast<void>(update_anchor(point_set, shared));
    }
    m_point_set = point_set.begin_point_edit() ? &point_set : nullptr;
}

void Point_set_transform::apply(Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (m_point_set == nullptr) {
        return;
    }
    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    // A click without motion restores the snapshot exactly instead of round-tripping it through the delta.
    const bool nontrivial = is_nontrivial_delta(world_delta);
    m_moved = m_moved || nontrivial;
    m_point_set->apply_point_edit(nontrivial ? std::optional<glm::mat4>{world_delta} : std::nullopt);
}

void Point_set_transform::commit()
{
    if (m_point_set == nullptr) {
        return;
    }
    IPoint_set* const point_set = m_point_set;
    m_point_set = nullptr;
    point_set->commit_point_edit();
    if (m_pointer_drag) {
        point_set->end_pointer_drag(m_moved ? Point_drag_result::moved : Point_drag_result::unmoved);
    }
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
