#pragma once

#include "level/kit_layout.hpp"
#include "level/kit_layout_outline.hpp"

#include "erhe_renderer/generated/visualization_mode.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe       { class Item_base; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

struct Removed_items;
class Kit_layout_preparation;
class Level_kits;
class Scene_root;
class Selection;

// Whether a shown scatter draws its spacing circles: only for a layout that is selected or has a selected node below it.
enum class Kit_layout_circles : unsigned int
{
    hidden = 0,
    shown
};

// A kit layout node a view draws this frame, with its cached outline in the node's frame.
class Kit_layout_shown
{
public:
    const erhe::scene::Node*  node   {nullptr};
    const Kit_layout_outline* outline{nullptr};
    Kit_layout_circles        circles{Kit_layout_circles::hidden};
};

// The kit layout outlines a view draws (doc/editor/level_tools.md "Kit layout drawing"); owned by Level_kits.
class Kit_layout_indicators
{
public:
    explicit Kit_layout_indicators(Level_kits& level_kits);
    ~Kit_layout_indicators() noexcept;

    // The mode's layout nodes of the scene (selected: a selected layout node or one above a selected node); fills
    // out_shown (cleared, capacity kept). A node a generation refuses, and a path layout, are not shown.
    void collect(Scene_root& scene_root, Visualization_mode mode, const Selection& selection, std::vector<Kit_layout_shown>& out_shown);

    // The cached outline of the node (the pending one during a drag), made on first use after a change; null when its
    // values refuse. The scatter region handles read it.
    [[nodiscard]] auto get_outline(Scene_root& scene_root, const erhe::scene::Node& node) -> const Kit_layout_outline*;

    // An unlocked layout's value drag step: the node shows the outline of the pending values (refused values keep the
    // last shown one) until end_pending(). Returns whether the values are shown.
    auto set_pending_values(Scene_root& scene_root, const erhe::scene::Node& node, const Kit_layout_data& data) -> bool;
    void end_pending();
    // Ends the pending outline when its node is no longer selected.
    void end_pending_unless_selected();

    // The node's values may have changed: its outline is made again when next drawn.
    void forget_layout(const erhe::scene::Node& node);
    void forget_layout(std::uint64_t node_id);
    void forget_all   ();
    void forget_removed(const Removed_items& removed);
    [[nodiscard]] auto get_cached_layout_count() const -> std::size_t;

private:
    class Cached_layout
    {
    public:
        const erhe::Item_base* item{nullptr}; // compared with removal sets only, never dereferenced
        std::string            refusal;       // empty when the outline is valid
        Kit_layout_outline     outline;
    };

    void make_layout(Scene_root& scene_root, const erhe::scene::Node& node, const Kit_layout_data& data, Cached_layout& out_layout);
    void add_layout(Scene_root& scene_root, const erhe::scene::Node& node, Kit_layout_circles circles, std::vector<Kit_layout_shown>& out_shown);
    [[nodiscard]] auto is_selected_layout(const erhe::scene::Node& node) const -> bool;

    Level_kits&                                      m_level_kits;
    std::unordered_map<std::uint64_t, Cached_layout> m_layouts; // by node id
    std::vector<const erhe::scene::Node*>            m_selected_layouts;
    Kit_layout_data                                  m_data;
    std::unique_ptr<Kit_layout_preparation>          m_prepared;  // cleared after each use: it pins no material
    Cached_layout                                    m_candidate; // a pending outline before it replaces the shown one
    std::uint64_t                                    m_pending_id{0};
    const erhe::scene::Node*                         m_pending_node{nullptr}; // reset when the node is removed
};

} // namespace editor
