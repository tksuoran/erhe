#pragma once

#include "level/kit_connector.hpp"

#include "erhe_scene/node.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace editor {

class Level_kits;
class Scene_root;

// The pair the last step of the drag snapped by (D3.13); snapped is false when that step stayed unsnapped. Kept after the drag ends.
class Kit_connector_snap_state
{
public:
    bool                             snapped         {false};
    float                            distance        {0.0f}; // of the pair in the unsnapped pose
    glm::vec3                        offset          {0.0f}; // added to the moved set's translation
    std::weak_ptr<erhe::scene::Node> piece;
    const char*                      connector       {""};
    std::weak_ptr<erhe::scene::Node> target;
    const char*                      target_connector{""};
};

// D3.13: snap to connectors during a translate drag; gathers at drag begin, then each step only reads world transforms.
class Kit_connector_drag_snap
{
public:
    // moved_roots: the nodes the step writes; excluded: every node of the drag (their subtrees are no targets).
    void begin(
        const Level_kits&                                   level_kits,
        const Scene_root&                                   scene_root,
        std::span<const std::shared_ptr<erhe::scene::Node>> moved_roots,
        std::span<const std::shared_ptr<erhe::scene::Node>> excluded,
        float                                               max_distance
    );
    void end();
    // end() and forgets the last step's pair.
    void clear();
    [[nodiscard]] auto is_active() const -> bool;
    // The scene of the active drag; null when none is active.
    [[nodiscard]] auto get_scene_root() const -> const Scene_root*;

    // The offset co-locating the closest pair of the moved roots' current (unsnapped) pose; nullopt when none is in reach.
    auto step() -> std::optional<glm::vec3>;
    // The caller could not apply the last step's offset: that step reports unsnapped.
    void reject_step();

    // Appends, at the current world transforms, the moved connectors and the targets within reach_factor times the
    // distance of one (the last step's pair as snapped); nothing when the drag is not active. No allocation at capacity.
    void collect_indicators(float reach_factor, std::vector<Kit_connector_indicator>& out_indicators) const;

    [[nodiscard]] auto get_state       () const -> const Kit_connector_snap_state&;
    [[nodiscard]] auto get_moved_count () const -> std::size_t;
    [[nodiscard]] auto get_target_count() const -> std::size_t;

private:
    class Moved_point
    {
    public:
        std::size_t                      root{0};
        glm::vec3                        position_in_root{0.0f};
        glm::vec3                        outward_in_root {0.0f}; // as a normal under the root's frame, not normalized
        glm::vec3                        up_in_root      {0.0f};
        std::weak_ptr<erhe::scene::Node> piece;
        std::uint64_t                    piece_id{0};
        std::size_t                      connector_index{0};
        const char*                      name{""};
    };
    class Target_piece
    {
    public:
        std::weak_ptr<erhe::scene::Node> node;
        std::uint64_t                    piece_id   {0};
        std::size_t                      first_point{0};
        std::size_t                      point_count{0};
    };
    class Target_point
    {
    public:
        glm::vec3   local_position{0.0f};
        glm::vec3   local_outward {0.0f, 0.0f, 1.0f};
        glm::vec3   local_up      {0.0f, 1.0f, 0.0f};
        std::size_t piece{0}; // in m_target_pieces
        std::size_t connector_index{0};
        const char* name{""};
    };

    bool                                          m_active      {false};
    const Scene_root*                             m_scene_root  {nullptr};
    float                                         m_max_distance{0.0f};
    std::vector<std::weak_ptr<erhe::scene::Node>> m_roots;
    std::vector<Moved_point>                      m_moved;
    std::vector<Target_piece>                     m_target_pieces;
    std::vector<Target_point>                     m_target_points;
    // Per step: world positions, and per world point its m_moved / m_target_points index.
    std::vector<Kit_connector_point>              m_moved_world;
    std::vector<std::size_t>                      m_moved_world_source;
    std::vector<Kit_connector_point>              m_target_world;
    std::vector<std::size_t>                      m_target_world_source;
    std::vector<Kit_connector>                    m_local_connectors;
    std::vector<Kit_connector>                    m_world_connectors;
    Kit_connector_snap_state                      m_state;
    std::optional<std::size_t>                    m_snapped_moved;  // in m_moved, of the last step's pair
    std::optional<std::size_t>                    m_snapped_target; // in m_target_points
};

} // namespace editor
