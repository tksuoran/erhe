#include "erhe/raytrace/bvh/bvh_instance.hpp"
#include "erhe/raytrace/bvh/bvh_scene.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::raytrace
{

auto IInstance::create(const std::string_view debug_label) -> IInstance*
{
    return new Bvh_instance(debug_label);
}

auto IInstance::create_shared(const std::string_view debug_label) -> std::shared_ptr<IInstance>
{
    return std::make_shared<Bvh_instance>(debug_label);
}

auto IInstance::create_unique(const std::string_view debug_label) -> std::unique_ptr<IInstance>
{
    return std::make_unique<Bvh_instance>(debug_label);
}

Bvh_instance::Bvh_instance(const std::string_view debug_label)
    : m_debug_label(debug_label)
{
}

Bvh_instance::~Bvh_instance() noexcept
{
}

void Bvh_instance::commit()
{
}

void Bvh_instance::enable()
{
    m_enabled = true;
}

void Bvh_instance::disable()
{
    m_enabled = false;
}

auto Bvh_instance::is_enabled() const -> bool
{
    return m_enabled;
}

void Bvh_instance::set_transform(const glm::mat4 transform)
{
    m_transform = transform;
}

void Bvh_instance::set_scene(IScene* scene)
{
    m_scene = scene;
}

void Bvh_instance::set_mask(const uint32_t mask)
{
    m_mask = mask;
}

void Bvh_instance::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

void Bvh_instance::intersect(Ray& ray, Hit& hit)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return;
    }
    if ((ray.mask & m_mask) == 0) {
        return;
    }

    const auto transform         = get_transform();
    const auto inverse_transform = glm::inverse(transform);
    Ray        local_ray         = ray.transform(inverse_transform);
    auto*      instance_scene    = get_scene();
    auto*      bvh_scene         = reinterpret_cast<Bvh_scene*>(instance_scene);
    bvh_scene->intersect_instance(local_ray, hit, this);
    ray.t_far = local_ray.t_far;
}

#if 0
void Bvh_instance::collect_spheres(
    std::vector<bvh::Sphere<float>>& spheres,
    std::vector<Bvh_instance*>&      instances
)
{
    auto* instance_scene = get_scene();
    auto* bvh_scene      = reinterpret_cast<Bvh_scene*>(instance_scene);
    bvh_scene->collect_spheres(spheres, instances, this);
}
#endif

auto Bvh_instance::get_transform() const -> glm::mat4
{
    return m_transform;
}

auto Bvh_instance::get_scene() const -> IScene*
{
    return m_scene;
}

auto Bvh_instance::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Bvh_instance::get_user_data() const -> void*
{
    return m_user_data;
}

auto Bvh_instance::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
