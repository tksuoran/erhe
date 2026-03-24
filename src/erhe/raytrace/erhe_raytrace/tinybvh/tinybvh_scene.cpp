#include "erhe_raytrace/tinybvh/tinybvh_scene.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_geometry.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_instance.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include "tiny_bvh.h"

namespace erhe::raytrace {

bool Tinybvh_scene::s_use_tlas = true;

class Tlas_data
{
public:
    tinybvh::BVH                        tlas;
    std::vector<tinybvh::BLASInstance>   instances;
    std::vector<tinybvh::BVHBase*>       blases;
    std::vector<Tinybvh_instance*>       instance_map; // TLAS instance index -> erhe instance
    std::vector<Tinybvh_geometry*>       geometry_map; // TLAS instance index -> erhe geometry
};

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Tinybvh_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Tinybvh_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Tinybvh_scene>(debug_label);
}

Tinybvh_scene::Tinybvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
    , m_tlas_data{std::make_unique<Tlas_data>()}
{
    log_scene->trace("Created Tinybvh_scene '{}'", debug_label);
}

Tinybvh_scene::~Tinybvh_scene() noexcept
{
    log_scene->trace("Destroyed Tinybvh_scene '{}'", m_debug_label);
}

void Tinybvh_scene::attach(IGeometry* geometry)
{
    log_scene->trace("Tinybvh_scene {} attach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    Tinybvh_geometry* tinybvh_geometry = static_cast<Tinybvh_geometry*>(geometry);

#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), tinybvh_geometry);
    if (i != m_geometries.end()) {
        log_scene->error("raytrace geometry already in scene");
    } else
#endif
    {
        m_geometries.push_back(tinybvh_geometry);
    }
    m_tlas_valid = false;
}

void Tinybvh_scene::attach(IInstance* instance)
{
    log_scene->trace("Tinybvh_scene {} attach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    Tinybvh_instance* tinybvh_instance = static_cast<Tinybvh_instance*>(instance);

#ifndef NDEBUG
    const auto i = std::find(m_instances.begin(), m_instances.end(), tinybvh_instance);
    if (i != m_instances.end()) {
        log_scene->error("raytrace instance already in scene");
    } else
#endif
    {
        m_instances.push_back(tinybvh_instance);
    }
    m_tlas_valid = false;
}

void Tinybvh_scene::detach(IGeometry* geometry)
{
    log_scene->trace("Tinybvh_scene {} detach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    Tinybvh_geometry* tinybvh_geometry = static_cast<Tinybvh_geometry*>(geometry);

    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), tinybvh_geometry);
    if (i == m_geometries.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_geometries.erase(i, m_geometries.end());
    }
    m_tlas_valid = false;
}

void Tinybvh_scene::detach(IInstance* instance)
{
    log_scene->trace("Tinybvh_scene {} detach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    Tinybvh_instance* tinybvh_instance = static_cast<Tinybvh_instance*>(instance);

    const auto i = std::remove(m_instances.begin(), m_instances.end(), tinybvh_instance);
    if (i == m_instances.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_instances.erase(i, m_instances.end());
    }
    m_tlas_valid = false;
}

void Tinybvh_scene::build_tlas()
{
    Tlas_data& td = *m_tlas_data;
    td.instances.clear();
    td.blases.clear();
    td.instance_map.clear();
    td.geometry_map.clear();

    // Collect TLAS instances from all erhe instances.
    // Each erhe instance points to a child scene. For each geometry in
    // that child scene, create one TLAS BLASInstance entry.
    for (Tinybvh_instance* instance : m_instances) {
        if (!instance->is_enabled()) {
            continue;
        }

        IScene* child_scene_interface = instance->get_scene();
        if (child_scene_interface == nullptr) {
            continue;
        }
        Tinybvh_scene* child_scene = static_cast<Tinybvh_scene*>(child_scene_interface);

        const glm::mat4 transform = instance->get_transform();

        for (Tinybvh_geometry* geometry : child_scene->m_geometries) {
            if (!geometry->is_enabled()) {
                continue;
            }
            tinybvh::BVH* blas = geometry->get_bvh();
            if ((blas == nullptr) || (geometry->get_triangle_count() == 0)) {
                continue;
            }

            const uint32_t blas_index = static_cast<uint32_t>(td.blases.size());
            td.blases.push_back(blas);
            td.instance_map.push_back(instance);
            td.geometry_map.push_back(geometry);

            tinybvh::BLASInstance tlas_inst{};
            // Copy transform: glm is column-major, tinybvh bvhmat4 is row-major
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    tlas_inst.transform.cell[row * 4 + col] = transform[col][row];
                }
            }
            tlas_inst.blasIdx = blas_index;
            tlas_inst.mask    = static_cast<uint32_t>(instance->get_mask() & geometry->get_mask()) & 0xFFFFu;

            // Update computes invTransform and world-space AABB from BLAS
            tlas_inst.Update(blas);

            td.instances.push_back(tlas_inst);
        }
    }

    if (!td.instances.empty()) {
        td.tlas.Build(
            td.instances.data(),
            static_cast<uint32_t>(td.instances.size()),
            td.blases.data(),
            static_cast<uint32_t>(td.blases.size())
        );
    }

    m_tlas_valid = true;
}

void Tinybvh_scene::commit()
{
    if (s_use_tlas && !m_instances.empty()) {
        build_tlas();
    }
}

auto Tinybvh_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (s_use_tlas && m_tlas_valid && !m_tlas_data->instances.empty()) {
        // Use TLAS for instances, then linear scan for direct geometries
        bool is_hit = intersect_tlas(ray, hit);
        for (Tinybvh_geometry* geometry : m_geometries) {
            const bool geometry_is_hit = geometry->intersect_instance(ray, hit, nullptr);
            if (geometry_is_hit) {
                is_hit = true;
            }
        }
        return is_hit;
    }

    return intersect_linear(ray, hit);
}

auto Tinybvh_scene::intersect_linear(Ray& ray, Hit& hit) -> bool
{
    bool is_hit = false;
    for (Tinybvh_instance* instance : m_instances) {
        const bool instance_is_hit = instance->intersect(ray, hit);
        if (instance_is_hit) {
            is_hit = true;
        }
    }
    for (Tinybvh_geometry* geometry : m_geometries) {
        const bool geometry_is_hit = geometry->intersect_instance(ray, hit, nullptr);
        if (geometry_is_hit) {
            is_hit = true;
        }
    }
    return is_hit;
}

auto Tinybvh_scene::intersect_tlas(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    Tlas_data& td = *m_tlas_data;

    tinybvh::Ray tinybvh_ray{
        tinybvh::bvhvec3{ray.origin.x,    ray.origin.y,    ray.origin.z},
        tinybvh::bvhvec3{ray.direction.x, ray.direction.y, ray.direction.z},
        ray.t_far,
        ray.mask
    };

    td.tlas.Intersect(tinybvh_ray);

    if (tinybvh_ray.hit.t < ray.t_far) {
        const uint32_t tlas_instance_index = tinybvh_ray.hit.inst;
        const uint32_t prim_id             = tinybvh_ray.hit.prim;

        if (tlas_instance_index < td.instance_map.size()) {
            Tinybvh_instance* erhe_instance = td.instance_map[tlas_instance_index];
            Tinybvh_geometry* erhe_geometry = td.geometry_map[tlas_instance_index];

            // Compute normal from triangle vertices stored in the geometry
            const glm::mat4 transform = erhe_instance->get_transform();
            const std::vector<tinybvh::bvhvec4>& triangles = erhe_geometry->get_triangles();
            const std::size_t base = static_cast<std::size_t>(prim_id) * 3;

            glm::vec3 local_normal{0.0f, 1.0f, 0.0f};
            if ((base + 2) < triangles.size()) {
                const tinybvh::bvhvec4& v0 = triangles[base + 0];
                const tinybvh::bvhvec4& v1 = triangles[base + 1];
                const tinybvh::bvhvec4& v2 = triangles[base + 2];
                const glm::vec3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                const glm::vec3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                local_normal = glm::cross(edge1, edge2);
            }

            ray.t_far       = tinybvh_ray.hit.t;
            hit.triangle_id = prim_id;
            hit.uv          = glm::vec2{tinybvh_ray.hit.u, tinybvh_ray.hit.v};
            hit.normal      = glm::vec3{transform * glm::vec4{local_normal, 0.0f}};
            hit.instance    = erhe_instance;
            hit.geometry    = erhe_geometry;
            return true;
        }
    }

    return false;
}

auto Tinybvh_scene::intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* in_instance) -> bool
{
    // intersect_instance is used for nested hierarchy traversal.
    // TLAS is only used at the top-level intersect() call.
    // Here we always use linear scan for correctness with multi-level nesting.
    bool is_hit = false;
    if (in_instance == nullptr) {
        for (Tinybvh_instance* instance : m_instances) {
            const bool instance_is_hit = instance->intersect(ray, hit);
            if (instance_is_hit) {
                is_hit = true;
            }
        }
    } else {
        // Traverse sub-instances (for multi-level nesting)
        for (Tinybvh_instance* instance : m_instances) {
            const bool instance_is_hit = instance->intersect(ray, hit);
            if (instance_is_hit) {
                is_hit = true;
            }
        }
        for (Tinybvh_geometry* geometry : m_geometries) {
            const bool geometry_is_hit = geometry->intersect_instance(ray, hit, in_instance);
            if (geometry_is_hit) {
                is_hit = true;
            }
        }
    }
    return is_hit;
}

auto Tinybvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
