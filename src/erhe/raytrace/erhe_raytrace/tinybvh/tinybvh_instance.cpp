#include "erhe_raytrace/tinybvh/tinybvh_instance.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_scene.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::raytrace {

auto IInstance::create(const std::string_view debug_label) -> IInstance*
{
    return new Tinybvh_instance(debug_label);
}

auto IInstance::create_shared(const std::string_view debug_label) -> std::shared_ptr<IInstance>
{
    return std::make_shared<Tinybvh_instance>(debug_label);
}

auto IInstance::create_unique(const std::string_view debug_label) -> std::unique_ptr<IInstance>
{
    return std::make_unique<Tinybvh_instance>(debug_label);
}

Tinybvh_instance::Tinybvh_instance(const std::string_view debug_label)
    : m_debug_label(debug_label)
{
    log_instance->trace("Created Tinybvh_instance {}", debug_label);
}

Tinybvh_instance::~Tinybvh_instance() noexcept
{
    log_instance->trace("Destroyed Tinybvh_instance {}", m_debug_label);
}

void Tinybvh_instance::commit()
{
}

void Tinybvh_instance::enable()
{
    log_instance->trace("Tinybvh_instance::enable {}", m_debug_label);
    m_enabled = true;
}

void Tinybvh_instance::disable()
{
    log_instance->trace("Tinybvh_instance::disable {}", m_debug_label);
    m_enabled = false;
}

auto Tinybvh_instance::is_enabled() const -> bool
{
    return m_enabled;
}

void Tinybvh_instance::set_transform(const glm::mat4 transform)
{
    m_transform = transform;
}

void Tinybvh_instance::set_scene(IScene* scene)
{
    m_scene = scene;
}

void Tinybvh_instance::set_mask(const uint32_t mask)
{
    log_instance->trace("Tinybvh_instance::set_mask(mask = {:04x}) {}", mask, m_debug_label);
    m_mask = mask;
}

void Tinybvh_instance::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Tinybvh_instance::intersect(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_enabled) {
        return false;
    }
    if ((ray.mask & m_mask) == 0) {
        return false;
    }

    const glm::mat4 transform         = get_transform();
    const glm::mat4 inverse_transform = glm::inverse(transform);
    Ray             local_ray         = ray.transform(inverse_transform);
    IScene*         instance_scene    = get_scene();
    Tinybvh_scene*  tinybvh_scene     = static_cast<Tinybvh_scene*>(instance_scene);
    const bool      is_hit            = tinybvh_scene->intersect_instance(local_ray, hit, this);
    ray.t_far = local_ray.t_far;
    return is_hit;
}

auto Tinybvh_instance::get_transform() const -> glm::mat4
{
    return m_transform;
}

auto Tinybvh_instance::get_scene() const -> IScene*
{
    return m_scene;
}

auto Tinybvh_instance::get_mask() const -> uint32_t
{
    return m_mask;
}

auto Tinybvh_instance::get_user_data() const -> void*
{
    return m_user_data;
}

auto Tinybvh_instance::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
