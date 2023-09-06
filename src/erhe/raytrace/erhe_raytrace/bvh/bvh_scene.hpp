#pragma once

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/iscene.hpp"

#include <bvh/v2/bvh.h>

#include <string>
#include <vector>

namespace erhe::raytrace
{

class Bvh_geometry;
class Bvh_instance;
class IGeometry;

class Bvh_scene
    : public IScene
{
public:
    explicit Bvh_scene(const std::string_view debug_label);
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

private:
    std::vector<Bvh_geometry*> m_geometries;
    std::vector<Bvh_instance*> m_instances;
    std::string                m_debug_label;
};

}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
