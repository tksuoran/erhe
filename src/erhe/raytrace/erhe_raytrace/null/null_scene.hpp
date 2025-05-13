#pragma once

#include "erhe_raytrace/iscene.hpp"

#include <string>
#include <vector>

namespace erhe::raytrace
{

class IGeometry;

class Null_scene
    : public IScene
{
public:
    explicit Null_scene(const std::string_view debug_label);
    ~Null_scene() noexcept override;

    // Implements IScene
    void attach     (IGeometry* geometry)        override;
    void attach     (IInstance* instance)        override;
    void detach     (IGeometry* geometry)        override;
    void detach     (IInstance* geometry)        override;
    void commit     ()                           override;
    auto intersect  (Ray& ray, Hit& hit) -> bool override;
    auto debug_label() const -> std::string_view override;

private:
    std::vector<IGeometry*> m_geometries;
    std::vector<IInstance*> m_instances;
    std::string             m_debug_label;
};

}
