#include "scene/node_raytrace.hpp"

#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh_geometry.h>

#include <glm/gtx/matrix_operation.hpp>

namespace editor {

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::Node_attachment;
using erhe::Item_flags;

auto raytrace_node_mask(erhe::Item_base& item) -> uint32_t
{
    uint32_t result{0};
    const uint64_t flags = item.get_flag_bits();
    if ((flags & Item_flags::content     ) != 0) result |= Raytrace_node_mask::content     ;
    if ((flags & Item_flags::shadow_cast ) != 0) result |= Raytrace_node_mask::shadow_cast ;
    if ((flags & Item_flags::tool        ) != 0) result |= Raytrace_node_mask::tool        ;
    if ((flags & Item_flags::brush       ) != 0) result |= Raytrace_node_mask::brush       ;
    if ((flags & Item_flags::rendertarget) != 0) result |= Raytrace_node_mask::rendertarget;
    if ((flags & Item_flags::controller  ) != 0) result |= Raytrace_node_mask::controller  ;
    return result;
}


auto get_hit_node(const erhe::raytrace::Hit& hit) -> erhe::scene::Node*
{
    if ((hit.geometry == nullptr) || (hit.instance == nullptr)) {
        return nullptr;
    }

    void* const user_data          = hit.instance->get_user_data();
    auto* const raytrace_primitive = static_cast<erhe::scene::Raytrace_primitive*>(user_data);
    if (raytrace_primitive == nullptr) {
        log_raytrace->error("This should not happen");
        return nullptr;
    }

    auto* mesh = raytrace_primitive->mesh;
    return mesh->get_node();
}

auto get_hit_normal(const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>
{
    if ((hit.geometry == nullptr) || (hit.instance == nullptr)) {
        return {};
    }

    void* const user_data          = hit.instance->get_user_data();
    auto* const raytrace_primitive = static_cast<erhe::scene::Raytrace_primitive*>(user_data);
    ERHE_VERIFY(raytrace_primitive != nullptr);
    auto* mesh = raytrace_primitive->mesh;
    ERHE_VERIFY(mesh != nullptr);
    auto* node = mesh->get_node();
    ERHE_VERIFY(node != nullptr);
    const auto& mesh_primitives = mesh->get_primitives();
    ERHE_VERIFY(raytrace_primitive->primitive_index < mesh_primitives.size());
    const erhe::primitive::Primitive& primitive = mesh_primitives[raytrace_primitive->primitive_index];

    using namespace erhe::primitive;
    const std::shared_ptr<Primitive_shape> shape = primitive.get_shape_for_raytrace();
    ERHE_VERIFY(shape);
    const GEO::index_t facet = shape->get_mesh_facet_from_triangle(hit.triangle_id);
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
    if (!geometry) {
        return hit.normal;
    }
    const GEO::Mesh& geo_mesh               = geometry->get_mesh();
    const GEO::vec3f facet_normal           = GEO::normalize(GEO::vec3f{GEO::Geom::mesh_facet_normal(geo_mesh, facet)});
    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
    const glm::mat4  world_from_node        = node->world_from_node();
    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
    return glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
}

void draw_ray_hit(
    erhe::renderer::Scoped_line_renderer& line_renderer,
    const erhe::raytrace::Ray&            ray,
    const erhe::raytrace::Hit&            hit,
    const Ray_hit_style&                  style
)
{
    erhe::scene::Node* node = get_hit_node(hit);
    if (node == nullptr) {
        return;
    }

    const auto local_normal_opt = get_hit_normal(hit);
    if (!local_normal_opt.has_value()) {
        return;
    }

    const glm::vec3 position        = ray.origin + ray.t_far * ray.direction;
    const glm::vec3 local_normal    = local_normal_opt.value();
    const glm::mat4 world_from_node = node->world_from_node();
    const glm::vec3 N{world_from_node * glm::vec4{local_normal, 0.0f}};
    const glm::vec3 T = erhe::math::safe_normalize_cross<float>(N, ray.direction);
    const glm::vec3 B = erhe::math::safe_normalize_cross<float>(T, N);

    line_renderer.set_thickness(style.hit_thickness);
    line_renderer.add_lines(
        style.hit_color,
        {
            {
                position + 0.01f * N - style.hit_size * T,
                position + 0.01f * N + style.hit_size * T
            },
            {
                position + 0.01f * N - style.hit_size * B,
                position + 0.01f * N + style.hit_size * B
            },
            {
                position,
                position + style.hit_size * N
            }
        }
    );
    line_renderer.set_thickness(style.ray_thickness);
    line_renderer.add_lines(
        style.ray_color,
        {
            {
                position,
                position - style.ray_length * ray.direction
            }
        }
    );
}

auto project_ray(
    erhe::raytrace::IScene* const raytrace_scene,
    erhe::scene::Mesh*            ignore_mesh,
    erhe::raytrace::Ray&          ray,
    erhe::raytrace::Hit&          hit
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    bool stored_visibility_state{false};
    if (ignore_mesh != nullptr) {
        stored_visibility_state = ignore_mesh->is_visible();
        ignore_mesh->hide();
    }
    ERHE_DEFER(
        if (ignore_mesh != nullptr) {
            ignore_mesh->set_visible(stored_visibility_state);
        }
    );

    raytrace_scene->intersect(ray, hit);
    return hit.instance != nullptr;
}

}
