#pragma once

#include "level/kit_connector.hpp"
#include "config/generated/connector_snap_orientation.hpp"

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

// Why a step of an active drag did not look for a pair (U27).
enum class Kit_connector_snap_skip : unsigned int {
    none = 0,
    before_motion, // the pointer has not moved since the press
    suspended      // the suspend key is held
};

// U44: whether the last step's snap turned the moved set (aligned) or, asked to, translated it only and why.
enum class Kit_connector_snap_alignment : unsigned int {
    off = 0,   // not asked: Connector_snap_orientation::keep, or a rotate or scale drag
    aligned,
    on_axis,   // a stay_on_axis handle drag: the constrained offset only
    locked,    // a moved root's locked translate or rotate channel would hold back the motion
    shear,     // a moved root's resulting local transform would be sheared (a non-uniformly scaled parent, or its own skew)
    degenerate // the pair's frame degenerated since the gather (a target scaled to zero)
};

// The pair the last step of the drag snapped by (D3.13); snapped is false when that step stayed unsnapped. Kept after the drag ends.
class Kit_connector_snap_state
{
public:
    bool                             snapped         {false};
    Kit_connector_snap_skip          skip            {Kit_connector_snap_skip::none};
    Kit_connector_snap_constraint    constraint;
    float                            distance        {0.0f}; // of the pair in the unsnapped pose
    glm::vec3                        full_offset     {0.0f}; // puts the moved connector on the target's
    glm::vec3                        offset          {0.0f}; // added to the moved set's translation (full_offset constrained)
    Kit_connector_snap_alignment     alignment       {Kit_connector_snap_alignment::off};
    Kit_join_pose                    motion;                  // aligned: the rigid world motion of the moved set
    int                              twist_steps     {0};     // aligned: the quarter twist of the join (closest_join_twist())
    std::weak_ptr<erhe::scene::Node> piece;
    const char*                      connector       {""};
    std::weak_ptr<erhe::scene::Node> target;
    const char*                      target_connector{""};
};

// D3.13: snap to connectors during a translate, rotate or scale drag; gathers at drag begin, then each step only reads world transforms.
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

    // The offset co-locating the closest pair of the moved roots' current (unsnapped) pose, constrained to the dragged
    // axis or plane (U27); nullopt when none is in reach. A root whose frame degenerates contributes no connector.
    // With align and a free constraint the state's motion also turns the pair's frames to face (U44).
    auto step(const Kit_connector_snap_constraint& constraint, Connector_snap_orientation orientation) -> std::optional<glm::vec3>;
    // The caller cannot apply the last step's motion: the step snaps by its translation only, for the reason given.
    void drop_alignment(Kit_connector_snap_alignment reason);
    // The caller could not apply the last step's offset: that step reports unsnapped.
    void reject_step();
    // The step does not snap and looks for no pair, for the reason given.
    void skip_step(Kit_connector_snap_skip reason);

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

    // U44: the state's motion turning the step's pair to face, unless the constraint is not free.
    void align_step(const Moved_point& moved, const Target_point& target);

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
