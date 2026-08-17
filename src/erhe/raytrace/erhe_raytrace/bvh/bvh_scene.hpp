#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/iscene.hpp"
#include "erhe_math/aabb.hpp"

#include <bvh/v2/bbox.h>
#include <bvh/v2/bvh.h>
#include <bvh/v2/node.h>
#include <bvh/v2/vec.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::raytrace {

class Bvh_geometry;
class Bvh_instance;
class IGeometry;

// One entry in the child list of Bvh_scene. Exactly one of geometry and instance is set.
class Bvh_scene_child
{
public:
    Bvh_geometry* geometry          {nullptr};
    Bvh_instance* instance          {nullptr};

    // Tick at which this child was last modified in a way that can move its
    // bounding box. Children which have not been modified for a while are
    // eligible for the scene level BVH.
    uint64_t      last_modified_tick{0};

    // Set while this child is covered by the scene level BVH. Children covered
    // by it are traversed through the BVH, the others are traversed linearly.
    bool          in_tlas           {false};

    // Set while this child is part of a scene level BVH build which is still
    // running. Modifying such a child aborts that build.
    bool          in_pending_tlas   {false};

    // Index of this child in the member list of the scene level BVH. Valid
    // while in_tlas is set.
    std::size_t   tlas_index        {0};

    [[nodiscard]] auto get_bbox() const -> erhe::math::Aabb;
};

// Entry in the member list of the scene level BVH. Exactly one of geometry and
// instance is set.
class Tlas_member
{
public:
    Bvh_geometry* geometry{nullptr};
    Bvh_instance* instance{nullptr};
};

// Scene level container for geometries and instances.
//
// Threading contract:
//  - Mutation (attach, detach, commit, and modification of attached children)
//    is single threaded.
//  - intersect() is read only. Several threads may intersect the same scene
//    concurrently, but not concurrently with mutation.
class Bvh_scene : public IScene
{
public:
    using Tlas_node = bvh::v2::Node<float, 3>;
    using Tlas      = bvh::v2::Bvh<Tlas_node>;
    using Tlas_bbox = bvh::v2::BBox<float, 3>;
    using Tlas_vec  = bvh::v2::Vec<float, 3>;

    // Number of ticks a child has to be unmodified before it is eligible for
    // the scene level BVH.
    static constexpr uint64_t    k_static_delay_ticks = 30;

    // Below this number of eligible children the linear traversal wins and no
    // scene level BVH is built. This is what keeps the one geometry scenes,
    // which the editor creates one per mesh primitive, free of any BVH.
    static constexpr std::size_t k_min_tlas_children  = 4;

    // Minimum number of ticks between two scene level BVH builds. Evicted
    // members are traversed linearly until the next build, so this only trades
    // traversal quality for build cost; it never affects results.
    static constexpr uint64_t    k_rebuild_cooldown_ticks = 30;

    explicit Bvh_scene(std::string_view debug_label);
    ~Bvh_scene() noexcept override;

    // Implements IScene
    void attach     (IGeometry* geometry)        override;
    void attach     (IInstance* instance)        override;
    void detach     (IGeometry* geometry)        override;
    void detach     (IInstance* geometry)        override;
    void commit     ()                           override;
    auto intersect  (Ray& ray, Hit& hit) -> bool override;
    auto debug_label() const -> std::string_view override;

    // Bvh_scene public API
    auto intersect_instance(Ray& ray, Hit& hit, Bvh_instance* instance) -> bool;

    // Bounding box of all children, in the space of this scene.
    [[nodiscard]] auto get_bbox() const -> erhe::math::Aabb;

    // Advanced once per commit(). commit() is called once per view per frame,
    // so a tick is a frame in practice.
    [[nodiscard]] auto get_tick() const -> uint64_t;

    // Number of children which have not been modified for at least delay_ticks.
    [[nodiscard]] auto get_static_child_count(uint64_t delay_ticks) const -> std::size_t;

    // Number of children currently covered by the scene level BVH.
    [[nodiscard]] auto get_tlas_member_count() const -> std::size_t;

    // Called by attached children when something which can move their bounding
    // box changes, and when they are destroyed while still attached.
    void on_child_modified          (const Bvh_geometry* geometry);
    void on_child_modified          (const Bvh_instance* instance);
    void on_child_destroyed         (const Bvh_geometry* geometry);
    void on_child_destroyed         (const Bvh_instance* instance);

    // Called by Bvh_instance to track which instances instantiate this scene,
    // so that modifications can be propagated to the scenes above.
    void add_referencing_instance   (Bvh_instance* instance);
    void remove_referencing_instance(Bvh_instance* instance);

private:
    // Immutable input of a scene level BVH build. Holds no scene state: the
    // child pointers are carried through as an ordering key only, so a build
    // can run without touching the scene.
    class Tlas_build_input
    {
    public:
        std::vector<Tlas_member> members;
        std::vector<Tlas_bbox>   bboxes;
        std::vector<Tlas_vec>    centers;
    };

    class Tlas_build_result
    {
    public:
        Tlas                     tlas;
        std::vector<Tlas_member> members;
    };

    // A scene level BVH build. Owned by a shared_ptr so that it outlives the
    // scene if the scene is destroyed while the build is still running: the
    // worker writes into the task, never into the scene.
    class Tlas_build_task
    {
    public:
        Tlas_build_input  input;
        Tlas_build_result result;
        std::size_t       static_child_count{0};
        std::atomic<bool> done{false};
    };

    auto intersect_children(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool;
    auto intersect_tlas    (Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool;

    // Collects the children which have been static long enough. Main thread.
    [[nodiscard]] auto make_tlas_build_input() -> Tlas_build_input;

    // Pure function of its input; safe to run on a worker thread.
    static void build_tlas(Tlas_build_task& task);

    void update_tlas       ();
    void start_tlas_build  ();
    void collect_tlas_build();
    void clear_pending_build();
    void take_tlas         (Tlas_build_result&& result);
    void evict_from_tlas   (Bvh_scene_child& child);
    void invalidate_tlas   ();

    static constexpr std::size_t npos = ~std::size_t{0};

    [[nodiscard]] auto find_child_index(const void* child) const -> std::size_t;
    [[nodiscard]] static auto get_child_key(const Bvh_scene_child& child) -> const void*;

    void erase_child(std::size_t index);

    // Marks the bounds of this scene as changed and propagates that to the
    // instances which instantiate this scene.
    void mark_modified();

    std::vector<Bvh_scene_child> m_children;

    // Child pointer to index in m_children, so that a modification of one
    // child does not cost a scan over all of them.
    std::unordered_map<const void*, std::size_t> m_child_index;

    std::vector<Bvh_instance*>   m_referencing_instances;
    std::string                  m_debug_label;
    uint64_t                     m_tick{0};

    Tlas                             m_tlas;
    std::vector<Tlas_member>         m_tlas_members;
    bool                             m_tlas_ready{false};

    // Build in flight, if any, and whether one of its members was modified
    // while it was running (in which case its result is thrown away).
    std::shared_ptr<Tlas_build_task> m_build_task;
    bool                             m_build_aborted{false};

    // Number of static children at the time the current scene level BVH was
    // built, and the number of its members which have not been evicted since.
    // A difference means children have settled, moved or been detached, and
    // the BVH is rebuilt once the cooldown has passed.
    std::size_t                  m_tlas_static_child_count{0};
    std::size_t                  m_tlas_live_member_count {0};
    uint64_t                     m_last_build_tick        {0};

    // Guard against infinite recursion when the scene graph contains a cycle.
    mutable bool                 m_in_get_bbox   {false};
    bool                         m_in_propagation{false};
};

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
