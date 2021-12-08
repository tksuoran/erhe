#pragma once

#include "erhe/raytrace/iscene.hpp"

#include <vector>

namespace erhe::raytrace
{

class IGeometry;

class Null_scene
    : public IScene
{
public:
    Null_scene(const std::string_view debug_label);
    ~Null_scene() override;

    // Implements IScene
    void attach   (IGeometry* geometry) override;
    void attach   (IInstance* instance) override;
    void detach   (IGeometry* geometry) override;
    void detach   (IInstance* geometry) override;
    void commit   ()           override;
    void intersect(Ray&, Hit&) override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

private:
    std::vector<IGeometry*> m_geometries;
    std::vector<IInstance*> m_instances;
    std::string             m_debug_label;
};

}