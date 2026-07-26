#include "transform/lattice_point_transform.hpp"

#include "app_context.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/nodes/lattice_node.hpp"
#include "operations/operation_stack.hpp"
#include "tools/lattice_tool.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_scene/node.hpp"

#include <cmath>

namespace editor {

namespace {

[[nodiscard]] auto control_point_rest_position(const Lattice_node& lattice, const glm::vec3& cage_min, const glm::vec3& cage_max, const glm::ivec3 point) -> glm::vec3
{
    const glm::ivec3 divisions = lattice.get_divisions();
    return
        cage_min +
        (cage_max - cage_min) * glm::vec3{
            static_cast<float>(point.x) / static_cast<float>(divisions.x),
            static_cast<float>(point.y) / static_cast<float>(divisions.y),
            static_cast<float>(point.z) / static_cast<float>(divisions.z)
        };
}

// Inverse of the driver transform, for mapping a gizmo-moved control point
// position back into an offset. Falls back to identity for a degenerate
// (non-invertible) driver transform - the drag then edits pre-transform space.
[[nodiscard]] auto safe_inverse(const glm::mat4& transform) -> glm::mat4
{
    const float determinant = glm::determinant(transform);
    if (std::abs(determinant) < 1e-12f) {
        return glm::mat4{1.0f};
    }
    return glm::inverse(transform);
}

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

auto Lattice_point_transform::update_anchor(App_context& context, Transform_tool_shared& shared) -> bool
{
    Lattice_tool* lattice_tool = context.lattice_tool;
    if (lattice_tool == nullptr) {
        return false;
    }
    const Lattice_tool::Active_lattice& active = lattice_tool->update_active_lattice();
    if (!active.lattice || !active.bound_node) {
        return false;
    }
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!active.lattice->resolve_cage(cage_min, cage_max)) {
        return false;
    }
    const glm::ivec3 point  = active.lattice->get_selected_point();
    const glm::vec3  rest   = control_point_rest_position(*active.lattice, cage_min, cage_max, point);
    const glm::vec3  local  = glm::vec3{
        active.lattice->get_control_point_transform() * glm::vec4{rest + active.lattice->get_control_point_offset(point), 1.0f}
    };
    const glm::vec3  world  = glm::vec3{active.bound_node->world_from_node() * glm::vec4{local, 1.0f}};
    const glm::quat rotation = active.bound_node->world_from_node_transform().get_rotation();

    shared.world_from_anchor_initial_state.set_trs(world, rotation, glm::vec3{1.0f});
    shared.entries.clear();
    shared.component_mode = true;
    shared.apply_reference_frame();
    return true;
}

void Lattice_point_transform::begin(App_context& context)
{
    m_active = false;
    Lattice_tool* lattice_tool = context.lattice_tool;
    if (lattice_tool == nullptr) {
        return;
    }
    const Lattice_tool::Active_lattice& active = lattice_tool->get_active_lattice();
    if (!active.lattice || !active.bound_node) {
        return;
    }
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!active.lattice->resolve_cage(cage_min, cage_max)) {
        return;
    }
    m_lattice           = active.lattice;
    m_bound_node        = active.bound_node;
    m_point             = active.lattice->get_selected_point();
    m_rest_local        = control_point_rest_position(*active.lattice, cage_min, cage_max, m_point);
    m_before_offset     = active.lattice->get_control_point_offset(m_point);
    m_world_from_node   = active.bound_node->world_from_node();
    m_node_from_world   = active.bound_node->node_from_world();
    m_driver_transform  = active.lattice->get_control_point_transform();
    m_driver_inverse    = safe_inverse(m_driver_transform);
    m_before_parameters = active.lattice->dump_parameters();
    m_active            = true;
}

void Lattice_point_transform::apply(App_context&, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (!m_active) {
        return;
    }
    const std::shared_ptr<Lattice_node> lattice = m_lattice.lock();
    if (!lattice) {
        return;
    }
    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    if (!is_nontrivial_delta(world_delta)) {
        // Click without motion: restore the exact captured offset instead of
        // round-tripping it through the (identity) delta, avoiding float-ULP
        // drift and a spurious re-evaluation.
        lattice->set_control_point_offset(m_point, m_before_offset);
        return;
    }
    // Forward: offset -> driver transform -> bound node -> world; the gizmo
    // delta applies in world, then the chain inverts back to an offset.
    const glm::vec3 before_local = glm::vec3{m_driver_transform * glm::vec4{m_rest_local + m_before_offset, 1.0f}};
    const glm::vec3 before_world = glm::vec3{m_world_from_node * glm::vec4{before_local, 1.0f}};
    const glm::vec3 after_world  = glm::vec3{world_delta * glm::vec4{before_world, 1.0f}};
    const glm::vec3 after_local  = glm::vec3{m_node_from_world * glm::vec4{after_world, 1.0f}};
    const glm::vec3 after_pre    = glm::vec3{m_driver_inverse * glm::vec4{after_local, 1.0f}};
    lattice->set_control_point_offset(m_point, after_pre - m_rest_local);
}

void Lattice_point_transform::commit(App_context& context)
{
    if (!m_active) {
        return;
    }
    m_active = false;
    const std::shared_ptr<Lattice_node> lattice = m_lattice.lock();
    if (!lattice || (context.geometry_graph_window == nullptr) || (context.operation_stack == nullptr)) {
        return;
    }
    std::string after_parameters = lattice->dump_parameters();
    if (after_parameters == m_before_parameters) {
        return;
    }
    const std::shared_ptr<Geometry_graph_node> node = std::dynamic_pointer_cast<Geometry_graph_node>(lattice->node_from_this());
    context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_parameter_operation>(
            *context.geometry_graph_window,
            node,
            std::move(m_before_parameters),
            std::move(after_parameters)
        )
    );
}

}
