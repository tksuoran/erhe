#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_raytrace/bvh/bvh_geometry.hpp"
#include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::raytrace {

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

auto Bvh_scene_child::get_bbox() const -> erhe::math::Aabb
{
    if (geometry != nullptr) {
        return geometry->get_bbox();
    }
    if (instance != nullptr) {
        return instance->get_bbox();
    }
    return erhe::math::Aabb{};
}

Bvh_scene::Bvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    log_scene->trace("Created Bvh_scene '{}'", debug_label);
}

Bvh_scene::~Bvh_scene() noexcept
{
    log_scene->trace("Destroyed Bvh_scene '{}'", m_debug_label);
}

void Bvh_scene::attach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} attach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

#ifndef NDEBUG
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [bvh_geometry](const Bvh_scene_child& child) { return child.geometry == bvh_geometry; }
    );
    if (i != m_children.end()) {
        log_scene->error("raytrace geometry already in scene");
        return;
    }
#endif

    m_children.push_back(Bvh_scene_child{.geometry = bvh_geometry, .instance = nullptr});
}

void Bvh_scene::attach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} attach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

#ifndef NDEBUG
    const auto i = std::find_if(
        m_children.begin(),
        m_children.end(),
        [bvh_instance](const Bvh_scene_child& child) { return child.instance == bvh_instance; }
    );
    if (i != m_children.end()) {
        log_scene->error("raytrace instance already in scene");
        return;
    }
#endif

    m_children.push_back(Bvh_scene_child{.geometry = nullptr, .instance = bvh_instance});
}

void Bvh_scene::detach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} detach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

    const auto i = std::remove_if(
        m_children.begin(),
        m_children.end(),
        [bvh_geometry](const Bvh_scene_child& child) { return child.geometry == bvh_geometry; }
    );
    if (i == m_children.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_children.erase(i, m_children.end());
    }
}

void Bvh_scene::detach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} detach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

    const auto i = std::remove_if(
        m_children.begin(),
        m_children.end(),
        [bvh_instance](const Bvh_scene_child& child) { return child.instance == bvh_instance; }
    );
    if (i == m_children.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_children.erase(i, m_children.end());
    }
}

void Bvh_scene::commit()
{
}

auto Bvh_scene::get_bbox() const -> erhe::math::Aabb
{
    if (m_in_get_bbox) {
        return erhe::math::Aabb{};
    }
    m_in_get_bbox = true;
    erhe::math::Aabb bbox{};
    for (const Bvh_scene_child& child : m_children) {
        const erhe::math::Aabb child_bbox = child.get_bbox();
        if (child_bbox.is_valid()) {
            bbox.include(child_bbox);
        }
    }
    m_in_get_bbox = false;
    return bbox;
}

auto Bvh_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    // log_frame->trace(
    //     "Bvh_scene {} intersect mask = {:04x} children = {}, ray origin = {}, direction = {}",
    //     m_debug_label, ray.mask, m_children.size(), ray.origin, ray.direction
    // );

    ERHE_PROFILE_FUNCTION();

    return intersect_children(ray, hit, nullptr);
}

auto Bvh_scene::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    return intersect_children(ray, hit, in_instance);
}

auto Bvh_scene::intersect_children(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    bool is_hit = false;
    for (const Bvh_scene_child& child : m_children) {
        const bool child_is_hit = (child.instance != nullptr)
            ? child.instance->intersect(ray, hit)
            : child.geometry->intersect_instance(ray, hit, in_instance);
        if (child_is_hit) {
            is_hit = true;
        }
    }
    return is_hit;
}

auto Bvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
