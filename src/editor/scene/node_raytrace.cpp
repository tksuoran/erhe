#include "scene/node_raytrace.hpp"

#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_operation.hpp>

#include <algorithm>
#include <cmath>

using erhe::geometry::mesh_facet_normalf;
using erhe::geometry::to_glm_vec3;

namespace editor {

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::Node_attachment;
using erhe::Item_flags;

auto raytrace_node_mask(erhe::Item_base& item) -> uint32_t
{
    uint32_t result{0};
    const uint64_t flags = item.get_flag_bits();
    // Render proxies (lightmap piece meshes) are never raytrace-pickable:
    // mask 0 = unhittable, so rays pass through to the proxy_hidden source
    // mesh they stand in for (same pattern as bone proxies outside bone
    // mode; see tools/notes.md).
    if ((flags & Item_flags::render_proxy) != 0) {
        return 0;
    }
    if ((flags & Item_flags::content     ) != 0) result |= Raytrace_node_mask::content     ;
    if ((flags & Item_flags::shadow_cast ) != 0) result |= Raytrace_node_mask::shadow_cast ;
    if ((flags & Item_flags::tool        ) != 0) result |= Raytrace_node_mask::tool        ;
    if ((flags & Item_flags::brush       ) != 0) result |= Raytrace_node_mask::brush       ;
    if ((flags & Item_flags::rendertarget) != 0) result |= Raytrace_node_mask::rendertarget;
    if ((flags & Item_flags::controller  ) != 0) result |= Raytrace_node_mask::controller  ;
    // Bone proxies carry only this bit (never a role bit), so they are picked
    // exclusively by rays that ask for bones. Bone_visualization clears the flag
    // outside bone selection mode, which drops the mask to 0 and makes the
    // instance unhittable - clicks then pass through to the mesh as before.
    if ((flags & Item_flags::bone_proxy  ) != 0) result |= Raytrace_node_mask::bone        ;
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
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives.at(raytrace_primitive->primitive_index);
    const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();

    using namespace erhe::primitive;
    const std::shared_ptr<Primitive_shape> shape = primitive.get_shape_for_raytrace();
    ERHE_VERIFY(shape);
    const GEO::index_t facet = shape->get_mesh_facet_from_triangle(hit.geometry, hit.triangle_id);
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
    if (!geometry) {
        return hit.normal;
    }
    const GEO::Mesh& geo_mesh               = geometry->get_mesh();
    const GEO::vec3f facet_normal           = GEO::normalize(mesh_facet_normalf(geo_mesh, facet));
    const glm::vec3  local_normal           = to_glm_vec3(facet_normal);
    const glm::mat4  world_from_node        = node->world_from_node();
    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
    return glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
}

void draw_ray_hit(
    erhe::renderer::Primitive_renderer& line_renderer,
    const erhe::raytrace::Ray&          ray,
    const erhe::raytrace::Hit&          hit,
    const Ray_hit_style&                style,
    erhe::renderer::Primitive_renderer* ray_line_renderer
)
{
    // Null-node guard stays ahead of get_hit_normal(), which VERIFYs the node
    // instead of returning empty.
    if (get_hit_node(hit) == nullptr) {
        return;
    }

    // get_hit_normal() returns a WORLD-space normal (it applies the adjugate
    // transform itself); only normalize here - the adjugate does not preserve
    // length under scale, and N's length is used for the marker offsets below.
    const auto world_normal_opt = get_hit_normal(hit);
    if (!world_normal_opt.has_value()) {
        return;
    }

    const glm::vec3 position = ray.origin + ray.t_far * ray.direction;
    const glm::vec3 N = glm::normalize(world_normal_opt.value());
    // The marker cross prefers to align with the ray, but a ray parallel to
    // the normal (straight down onto a floor) makes that cross degenerate -
    // fall back to an arbitrary tangent so the hit marker never vanishes.
    glm::vec3 T = erhe::math::safe_normalize_cross<float>(N, ray.direction);
    if (glm::dot(T, T) < 1e-6f) {
        const glm::vec3 arbitrary = (std::abs(N.x) < 0.9f) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
        T = erhe::math::safe_normalize_cross<float>(N, arbitrary);
    }
    const glm::vec3 B = erhe::math::safe_normalize_cross<float>(T, N);

    // Tangent / bitangent marker arms: each arm is split in two equally long
    // segments - full alpha near the hit, then fading out to nothing at the
    // tip, so the cross stays crisp at the intersection without hard ends.
    const float     marker_width  = 0.5f * style.hit_thickness;
    const glm::vec3 marker_center = position + 0.01f * N;
    const glm::vec4 marker_full   = style.hit_color;
    const glm::vec4 marker_clear{marker_full.x, marker_full.y, marker_full.z, 0.0f};
    const glm::vec3 arms[4] = { T, -T, B, -B };
    for (const glm::vec3& arm : arms) {
        const glm::vec3 mid = marker_center + 0.5f * style.hit_size * arm;
        const glm::vec3 tip = marker_center + style.hit_size * arm;
        line_renderer.add_line(marker_full, marker_width, marker_center, marker_full,  marker_width, mid);
        line_renderer.add_line(marker_full, marker_width, mid,           marker_clear, marker_width, tip);
    }
    // Normal tick: same two-segment fade as the tangent / bitangent arms.
    {
        const glm::vec3 mid = position + 0.5f * style.hit_size * N;
        const glm::vec3 tip = position + style.hit_size * N;
        line_renderer.add_line(marker_full, marker_width, position, marker_full,  marker_width, mid);
        line_renderer.add_line(marker_full, marker_width, mid,      marker_clear, marker_width, tip);
    }

    // The tail from the hit back toward the ray origin never extends past the
    // origin. It is split in two equally long segments: full alpha near the
    // hit, then dimming to 0.2 - matching the constant-0.2 continuation that
    // covers any remaining span to the origin so the ray's source stays
    // readable.
    erhe::renderer::Primitive_renderer& ray_renderer = (ray_line_renderer != nullptr) ? *ray_line_renderer : line_renderer;
    const float     ray_width   = 0.5f * style.ray_thickness;
    const float     tail_length = std::min(style.ray_length, ray.t_far);
    const glm::vec3 tail_mid    = position - (0.5f * tail_length) * ray.direction;
    const glm::vec3 tail_end    = position - tail_length * ray.direction;
    const glm::vec4 ray_full    = style.ray_color;
    const glm::vec4 ray_dim{ray_full.x, ray_full.y, ray_full.z, 0.2f};
    ray_renderer.set_thickness(ray_width);
    ray_renderer.add_lines(ray_full, {{position, tail_mid}});
    ray_renderer.add_line(ray_full, ray_width, tail_mid, ray_dim, ray_width, tail_end);
    if (tail_length < ray.t_far) {
        ray_renderer.add_lines(ray_dim, {{tail_end, ray.origin}});
    }
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
