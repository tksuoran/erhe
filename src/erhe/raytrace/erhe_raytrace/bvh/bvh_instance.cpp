 #include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

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
    log_instance->trace("Created Bvh_instance {}", debug_label);
}

Bvh_instance::~Bvh_instance() noexcept
{
    log_instance->trace("Destroyed Bvh_instance {}", m_debug_label);
}

void Bvh_instance::commit()
{
}

void Bvh_instance::enable()
{
    log_instance->trace("Bvh_instance::enable {}", m_debug_label);
    m_enabled = true;
}

void Bvh_instance::disable()
{
    log_instance->trace("Bvh_instance::disable {}", m_debug_label);
    m_enabled = false;
}

auto Bvh_instance::is_enabled() const -> bool
{
    return m_enabled;
}

void Bvh_instance::set_transform(const glm::mat4 transform)
{
    //log_frame->trace("Bvh_instance::set_transform {}", m_debug_label);
    m_transform = transform;
}

void Bvh_instance::set_scene(IScene* scene)
{
    m_scene = scene;
}

void Bvh_instance::set_mask(const uint32_t mask)
{
    log_instance->trace("Bvh_instance::set_mask(mask = {:04x}) {}", mask, m_debug_label);
    m_mask = mask;
}

void Bvh_instance::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Bvh_instance::intersect(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        log_frame->trace("Bvh_instance::intersect() {}. No hit because instance is disabled.", m_debug_label);
        return false;
    }
    if ((ray.mask & m_mask) == 0) {
        log_frame->trace("Bvh_instance::intersect() {}. No hit because instance mask test failed.", m_debug_label);
        return false;
    }

    const auto transform         = get_transform();
    const auto inverse_transform = glm::inverse(transform);
    Ray        local_ray         = ray.transform(inverse_transform);
    auto*      instance_scene    = get_scene();
    auto*      bvh_scene         = reinterpret_cast<Bvh_scene*>(instance_scene);
    const bool is_hit            = bvh_scene->intersect_instance(local_ray, hit, this); // instance to scene -> depth increment
    ray.t_far = local_ray.t_far;
    log_frame->trace("Bvh_instance::intersect() {}. is_hit = {} ray.mask = {:04x} mask = {:04x}", m_debug_label, is_hit, ray.mask, m_mask);
    return is_hit;
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
