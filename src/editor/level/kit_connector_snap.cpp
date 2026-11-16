#include "level/kit_connector_snap.hpp"

#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_root.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace editor {

namespace {

[[nodiscard]] auto as_node(erhe::scene::Node& node) -> std::shared_ptr<erhe::scene::Node>
{
    return std::dynamic_pointer_cast<erhe::scene::Node>(node.shared_from_this());
}

[[nodiscard]] auto is_hosted(const erhe::scene::Node& node) -> bool
{
    return node.get_item_host() != nullptr;
}

} // anonymous namespace

void Kit_connector_drag_snap::begin(
    const Level_kits&                                         level_kits,
    const Scene_root&                                         scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Node>> moved_roots,
    const std::span<const std::shared_ptr<erhe::scene::Node>> excluded,
    const float                                               max_distance
)
{
    ERHE_PROFILE_FUNCTION();

    clear();
    if (!(max_distance > 0.0f) || !std::isfinite(max_distance)) {
        return;
    }
    m_max_distance = max_distance;

    // The moved pieces' connectors in the frame of the root they ride with: a step reads one matrix per root.
    for (std::size_t root_index = 0, root_end = moved_roots.size(); root_index < root_end; ++root_index) {
        const std::shared_ptr<erhe::scene::Node>& root = moved_roots[root_index];
        m_roots.push_back(root);
        if (!root || !is_hosted(*root)) {
            continue;
        }
        const glm::mat4 root_from_world = root->node_from_world();
        static_cast<void>(
            root->for_each<erhe::scene::Node>(
                [this, &level_kits, root_index, &root_from_world](erhe::scene::Node& node) -> bool {
                    if (!carries_kit_piece(node)) {
                        return true;
                    }
                    if (!level_kits.get_local_connectors(node, m_local_connectors, Kit_connector_set::all).empty()) {
                        return true;
                    }
                    const Kit_connector_switches switches = level_kits.get_connector_switches(node, m_local_connectors);
                    if (!switches.connectors_enabled) {
                        return true;
                    }
                    // D1.12: the full world matrix; a piece whose frames degenerate (a zero scale axis) takes no part.
                    const glm::mat4 root_from_node = root_from_world * node.world_from_node();
                    if (!transform_connectors(m_local_connectors, root_from_node, m_world_connectors)) {
                        return true;
                    }
                    const std::shared_ptr<erhe::scene::Node> shared_node = as_node(node);
                    for (std::size_t k = 0, k_end = m_local_connectors.size(); k < k_end; ++k) {
                        // U27: a disabled connector is neither a source nor a target; k stays the shape's index.
                        if (!switches.is_enabled(m_local_connectors[k].name, k)) {
                            continue;
                        }
                        const Kit_connector in_root = connector_in_root(m_local_connectors[k], root_from_node);
                        m_moved.push_back(
                            Moved_point{
                                .root             = root_index,
                                .position_in_root = in_root.position,
                                .outward_in_root  = in_root.outward,
                                .up_in_root       = in_root.up,
                                .piece            = shared_node,
                                .piece_id         = static_cast<std::uint64_t>(node.get_id()),
                                .connector_index  = k,
                                .name             = m_local_connectors[k].name
                            }
                        );
                    }
                    return true;
                }
            )
        );
    }
    if (m_moved.empty()) {
        return;
    }

    // Every kit piece of the scene outside the moved subtrees is a target, locked layout children included.
    scene_root.get_scene().for_each_node(
        [this, &level_kits, excluded](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (!node || !carries_kit_piece(*node)) {
                return true;
            }
            for (const std::shared_ptr<erhe::scene::Node>& moved : excluded) {
                if (moved && ((node.get() == moved.get()) || node->is_ancestor(moved.get()))) {
                    return true;
                }
            }
            if (!level_kits.get_local_connectors(*node, m_local_connectors, Kit_connector_set::all).empty() || m_local_connectors.empty()) {
                return true;
            }
            const Kit_connector_switches switches = level_kits.get_connector_switches(*node, m_local_connectors);
            if (!switches.connectors_enabled) {
                return true;
            }
            if (!transform_connectors(m_local_connectors, node->world_from_node(), m_world_connectors)) {
                return true;
            }
            const std::size_t piece_index = m_target_pieces.size();
            const std::size_t first_point = m_target_points.size();
            for (std::size_t k = 0, k_end = m_local_connectors.size(); k < k_end; ++k) {
                if (!switches.is_enabled(m_local_connectors[k].name, k)) {
                    continue;
                }
                m_target_points.push_back(
                    Target_point{
                        .local_position  = m_local_connectors[k].position,
                        .local_outward   = m_local_connectors[k].outward,
                        .local_up        = m_local_connectors[k].up,
                        .piece           = piece_index,
                        .connector_index = k,
                        .name            = m_local_connectors[k].name
                    }
                );
            }
            if (m_target_points.size() > first_point) {
                m_target_pieces.push_back(
                    Target_piece{
                        .node        = node,
                        .piece_id    = static_cast<std::uint64_t>(node->get_id()),
                        .first_point = first_point,
                        .point_count = m_target_points.size() - first_point
                    }
                );
            }
            return true;
        }
    );
    if (m_target_points.empty()) {
        m_moved.clear();
        return;
    }
    // Capacity for every step, so steps allocate nothing.
    m_moved_world.reserve(m_moved.size());
    m_moved_world_source.reserve(m_moved.size());
    m_target_world.reserve(m_target_points.size());
    m_target_world_source.reserve(m_target_points.size());
    m_scene_root = &scene_root;
    m_active     = true;
}

void Kit_connector_drag_snap::end()
{
    m_active       = false;
    m_scene_root   = nullptr;
    m_max_distance = 0.0f;
    m_roots.clear();
    m_moved.clear();
    m_target_pieces.clear();
    m_target_points.clear();
    m_moved_world.clear();
    m_moved_world_source.clear();
    m_target_world.clear();
    m_target_world_source.clear();
    m_snapped_moved.reset();
    m_snapped_target.reset();
}

void Kit_connector_drag_snap::clear()
{
    end();
    m_state = Kit_connector_snap_state{};
}

auto Kit_connector_drag_snap::is_active() const -> bool
{
    return m_active;
}

auto Kit_connector_drag_snap::get_scene_root() const -> const Scene_root*
{
    return m_active ? m_scene_root : nullptr;
}

auto Kit_connector_drag_snap::step(const Kit_connector_snap_constraint& constraint) -> std::optional<glm::vec3>
{
    ERHE_PROFILE_FUNCTION();

    m_state = Kit_connector_snap_state{};
    m_state.constraint = constraint;
    m_snapped_moved.reset();
    m_snapped_target.reset();
    if (!m_active) {
        return std::nullopt;
    }

    m_moved_world.clear();
    m_moved_world_source.clear();
    for (std::size_t i = 0, end = m_moved.size(); i < end; ++i) {
        const Moved_point& point = m_moved[i];
        const std::shared_ptr<erhe::scene::Node> root = m_roots[point.root].lock();
        if (!root || !is_hosted(*root)) {
            continue;
        }
        // The root's pose of this step carries its connectors through the step's rotation and scale too.
        const std::optional<glm::vec3> position = carry_connector_position(point.position_in_root, root->world_from_node());
        if (!position.has_value()) {
            continue;
        }
        m_moved_world.push_back(
            Kit_connector_point{
                .position        = position.value(),
                .piece_id        = point.piece_id,
                .connector_index = point.connector_index
            }
        );
        m_moved_world_source.push_back(i);
    }

    m_target_world.clear();
    m_target_world_source.clear();
    for (const Target_piece& piece : m_target_pieces) {
        const std::shared_ptr<erhe::scene::Node> node = piece.node.lock();
        if (!node || !is_hosted(*node)) {
            continue;
        }
        const glm::mat4& world_from_node = node->world_from_node();
        for (std::size_t k = piece.first_point, k_end = piece.first_point + piece.point_count; k < k_end; ++k) {
            const Target_point& point = m_target_points[k];
            m_target_world.push_back(
                Kit_connector_point{
                    .position        = glm::vec3{world_from_node * glm::vec4{point.local_position, 1.0f}},
                    .piece_id        = piece.piece_id,
                    .connector_index = point.connector_index
                }
            );
            m_target_world_source.push_back(k);
        }
    }

    const Kit_connector_snap_match match = find_connector_snap(m_moved_world, m_target_world, m_max_distance);
    if (!match.found) {
        return std::nullopt;
    }
    const Moved_point&  moved  = m_moved[m_moved_world_source[match.moved]];
    const Target_point& target = m_target_points[m_target_world_source[match.other]];
    m_state.snapped          = true;
    m_state.distance         = match.distance;
    m_state.full_offset      = m_target_world[match.other].position - m_moved_world[match.moved].position;
    m_state.offset           = constrain_connector_snap_offset(m_state.full_offset, constraint);
    m_state.piece            = moved.piece;
    m_state.connector        = moved.name;
    m_state.target           = m_target_pieces[target.piece].node;
    m_state.target_connector = target.name;
    m_snapped_moved          = m_moved_world_source[match.moved];
    m_snapped_target         = m_target_world_source[match.other];
    return m_state.offset;
}

void Kit_connector_drag_snap::reject_step()
{
    m_state = Kit_connector_snap_state{};
    m_snapped_moved.reset();
    m_snapped_target.reset();
}

void Kit_connector_drag_snap::skip_step(const Kit_connector_snap_skip reason)
{
    m_state = Kit_connector_snap_state{};
    m_state.skip = reason;
    m_snapped_moved.reset();
    m_snapped_target.reset();
}

auto Kit_connector_drag_snap::get_state() const -> const Kit_connector_snap_state&
{
    return m_state;
}

auto Kit_connector_drag_snap::get_moved_count() const -> std::size_t
{
    return m_moved.size();
}

auto Kit_connector_drag_snap::get_target_count() const -> std::size_t
{
    return m_target_points.size();
}

void Kit_connector_drag_snap::collect_indicators(const float reach_factor, std::vector<Kit_connector_indicator>& out_indicators) const
{
    if (!m_active) {
        return;
    }
    const std::size_t moved_begin = out_indicators.size();
    for (std::size_t i = 0, end = m_moved.size(); i < end; ++i) {
        const Moved_point& point = m_moved[i];
        const std::shared_ptr<erhe::scene::Node> root = m_roots[point.root].lock();
        if (!root || !is_hosted(*root)) {
            continue;
        }
        const Kit_connector in_root{.name = point.name, .position = point.position_in_root, .outward = point.outward_in_root, .up = point.up_in_root};
        const std::optional<Kit_connector> frame = transform_connector(in_root, root->world_from_node());
        if (!frame.has_value()) {
            continue;
        }
        const bool snapped = m_snapped_moved.has_value() && (m_snapped_moved.value() == i);
        out_indicators.push_back(
            Kit_connector_indicator{
                .frame    = frame.value(),
                .piece_id = point.piece_id,
                .role     = snapped ? Kit_connector_indicator_role::snapped : Kit_connector_indicator_role::moved
            }
        );
    }
    const std::size_t moved_end      = out_indicators.size();
    const float       reach          = reach_factor * m_max_distance;
    const float       reach_squared  = reach * reach;
    for (const Target_piece& piece : m_target_pieces) {
        const std::shared_ptr<erhe::scene::Node> node = piece.node.lock();
        if (!node || !is_hosted(*node)) {
            continue;
        }
        const glm::mat4& world_from_node = node->world_from_node();
        for (std::size_t k = piece.first_point, k_end = piece.first_point + piece.point_count; k < k_end; ++k) {
            const Target_point& point    = m_target_points[k];
            const glm::vec3     position = glm::vec3{world_from_node * glm::vec4{point.local_position, 1.0f}};
            const bool          snapped  = m_snapped_target.has_value() && (m_snapped_target.value() == k);
            bool                in_reach = snapped;
            for (std::size_t m = moved_begin; !in_reach && (m < moved_end); ++m) {
                const glm::vec3 d = out_indicators[m].frame.position - position;
                in_reach = (glm::dot(d, d) <= reach_squared);
            }
            if (!in_reach) {
                continue;
            }
            const Kit_connector local{.name = point.name, .position = point.local_position, .outward = point.local_outward, .up = point.local_up};
            const std::optional<Kit_connector> frame = transform_connector(local, world_from_node);
            if (!frame.has_value()) {
                continue;
            }
            out_indicators.push_back(
                Kit_connector_indicator{
                    .frame    = frame.value(),
                    .piece_id = piece.piece_id,
                    .role     = snapped ? Kit_connector_indicator_role::snapped : Kit_connector_indicator_role::target
                }
            );
        }
    }
}

} // namespace editor
