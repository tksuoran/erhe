#pragma once

#include "erhe_raytrace/iscene.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::raytrace {

class Tinybvh_geometry;
class Tinybvh_instance;
class IGeometry;

class Tlas_data;

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

    // TLAS configuration
    static bool s_use_tlas;

private:
    auto intersect_linear(Ray& ray, Hit& hit) -> bool;
    auto intersect_tlas  (Ray& ray, Hit& hit) -> bool;
    void build_tlas();

    std::vector<Tinybvh_geometry*> m_geometries;
    std::vector<Tinybvh_instance*> m_instances;
    std::string                    m_debug_label;

    // TLAS data (opaque to avoid tinybvh types in header)
    std::unique_ptr<Tlas_data> m_tlas_data;
    bool                       m_tlas_valid{false};
};

} // namespace erhe::raytrace
