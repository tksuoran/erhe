#include "level/kit_layout_indicators.hpp"

#include "app_message.hpp"
#include "level/level_kits.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <algorithm>

namespace editor {

Kit_layout_indicators::Kit_layout_indicators(Level_kits& level_kits)
    : m_level_kits{level_kits}
    , m_prepared  {std::make_unique<Kit_layout_preparation>()}
{
}

Kit_layout_indicators::~Kit_layout_indicators() noexcept = default;

void Kit_layout_indicators::make_layout(Scene_root& scene_root, const erhe::scene::Node& node, const Kit_layout_data& data, Cached_layout& out_layout)
{
    out_layout.item = &node;
    out_layout.outline.clear();
    out_layout.outline.kind = data.pattern.kind;
    // The generation's own refusal test; the node's scale is checked as it is drawn, so the cache takes no transform.
    out_layout.refusal = m_level_kits.prepare_layout_generation(scene_root, node, data, glm::mat4{1.0f}, *m_prepared);
    if (out_layout.refusal.empty() && (data.pattern.kind != Repeat_pattern_kind::path)) {
        make_layout_outline(data.pattern, m_prepared->run, m_prepared->poses, m_prepared->sizes.items, out_layout.outline);
    }
    m_prepared->clear();
}

auto Kit_layout_indicators::get_outline(Scene_root& scene_root, const erhe::scene::Node& node) -> const Kit_layout_outline*
{
    const std::uint64_t id = static_cast<std::uint64_t>(node.get_id());
    const std::unordered_map<std::uint64_t, Cached_layout>::const_iterator i = m_layouts.find(id);
    if (i != m_layouts.end()) {
        return i->second.refusal.empty() ? &i->second.outline : nullptr;
    }
    Cached_layout& layout = m_layouts[id];
    layout.refusal = read_kit_layout(node, m_data);
    if (!layout.refusal.empty()) {
        layout.item = &node;
        return nullptr;
    }
    make_layout(scene_root, node, m_data, layout);
    return layout.refusal.empty() ? &layout.outline : nullptr;
}

auto Kit_layout_indicators::set_pending_values(Scene_root& scene_root, const erhe::scene::Node& node, const Kit_layout_data& data) -> bool
{
    const std::uint64_t id = static_cast<std::uint64_t>(node.get_id());
    if ((m_pending_node != nullptr) && (m_pending_id != id)) {
        end_pending();
    }
    m_pending_id   = id;
    m_pending_node = &node;
    make_layout(scene_root, node, data, m_candidate);
    if (!m_candidate.refusal.empty()) {
        return false;
    }
    std::swap(m_layouts[id], m_candidate);
    return true;
}

void Kit_layout_indicators::end_pending()
{
    if (m_pending_node == nullptr) {
        return;
    }
    m_layouts.erase(m_pending_id);
    m_pending_id   = 0;
    m_pending_node = nullptr;
}

void Kit_layout_indicators::end_pending_unless_selected()
{
    if ((m_pending_node != nullptr) && !m_pending_node->is_selected()) {
        end_pending();
    }
}

void Kit_layout_indicators::add_layout(
    Scene_root&                    scene_root,
    const erhe::scene::Node&       node,
    const Kit_layout_circles       circles,
    std::vector<Kit_layout_shown>& out_shown
)
{
    const Kit_layout_outline* const outline = get_outline(scene_root, node);
    if ((outline == nullptr) || (outline->kind == Repeat_pattern_kind::path) || !is_layout_scale_kept(node.world_from_node())) {
        return;
    }
    out_shown.push_back(Kit_layout_shown{.node = &node, .outline = outline, .circles = circles});
}

auto Kit_layout_indicators::is_selected_layout(const erhe::scene::Node& node) const -> bool
{
    return std::find(m_selected_layouts.begin(), m_selected_layouts.end(), &node) != m_selected_layouts.end();
}

void Kit_layout_indicators::collect(
    Scene_root&                    scene_root,
    const Visualization_mode       mode,
    const Selection&               selection,
    std::vector<Kit_layout_shown>& out_shown
)
{
    ERHE_PROFILE_FUNCTION();

    out_shown.clear();
    if ((mode != Visualization_mode::selected) && (mode != Visualization_mode::all)) {
        return;
    }
    // The layout nodes at or above a selected node of the scene.
    m_selected_layouts.clear();
    for (const std::shared_ptr<erhe::Item_base>& item : selection.get_selected_items()) {
        const erhe::scene::Node* const selected = dynamic_cast<const erhe::scene::Node*>(item.get());
        if ((selected == nullptr) || (selected->get_item_host() != &scene_root)) {
            continue;
        }
        if (carries_kit_layout(*selected) && !is_selected_layout(*selected)) {
            m_selected_layouts.push_back(selected);
        }
        for (std::shared_ptr<erhe::scene::Node> parent = selected->get_parent_node(); parent; parent = parent->get_parent_node()) {
            if (carries_kit_layout(*parent) && !is_selected_layout(*parent)) {
                m_selected_layouts.push_back(parent.get());
            }
        }
    }
    if (mode == Visualization_mode::selected) {
        for (const erhe::scene::Node* const node : m_selected_layouts) {
            add_layout(scene_root, *node, Kit_layout_circles::shown, out_shown);
        }
        return;
    }
    scene_root.get_scene().for_each_node(
        [this, &scene_root, &out_shown](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (node && carries_kit_layout(*node)) {
                add_layout(scene_root, *node, is_selected_layout(*node) ? Kit_layout_circles::shown : Kit_layout_circles::hidden, out_shown);
            }
            return true;
        }
    );
}

void Kit_layout_indicators::forget_layout(const erhe::scene::Node& node)
{
    forget_layout(static_cast<std::uint64_t>(node.get_id()));
}

void Kit_layout_indicators::forget_layout(const std::uint64_t node_id)
{
    m_layouts.erase(node_id);
}

void Kit_layout_indicators::forget_all()
{
    m_layouts.clear();
    m_pending_id   = 0;
    m_pending_node = nullptr;
}

void Kit_layout_indicators::forget_removed(const Removed_items& removed)
{
    if ((m_pending_node != nullptr) && removed.lookup.contains(static_cast<const erhe::Item_base*>(m_pending_node))) {
        m_pending_id   = 0;
        m_pending_node = nullptr;
    }
    std::erase_if(
        m_layouts,
        [&removed](const std::pair<const std::uint64_t, Cached_layout>& entry) -> bool {
            return removed.lookup.contains(entry.second.item);
        }
    );
}

auto Kit_layout_indicators::get_cached_layout_count() const -> std::size_t
{
    return m_layouts.size();
}

} // namespace editor
