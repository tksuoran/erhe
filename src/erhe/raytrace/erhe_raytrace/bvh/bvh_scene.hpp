#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/iscene.hpp"
#include "erhe_math/aabb.hpp"

#include <bvh/v2/bvh.h>

#include <cstdint>
#include <string>
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

    [[nodiscard]] auto get_bbox() const -> erhe::math::Aabb;
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
    auto intersect_children(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool;

    [[nodiscard]] auto find_child(const Bvh_geometry* geometry) -> std::vector<Bvh_scene_child>::iterator;
    [[nodiscard]] auto find_child(const Bvh_instance* instance) -> std::vector<Bvh_scene_child>::iterator;

    // Marks the bounds of this scene as changed and propagates that to the
    // instances which instantiate this scene.
    void mark_modified();

    std::vector<Bvh_scene_child> m_children;
    std::vector<Bvh_instance*>   m_referencing_instances;
    std::string                  m_debug_label;
    uint64_t                     m_tick{0};

    // Guard against infinite recursion when the scene graph contains a cycle.
    mutable bool                 m_in_get_bbox   {false};
    bool                         m_in_propagation{false};
};

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
