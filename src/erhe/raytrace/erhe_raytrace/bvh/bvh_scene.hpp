#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/iscene.hpp"
#include "erhe_math/aabb.hpp"

#include <bvh/v2/bvh.h>

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
    Bvh_geometry* geometry{nullptr};
    Bvh_instance* instance{nullptr};

    [[nodiscard]] auto get_bbox() const -> erhe::math::Aabb;
};

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

private:
    auto intersect_children(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool;

    std::vector<Bvh_scene_child> m_children;
    std::string                  m_debug_label;

    // Guards against infinite recursion when the scene graph contains a cycle.
    mutable bool                 m_in_get_bbox{false};
};

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
