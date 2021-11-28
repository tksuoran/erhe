#pragma once

#include "erhe/raytrace/igeometry.hpp"

#include <glm/glm.hpp>

namespace erhe::raytrace
{

class Null_scene;

class Null_geometry
    : public IGeometry
{
public:
    Null_geometry(IScene* scene);
    ~Null_geometry() override;

    // Implements IGeometry
    void set_transform(const glm::mat4 transform) override;

private:
    glm::mat4   m_transform{1.0f};
    Null_scene* m_scene{nullptr};
};

}