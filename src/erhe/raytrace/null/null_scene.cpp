#include "erhe/raytrace/null/null_scene.hpp"
#include "erhe/raytrace/null/null_geometry.hpp"
#include "erhe/raytrace/log.hpp"

namespace erhe::raytrace
{


auto IScene::create() -> IScene*
{
    return new Null_scene();
}

auto IScene::create_shared() -> std::shared_ptr<IScene>
{
    return std::make_shared<Null_scene>();
}

auto IScene::create_unique() -> std::unique_ptr<IScene>
{
    return std::make_unique<Null_scene>();
}

Null_scene::Null_scene()
{
}

Null_scene::~Null_scene()
{
}

void Null_scene::attach(IGeometry* geometry)
{
#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), geometry);
    if (i != m_geometries.end())
    {
        log_scene.error("raytrace geometry already in scene\n");
    }
    else
#endif
    {
        m_geometries.push_back(geometry);
    }
}

void Null_scene::detach(IGeometry* geometry)
{
    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), geometry);
    if (i == m_geometries.end()) 
    {
        log_scene.error("raytrace geometry not in scene\n");
    }
    else
    {
        m_geometries.erase(i, m_geometries.end());
    }
}

}