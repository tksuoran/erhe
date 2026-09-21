#include "scene/draw_mode_system.hpp"

#include "scene/draw_mode_cards.hpp"
#include "scene/draw_mode_properties.hpp"

#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <algorithm>

namespace editor {

Draw_mode_system::~Draw_mode_system() noexcept
{
    for (std::pair<erhe::scene::Node* const, Draw_mode_entry>& entry : m_entries) {
        remove_card_proxy(entry.second);
    }
}

void Draw_mode_system::on_node_registered(erhe::scene::Node& node)
{
    if (!carries_draw_mode(node)) {
        return;
    }
    m_entries.emplace(&node, Draw_mode_entry{});
    apply_pruning(node);
    queue_proxy_rebuild(node);
}

void Draw_mode_system::on_node_unregistered(erhe::scene::Node& node)
{
    const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>::iterator i = m_entries.find(&node);
    if (i == m_entries.end()) {
        return;
    }
    remove_card_proxy(i->second);
    m_entries.erase(i);
    node.set_prunes_children(false);
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_proxy_rebuilds_mutex};
    const std::vector<erhe::scene::Node*>::iterator pending = std::remove(
        m_proxy_rebuilds.begin(), m_proxy_rebuilds.end(), &node
    );
    m_proxy_rebuilds.erase(pending, m_proxy_rebuilds.end());
}

void Draw_mode_system::on_values_changed(erhe::scene::Node& node, const erhe::property::Dependency_property& property)
{
    const bool carries = carries_draw_mode(node);
    const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>::iterator i = m_entries.find(&node);
    if (!carries) {
        if (i != m_entries.end()) {
            // The key property's effective value went back to its default:
            // the prim carries no draw mode any more, so the pruning and the
            // proxy go with it.
            remove_card_proxy(i->second);
            m_entries.erase(i);
            node.set_prunes_children(false);
        }
        return;
    }
    if (i == m_entries.end()) {
        m_entries.emplace(&node, Draw_mode_entry{});
    } else if (
        (&property == Draw_mode::extents_hint_min_property.get_ptr()) ||
        (&property == Draw_mode::extents_hint_max_property.get_ptr())
    ) {
        i->second.extent_known = false;
    }
    // The key property arriving and the mode itself both move the pruning
    // opinion; set_prunes_children is a no-op when it does not.
    apply_pruning(node);
    // Every value of the group reaches the cards - the mode and the
    // visibility say which faces exist, the geometry and the extent where
    // they are, the textures and the color what they show - so any of them
    // rebuilds the proxy, and only a change does.
    queue_proxy_rebuild(node);
}

void Draw_mode_system::on_node_active_changed(erhe::scene::Node& node)
{
    // An inactive prim is out of render, pick and simulation, so the subtree
    // it stands for is not drawn and neither is the proxy that would stand in
    // for it: the bit going out takes the proxy away and the bit coming back
    // builds it.
    if (m_entries.find(&node) == m_entries.end()) {
        return;
    }
    queue_proxy_rebuild(node);
}

auto Draw_mode_system::get_entries() const -> const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>&
{
    return m_entries;
}

void Draw_mode_system::apply_pruning(erhe::scene::Node& node)
{
    const std::optional<Draw_mode_data> data = read_draw_mode(node);
    const bool prunes =
        data.has_value() &&
        (data.value().draw_mode != erhe::scene::Draw_mode::inherited) &&
        (data.value().draw_mode != erhe::scene::Draw_mode::default_);
    node.set_prunes_children(prunes);
}

auto Draw_mode_system::get_extent(erhe::scene::Node& node, glm::vec3& out_min, glm::vec3& out_max) -> bool
{
    const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>::iterator i = m_entries.find(&node);
    if (i == m_entries.end()) {
        return false;
    }
    Draw_mode_entry& entry = i->second;
    if (!entry.extent_known) {
        entry.extent_known = true;
        entry.extent_valid = false;
        const std::optional<Draw_mode_data> data = read_draw_mode(node);
        if (!data.has_value()) {
            return false;
        }
        if (data.value().has_extents_hint) {
            entry.extent_min   = data.value().extents_hint_min;
            entry.extent_max   = data.value().extents_hint_max;
            entry.extent_valid = glm::all(glm::lessThanEqual(entry.extent_min, entry.extent_max));
        } else {
            // The meshes at and below the prim, in the prim's own space: the
            // same box `extentsHint` would state. Measured once, not per
            // frame; a change of the hint asks for it again.
            const glm::mat4  node_from_world = node.node_from_world();
            erhe::math::Aabb bounds{};
            node.for_each<erhe::scene::Mesh>(
                [&bounds, &node_from_world](erhe::scene::Mesh& mesh) -> bool {
                    // The proxy is what the extent produced; measuring it
                    // back would make the box feed itself.
                    if ((mesh.get_flag_bits() & erhe::Item_flags::draw_mode_proxy) != 0) {
                        return true;
                    }
                    const glm::mat4 node_from_mesh = node_from_world * mesh.world_from_node();
                    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
                        if (!mesh_primitive.primitive) {
                            continue;
                        }
                        const erhe::math::Aabb primitive_bounds = mesh_primitive.primitive->get_bounding_box();
                        if (!primitive_bounds.is_valid()) {
                            continue;
                        }
                        bounds.include(primitive_bounds.transformed_by(node_from_mesh));
                    }
                    return true;
                }
            );
            if (bounds.is_valid()) {
                entry.extent_min   = bounds.min;
                entry.extent_max   = bounds.max;
                entry.extent_valid = true;
            }
        }
    }
    if (!entry.extent_valid) {
        return false;
    }
    out_min = entry.extent_min;
    out_max = entry.extent_max;
    return true;
}

void Draw_mode_system::queue_proxy_rebuild(erhe::scene::Node& node)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_proxy_rebuilds_mutex};
    for (const erhe::scene::Node* const pending : m_proxy_rebuilds) {
        if (pending == &node) {
            return;
        }
    }
    m_proxy_rebuilds.push_back(&node);
}

void Draw_mode_system::flush_proxy_rebuilds(App_context& context)
{
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_proxy_rebuilds_mutex};
        std::swap(m_proxy_rebuild_scratch, m_proxy_rebuilds);
    }
    for (erhe::scene::Node* const node : m_proxy_rebuild_scratch) {
        if (node == nullptr) {
            continue;
        }
        rebuild_card_proxy(context, *node);
    }
    m_proxy_rebuild_scratch.clear();
}

void Draw_mode_system::remove_card_proxy(Draw_mode_entry& entry)
{
    if (!entry.card_proxy) {
        return;
    }
    entry.card_proxy->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    entry.card_proxy.reset();
}

void Draw_mode_system::rebuild_card_proxy(App_context& context, erhe::scene::Node& node)
{
    const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>::iterator i = m_entries.find(&node);
    if (i == m_entries.end()) {
        return;
    }
    Draw_mode_entry& entry = i->second;
    remove_card_proxy(entry);
    const std::optional<Draw_mode_data> data = read_draw_mode(node);
    if (!data.has_value() || (data.value().resolved_draw_mode != erhe::scene::Draw_mode::cards)) {
        return;
    }
    // An inactive prim is drawn by nothing, so a proxy of it would be
    // geometry, materials and textures nobody sees. The clones of an instance
    // below a pruning prim are the case this is measured on.
    if (!node.is_active()) {
        return;
    }
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    if (!get_extent(node, min, max)) {
        return;
    }
    entry.card_proxy = build_draw_mode_card_proxy(context, node, data.value(), min, max);
    if (entry.card_proxy) {
        erhe::scene::set_mesh_parent(entry.card_proxy, node.shared_node_from_this());
    }
}

} // namespace editor
