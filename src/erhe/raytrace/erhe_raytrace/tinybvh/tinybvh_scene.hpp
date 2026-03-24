#pragma once

#include "erhe_raytrace/iscene.hpp"

#include <string>
#include <vector>

namespace erhe::raytrace {

class Tinybvh_geometry;
class Tinybvh_instance;
class IGeometry;

class Tinybvh_scene : public IScene
{
public:
    explicit Tinybvh_scene(std::string_view debug_label);
    ~Tinybvh_scene() noexcept override;

    // Implements IScene
    void attach     (IGeometry* geometry)        override;
    void attach     (IInstance* instance)        override;
    void detach     (IGeometry* geometry)        override;
    void detach     (IInstance* instance)        override;
    void commit     ()                           override;
    auto intersect  (Ray& ray, Hit& hit) -> bool override;
    auto debug_label() const -> std::string_view override;

    // Tinybvh_scene public API
    auto intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* instance) -> bool;

private:
    std::vector<Tinybvh_geometry*> m_geometries;
    std::vector<Tinybvh_instance*> m_instances;
    std::string                    m_debug_label;
};

} // namespace erhe::raytrace
