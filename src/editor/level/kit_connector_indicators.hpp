#pragma once

#include "level/kit_connector.hpp"

#include "erhe_renderer/generated/visualization_mode.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Debug_visualizations_style;

namespace erhe       { class Item_base; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Kit_connector_drag_snap;
struct Removed_items;
class Level_kits;
class Scene_root;
class Selection;

// The connector frames a view draws (doc/editor/level_tools.md "Connector indicators"); owned by Level_kits.
class Kit_connector_indicators
{
public:
    explicit Kit_connector_indicators(const Level_kits& level_kits);

    // A snapping drag's connectors whatever the mode, else the mode's pieces (selected: at or below a selected
    // node); fills out_indicators (cleared, capacity kept) at the current world transforms.
    void collect(
        const Scene_root&                     scene_root,
        Visualization_mode                    mode,
        const Selection&                      selection,
        const Kit_connector_drag_snap*        drag_snap,
        std::vector<Kit_connector_indicator>& out_indicators
    );

    // The Level Kit window's Connect rows: the chosen target and moved connectors (empty names: none).
    void set_connect_choice(
        const std::shared_ptr<erhe::scene::Node>& target,
        const std::string&                        target_connector,
        const std::shared_ptr<erhe::scene::Node>& moved,
        const std::string&                        moved_connector
    );
    void clear_connect_choice();

    // The Brush tool's kit mode preview joined at this connector (step 40): drawn as the snapped pair in the scene's views.
    void set_brush_join(
        const Scene_root&                         scene_root,
        const std::shared_ptr<erhe::scene::Node>& piece,
        const char*                               connector_name,
        const Kit_connector&                      preview_connector, // the new piece's, in world space
        std::uint64_t                             preview_id
    );
    void clear_brush_join();
    [[nodiscard]] auto has_brush_join(const Scene_root& scene_root) const -> bool;

    // The cached enabled local frames of a kit piece (U33, U39); parsed again on the first read after a change of its
    // record, switches or parent.
    [[nodiscard]] auto get_local_connectors(const erhe::scene::Node& node) -> const std::vector<Kit_connector>&;
    // Whether the node is a floor layout (K6), from the same cache entry: its layout value is parsed on a cache miss only.
    [[nodiscard]] auto is_floor_layout(const erhe::scene::Node& node) -> bool;

    // The record or connector switches of the piece may have changed: its enabled local frames are read again when next drawn.
    void forget_piece(const erhe::scene::Node& node);
    void forget_node (std::uint64_t node_id);
    // The layout's connector switches, lock, children or curve may have changed (U39): its children are read again.
    void forget_children_of(std::uint64_t layout_id);
    void forget_all  ();
    void forget_removed(const Removed_items& removed);
    [[nodiscard]] auto get_cached_piece_count() const -> std::size_t;

private:
    class Cached_piece;
    [[nodiscard]] auto get_cached_piece(const erhe::scene::Node& node) -> const Cached_piece&;
    void add_piece(const erhe::scene::Node& node, std::vector<Kit_connector_indicator>& out_indicators);
    void collect_pieces(
        const Scene_root&                     scene_root,
        Visualization_mode                    mode,
        const Selection&                      selection,
        const Kit_connector_drag_snap*        drag_snap,
        std::vector<Kit_connector_indicator>& out_indicators
    );
    void add_brush_join(const Scene_root& scene_root, std::vector<Kit_connector_indicator>& out_indicators);

    class Choice
    {
    public:
        std::uint64_t node_id{0};
        std::string   connector;
    };

    const Level_kits&                                            m_level_kits;
    class Cached_piece
    {
    public:
        const erhe::Item_base*     item{nullptr}; // compared with removal sets only, never dereferenced
        std::uint64_t              parent_id{0};  // when cached
        std::vector<Kit_connector> connectors;
        // A floor layout (a Curve with a kit layout): read again when its curve's revision or its slab child changes.
        bool                       floor        {false};
        std::uint64_t              revision     {0};
        std::uint64_t              first_child_id{0};
    };
    std::unordered_map<std::uint64_t, Cached_piece>              m_local_connectors; // by node id
    std::vector<Kit_connector>                                   m_world_connectors;
    std::vector<std::uint64_t>                                   m_removed_parent_ids; // forget_removed() scratch
    Choice                                                       m_target_choice;
    Choice                                                       m_moved_choice;
    class Brush_join
    {
    public:
        const Scene_root*                scene_root{nullptr}; // compared only, never dereferenced
        std::weak_ptr<erhe::scene::Node> piece;
        const char*                      connector {""};      // a shape's literal name
        Kit_connector                    preview_connector;
        std::uint64_t                    preview_id{0};
    };
    Brush_join                                                   m_brush_join;
};

// The style color of an indicator of the role.
[[nodiscard]] auto get_connector_indicator_color(const Debug_visualizations_style& style, Kit_connector_indicator_role role, Kit_connector_types types) -> const glm::vec4&;

// The reach, in snap distances, within which a drag shows the target connectors around its moved ones.
constexpr float c_connector_indicator_reach = 4.0f;

} // namespace editor
