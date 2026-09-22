#pragma once

#include "erhe_scene/node_system.hpp"

#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;

// The runtime state one node's draw mode implies: the cached extent the
// proxies are sized from and the card proxy itself.
class Draw_mode_entry
{
public:
    // The generated quad geometry a `cards` draw mode supplies in place of the
    // subtree it prunes: a Mesh child prim of the model prim, flagged
    // Item_flags::draw_mode_proxy and Item_flags::session_only. Null in every
    // other mode.
    std::shared_ptr<erhe::scene::Mesh> card_proxy;
    // The box the proxies are sized from, in the prim's own space, measured
    // once (doc/editor/scene.md, "Draw modes").
    glm::vec3                          extent_min  {0.0f};
    glm::vec3                          extent_max  {0.0f};
    bool                               extent_valid{false};
    bool                               extent_known{false};
};

// The per-scene owner of the `Draw_mode` value group's runtime state
// (doc/erhe/scene.md "Node systems"). One of these is owned by each
// Scene_root and added to its scene, which
// drives it from the three change sites; it keeps one Draw_mode_entry per
// node carrying a draw mode, keyed by a raw Node*, and erases the entry when
// the node leaves the scene, so a scene close releases the proxies it holds.
class Draw_mode_system : public erhe::scene::INode_system
{
public:
    ~Draw_mode_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (erhe::scene::Node& node) override;
    void on_node_unregistered  (erhe::scene::Node& node) override;
    void on_values_changed     (erhe::scene::Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(erhe::scene::Node& node) override;

    // The nodes of this scene carrying a draw mode, for the renderer.
    [[nodiscard]] auto get_entries() const -> const std::unordered_map<erhe::scene::Node*, Draw_mode_entry>&;

    // The box the proxies of `node` are sized from: the authored
    // `extentsHint` when there is one, else the bounds of the meshes at and
    // below the prim. False when neither exists, and false for a node
    // carrying no draw mode. The computed form is cached in the node's entry.
    [[nodiscard]] auto get_extent(erhe::scene::Node& node, glm::vec3& out_min, glm::vec3& out_max) -> bool;

    // Builds the card proxies every queued node asks for. Inserting a prim is
    // main-thread work that no change site may do, so the change sites only
    // queue and App_scenes::rebuild_draw_mode_proxies() calls this once per
    // frame.
    void flush_proxy_rebuilds(App_context& context);

private:
    void queue_proxy_rebuild(erhe::scene::Node& node);
    void rebuild_card_proxy (App_context& context, erhe::scene::Node& node);
    void remove_card_proxy  (Draw_mode_entry& entry);
    // Writes Item_base::set_prunes_children on the node from its own draw
    // mode opinion: only a prim that authors a proxy mode of its own prunes,
    // because a prim that defers is either inside a pruned subtree already or
    // resolves to `default`.
    void apply_pruning(erhe::scene::Node& node);

    std::unordered_map<erhe::scene::Node*, Draw_mode_entry> m_entries;
    // Queued by the change sites, drained by flush_proxy_rebuilds; a node
    // leaving the scene is taken out of it, so every entry names a node this
    // system still holds.
    std::vector<erhe::scene::Node*>                         m_proxy_rebuilds;
    ERHE_PROFILE_MUTEX(std::mutex,                          m_proxy_rebuilds_mutex);
    // Scratch of flush_proxy_rebuilds; cleared after use, capacity kept.
    std::vector<erhe::scene::Node*>                         m_proxy_rebuild_scratch;
};

} // namespace editor
