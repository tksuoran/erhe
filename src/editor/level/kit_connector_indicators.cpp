#include "level/kit_connector_indicators.hpp"

#include "level/kit_connector_snap.hpp"
#include "app_message.hpp"
#include "level/kit_layout.hpp"
#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "config/generated/debug_visualizations_style.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto has_selected_ancestor(const erhe::scene::Node& node) -> bool
{
    for (std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node(); parent; parent = parent->get_parent_node()) {
        if (parent->is_selected()) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

Kit_connector_indicators::Kit_connector_indicators(const Level_kits& level_kits)
    : m_level_kits{level_kits}
{
}

namespace {

// A Curve carrying a kit layout: a floor layout's frames come from the cache (a path or stairs layout caches none).
[[nodiscard]] auto is_floor_candidate(const erhe::scene::Node& node) -> bool
{
    return (dynamic_cast<const erhe::scene::Curve*>(&node) != nullptr) && carries_kit_layout(node);
}

} // anonymous namespace

auto Kit_connector_indicators::get_local_connectors(const erhe::scene::Node& node) -> const std::vector<Kit_connector>&
{
    return get_cached_piece(node).connectors;
}

auto Kit_connector_indicators::is_floor_layout(const erhe::scene::Node& node) -> bool
{
    return is_floor_candidate(node) && get_cached_piece(node).floor;
}

auto Kit_connector_indicators::get_cached_piece(const erhe::scene::Node& node) -> const Cached_piece&
{
    const std::uint64_t id = static_cast<std::uint64_t>(node.get_id());
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    const std::uint64_t parent_id = parent ? static_cast<std::uint64_t>(parent->get_id()) : 0;
    // A reparent sends no message (erhe::Hierarchy::set_parent()), so the entry keeps the parent it was read under:
    // under another parent its layout's switches may hide other connectors (U39).
    // A floor layout's frames follow its outline (the curve's revision) and its values (a new slab child).
    const erhe::scene::Curve* const curve = dynamic_cast<const erhe::scene::Curve*>(&node);
    const std::uint64_t revision = (curve != nullptr) ? curve->get_revision() : 0;
    const std::vector<std::shared_ptr<erhe::Hierarchy>>& children = node.get_children();
    const std::uint64_t first_child_id = children.empty() ? 0 : static_cast<std::uint64_t>(children.front()->get_id());
    const std::unordered_map<std::uint64_t, Cached_piece>::const_iterator i = m_local_connectors.find(id);
    if ((i != m_local_connectors.end()) && (i->second.parent_id == parent_id) &&
        (!i->second.floor || ((i->second.revision == revision) && (i->second.first_child_id == first_child_id)))) {
        return i->second;
    }
    Cached_piece& piece = m_local_connectors[id];
    piece.item           = &node;
    piece.parent_id      = parent_id;
    piece.floor          = (curve != nullptr) && editor::is_floor_layout(node);
    piece.revision       = revision;
    piece.first_child_id = first_child_id;
    // An unreadable record caches no frames, as get_world_connectors() refuses it; a disabled or layout-hidden connector is not drawn (U33, U39).
    static_cast<void>(m_level_kits.get_local_connectors(node, piece.connectors, Kit_connector_set::enabled));
    return piece;
}

void Kit_connector_indicators::add_piece(const erhe::scene::Node& node, std::vector<Kit_connector_indicator>& out_indicators)
{
    const std::vector<Kit_connector>& local = get_local_connectors(node);
    if (local.empty() || !transform_connectors(local, node.world_from_node(), m_world_connectors)) {
        return;
    }
    const std::uint64_t id = static_cast<std::uint64_t>(node.get_id());
    for (const Kit_connector& connector : m_world_connectors) {
        const bool chosen =
            ((m_target_choice.node_id == id) && (m_target_choice.connector == connector.name)) ||
            ((m_moved_choice.node_id  == id) && (m_moved_choice.connector  == connector.name));
        out_indicators.push_back(
            Kit_connector_indicator{
                .frame    = connector,
                .piece_id = id,
                .role     = chosen ? Kit_connector_indicator_role::chosen : Kit_connector_indicator_role::piece
            }
        );
    }
}

void Kit_connector_indicators::collect(
    const Scene_root&                     scene_root,
    const Visualization_mode              mode,
    const Selection&                      selection,
    const Kit_connector_drag_snap* const  drag_snap,
    std::vector<Kit_connector_indicator>& out_indicators
)
{
    ERHE_PROFILE_FUNCTION();

    out_indicators.clear();
    collect_pieces(scene_root, mode, selection, drag_snap, out_indicators);
    add_brush_join(scene_root, out_indicators);
}

void Kit_connector_indicators::collect_pieces(
    const Scene_root&                     scene_root,
    const Visualization_mode              mode,
    const Selection&                      selection,
    const Kit_connector_drag_snap* const  drag_snap,
    std::vector<Kit_connector_indicator>& out_indicators
)
{
    // A drag shows its connectors in the views of its own scene only.
    if ((drag_snap != nullptr) && (drag_snap->get_scene_root() == &scene_root)) {
        drag_snap->collect_indicators(c_connector_indicator_reach, out_indicators);
        return;
    }
    if (mode == Visualization_mode::all) {
        scene_root.get_scene().for_each_node(
            [this, &out_indicators](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
                if (node && (carries_kit_piece(*node) || is_floor_candidate(*node))) {
                    add_piece(*node, out_indicators);
                }
                return true;
            }
        );
        return;
    }
    if (mode != Visualization_mode::selected) {
        return;
    }
    for (const std::shared_ptr<erhe::Item_base>& item : selection.get_selected_items()) {
        erhe::scene::Node* const node = dynamic_cast<erhe::scene::Node*>(item.get());
        if ((node == nullptr) || (node->get_item_host() != &scene_root) || has_selected_ancestor(*node)) {
            continue;
        }
        static_cast<void>(
            node->for_each<erhe::scene::Node>(
                [this, &out_indicators](erhe::scene::Node& visited) -> bool {
                    if (carries_kit_piece(visited) || is_floor_candidate(visited)) {
                        add_piece(visited, out_indicators);
                    }
                    return true;
                }
            )
        );
    }
}

void Kit_connector_indicators::set_connect_choice(
    const std::shared_ptr<erhe::scene::Node>& target,
    const std::string&                        target_connector,
    const std::shared_ptr<erhe::scene::Node>& moved,
    const std::string&                        moved_connector
)
{
    m_target_choice = Choice{.node_id = target ? static_cast<std::uint64_t>(target->get_id()) : 0, .connector = target_connector};
    m_moved_choice  = Choice{.node_id = moved  ? static_cast<std::uint64_t>(moved->get_id())  : 0, .connector = moved_connector};
}

void Kit_connector_indicators::clear_connect_choice()
{
    m_target_choice = Choice{};
    m_moved_choice  = Choice{};
}

void Kit_connector_indicators::set_brush_join(
    const Scene_root&                         scene_root,
    const std::shared_ptr<erhe::scene::Node>& piece,
    const char* const                         connector_name,
    const Kit_connector&                      preview_connector,
    const std::uint64_t                       preview_id
)
{
    m_brush_join.scene_root        = &scene_root;
    m_brush_join.piece             = piece;
    m_brush_join.connector         = connector_name;
    m_brush_join.preview_connector = preview_connector;
    m_brush_join.preview_id        = preview_id;
}

void Kit_connector_indicators::clear_brush_join()
{
    m_brush_join = Brush_join{};
}

auto Kit_connector_indicators::has_brush_join(const Scene_root& scene_root) const -> bool
{
    return (m_brush_join.scene_root == &scene_root) && !m_brush_join.piece.expired();
}

void Kit_connector_indicators::add_brush_join(const Scene_root& scene_root, std::vector<Kit_connector_indicator>& out_indicators)
{
    if (m_brush_join.scene_root != &scene_root) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> piece = m_brush_join.piece.lock();
    if (!piece) {
        return;
    }
    // The pair, as a snapping drag draws it: the target connector and the preview's own.
    out_indicators.push_back(
        Kit_connector_indicator{.frame = m_brush_join.preview_connector, .piece_id = m_brush_join.preview_id, .role = Kit_connector_indicator_role::snapped}
    );
    const std::uint64_t    id   = static_cast<std::uint64_t>(piece->get_id());
    const std::string_view name = m_brush_join.connector;
    for (Kit_connector_indicator& indicator : out_indicators) {
        if ((indicator.piece_id == id) && (name == indicator.frame.name)) {
            indicator.role = Kit_connector_indicator_role::snapped;
            return;
        }
    }
    const Kit_connector* const local = find_connector(get_local_connectors(*piece), name);
    if (local == nullptr) {
        return;
    }
    const std::optional<Kit_connector> world = transform_connector(*local, piece->world_from_node());
    if (world.has_value()) {
        out_indicators.push_back(Kit_connector_indicator{.frame = world.value(), .piece_id = id, .role = Kit_connector_indicator_role::snapped});
    }
}

void Kit_connector_indicators::forget_piece(const erhe::scene::Node& node)
{
    m_local_connectors.erase(static_cast<std::uint64_t>(node.get_id()));
}

void Kit_connector_indicators::forget_node(const std::uint64_t node_id)
{
    m_local_connectors.erase(node_id);
}

void Kit_connector_indicators::forget_children_of(const std::uint64_t layout_id)
{
    std::erase_if(
        m_local_connectors,
        [layout_id](const std::pair<const std::uint64_t, Cached_piece>& entry) -> bool {
            return entry.second.parent_id == layout_id;
        }
    );
}

auto get_connector_indicator_color(const Debug_visualizations_style& style, const Kit_connector_indicator_role role, const Kit_connector_types types) -> const glm::vec4&
{
    // C1.8: a shown connector takes its first typed type's color; the roles of a drag or the Connect rows win.
    if (role == Kit_connector_indicator_role::piece) {
        if (types.contains(Kit_connector_type::wall)) {
            return style.connector_wall_color;
        }
        if (types.contains(Kit_connector_type::wall_floor)) {
            return style.connector_wall_floor_color;
        }
        if (types.contains(Kit_connector_type::rail)) {
            return style.connector_rail_color;
        }
    }
    switch (role) {
        case Kit_connector_indicator_role::chosen:  return style.connector_chosen_color;
        case Kit_connector_indicator_role::moved:   return style.connector_moved_color;
        case Kit_connector_indicator_role::target:  return style.connector_target_color;
        case Kit_connector_indicator_role::snapped: return style.connector_snapped_color;
        case Kit_connector_indicator_role::piece:
        default:                                    return style.connector_color;
    }
}

void Kit_connector_indicators::forget_removed(const Removed_items& removed)
{
    // A removed piece's siblings go too: a layout child's head or tail role depends on the child count (U39).
    std::vector<std::uint64_t>& parent_ids = m_removed_parent_ids;
    parent_ids.clear();
    for (const std::pair<const std::uint64_t, Cached_piece>& entry : m_local_connectors) {
        if (removed.lookup.contains(entry.second.item) && (entry.second.parent_id != 0)) {
            parent_ids.push_back(entry.second.parent_id);
        }
    }
    std::erase_if(
        m_local_connectors,
        [&removed, &parent_ids](const std::pair<const std::uint64_t, Cached_piece>& entry) -> bool {
            return
                removed.lookup.contains(entry.second.item) ||
                (std::find(parent_ids.begin(), parent_ids.end(), entry.second.parent_id) != parent_ids.end());
        }
    );
}

auto Kit_connector_indicators::get_cached_piece_count() const -> std::size_t
{
    return m_local_connectors.size();
}

void Kit_connector_indicators::forget_all()
{
    m_local_connectors.clear();
}

} // namespace editor
