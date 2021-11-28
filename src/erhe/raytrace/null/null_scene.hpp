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
    Null_scene();
    ~Null_scene() override;

    // Implements IScene
    void attach(IGeometry* geometry) override;
    void detach(IGeometry* geometry) override;

private:
    std::vector<IGeometry*> m_geometries;
};

}