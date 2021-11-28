#include "erhe/raytrace/null/null_geometry.hpp"
#include "erhe/raytrace/null/null_scene.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(IScene* scene) -> IGeometry*
{
    return new Null_geometry(scene);
}

auto IGeometry::create_shared(IScene* scene) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Null_geometry>(scene);
}

Null_geometry::Null_geometry(IScene* scene)
{
    m_scene = reinterpret_cast<Null_scene*>(scene);
}

Null_geometry::~Null_geometry()
{
}

void Null_geometry::set_transform(const glm::mat4 transform)
{
    m_transform = transform;
}

} // namespace erhe::raytrace
