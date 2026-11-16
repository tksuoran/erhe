#include "level/kit_connector_indicators.hpp"

#include "level/kit_connector_snap.hpp"
#include "app_message.hpp"
#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "config/generated/debug_visualizations_style.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

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

auto Kit_connector_indicators::get_local_connectors(const erhe::scene::Node& node) -> const std::vector<Kit_connector>&
{
    const std::uint64_t id = static_cast<std::uint64_t>(node.get_id());
    const std::unordered_map<std::uint64_t, Cached_piece>::const_iterator i = m_local_connectors.find(id);
    if (i != m_local_connectors.end()) {
        return i->second.connectors;
    }
    Cached_piece& piece = m_local_connectors[id];
    piece.item = &node;
    // An unreadable record caches no frames, as get_world_connectors() refuses it; a disabled connector is not drawn (U33).
    static_cast<void>(m_level_kits.get_local_connectors(node, piece.connectors, Kit_connector_set::enabled));
    return piece.connectors;
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
    // A drag shows its connectors in the views of its own scene only.
    if ((drag_snap != nullptr) && (drag_snap->get_scene_root() == &scene_root)) {
        drag_snap->collect_indicators(c_connector_indicator_reach, out_indicators);
        return;
    }
    if (mode == Visualization_mode::all) {
        scene_root.get_scene().for_each_node(
            [this, &out_indicators](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
                if (node && carries_kit_piece(*node)) {
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
                    if (carries_kit_piece(visited)) {
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

void Kit_connector_indicators::forget_piece(const erhe::scene::Node& node)
{
    m_local_connectors.erase(static_cast<std::uint64_t>(node.get_id()));
}

auto get_connector_indicator_color(const Debug_visualizations_style& style, const Kit_connector_indicator_role role) -> const glm::vec4&
{
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
    std::erase_if(
        m_local_connectors,
        [&removed](const std::pair<const std::uint64_t, Cached_piece>& entry) -> bool {
            return removed.lookup.contains(entry.second.item);
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
