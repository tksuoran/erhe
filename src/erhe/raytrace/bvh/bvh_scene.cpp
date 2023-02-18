#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe/raytrace/bvh/bvh_scene.hpp"
#include "erhe/raytrace/bvh/bvh_geometry.hpp"
#include "erhe/raytrace/bvh/bvh_instance.hpp"
#include "erhe/raytrace/bvh/glm_conversions.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/raytrace_log.hpp"
#include "erhe/raytrace/ray.hpp"

#include <bvh/v2/default_builder.h>

namespace erhe::raytrace
{


auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Bvh_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Bvh_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Bvh_scene>(debug_label);
}

Bvh_scene::Bvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
}

Bvh_scene::~Bvh_scene() noexcept = default;

void Bvh_scene::attach(IGeometry* geometry)
{
    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), bvh_geometry);
    if (i != m_geometries.end()) {
        log_scene->error("raytrace geometry already in scene");
    } else
#endif
    {
        m_geometries.push_back(bvh_geometry);
    }
}

void Bvh_scene::attach(IInstance* instance)
{
    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

#ifndef NDEBUG
    const auto i = std::find(m_instances.begin(), m_instances.end(), bvh_instance);
    if (i != m_instances.end()) {
        log_scene->error("raytrace instance already in scene");
    } else
#endif
    {
        m_instances.push_back(bvh_instance);
    }
}

void Bvh_scene::detach(IGeometry* geometry)
{
    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), bvh_geometry);
    if (i == m_geometries.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_geometries.erase(i, m_geometries.end());
    }
}

void Bvh_scene::detach(IInstance* instance)
{
    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

    const auto i = std::remove(m_instances.begin(), m_instances.end(), bvh_instance);
    if (i == m_instances.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_instances.erase(i, m_instances.end());
    }
}

void Bvh_scene::commit()
{
    // WIP - not really used
#if 0
    m_collected_spheres.clear();
    m_collected_instances.clear();

    collect_spheres(m_collected_spheres, m_collected_instances, nullptr);

    const auto [bboxes, centers] = bvh::compute_bounding_boxes_and_centers<bvh::Sphere<float>>(
        m_collected_spheres.data(),
        m_collected_spheres.size()
    );
    const auto global_bbox = bvh::compute_bounding_boxes_union<float>(
        bboxes.get(),
        m_collected_spheres.size()
    );

    // Create an acceleration data structure on the primitives
    bvh::SweepSahBuilder<bvh::Bvh<float>> builder{m_bvh};
    builder.build(
        global_bbox,
        bboxes.get(),
        centers.get(),
        m_collected_spheres.size()
    );
#endif
}

void Bvh_scene::intersect(Ray& ray, Hit& hit)
{
    for (const auto& instance : m_instances) {
        instance->intersect(ray, hit);
    }
    for (const auto& geometry : m_geometries) {
        geometry->intersect_instance(ray, hit, nullptr);
    }
}

void Bvh_scene::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* in_instance)
{
    if (in_instance == nullptr) {
        for (const auto& instance : m_instances) {
            instance->intersect(ray, hit);
        }
    } else {
        for (const auto& geometry : m_geometries) {
            geometry->intersect_instance(ray, hit, in_instance);
        }
    }
}

// WIP - not ready / used
#if 0
void Bvh_scene::collect_spheres(
    std::vector<bvh::Sphere<float>>& spheres,
    std::vector<Bvh_instance*>&      instances,
    Bvh_instance*                    in_instance
)
{
    if (in_instance == nullptr) {
        for (const auto& instance : m_instances) {
            instance->collect_spheres(spheres, instances);
        }
    } else {
        const auto transform = in_instance->get_transform();
        for (const auto& geometry : m_geometries) {
            const auto& geometry_sphere = geometry->get_sphere();
            const auto  origin          = glm::vec3{transform * glm::vec4{geometry_sphere.center, 1.0f}};
            spheres.emplace_back(
                to_bvh(origin),
                geometry_sphere.radius
            );
            instances.emplace_back(in_instance);
        }
    }
}
#endif

auto Bvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
