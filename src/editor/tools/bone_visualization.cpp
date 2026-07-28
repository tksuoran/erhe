#include "tools/bone_visualization.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>

namespace editor {

namespace {

// Fraction of the bone length at which the octahedron's widest ring sits.
// Matches the long-standing line visualization (mid_point = mix(a, b, 0.1)).
constexpr float c_ring_position = 0.1f;

// Smallest half-width a bone proxy may have, in world units. A zero-length or
// hairline bone would otherwise collapse to a degenerate shape that cannot be
// clicked; picking has to stay possible for every joint.
constexpr float c_min_half_width = 0.002f;

// Unit bone: head at the origin, tail at +Y, square ring at y = c_ring_position
// with half-width 1 in x and z. The instance transform scales x/z by the bone
// half-width and y by the bone length, so this one geometry serves every bone -
// which is exactly what lets the raytrace side keep a single BVH and pose it per
// instance.
void make_bone(GEO::Mesh& mesh)
{
    mesh.vertices.set_double_precision();
    {
        const GEO::vec3 vertices[] = {
            { 0.0, 0.0,             0.0}, // 0 head
            { 0.0, 1.0,             0.0}, // 1 tail
            { 1.0, c_ring_position, 0.0}, // 2 +x
            { 0.0, c_ring_position, 1.0}, // 3 +z
            {-1.0, c_ring_position, 0.0}, // 4 -x
            { 0.0, c_ring_position,-1.0}  // 5 -z
        };
        const GEO::index_t vertex_count = sizeof(vertices) / sizeof(vertices[0]);
        const GEO::index_t base_vertex  = mesh.vertices.create_vertices(vertex_count);
        for (GEO::index_t i = 0; i < vertex_count; ++i) {
            mesh.vertices.point(base_vertex + i) = vertices[i];
        }
    }
    {
        // Counter-clockwise seen from outside: 4 facets from the head down to
        // the ring, 4 from the ring up to the tail.
        const std::array<GEO::index_t, 3> facets[] = {
            { 0, 3, 2 }, { 0, 4, 3 }, { 0, 5, 4 }, { 0, 2, 5 },
            { 1, 2, 3 }, { 1, 3, 4 }, { 1, 4, 5 }, { 1, 5, 2 }
        };
        const GEO::index_t facet_count = sizeof(facets) / sizeof(facets[0]);
        const GEO::index_t base_facet  = mesh.facets.create_facets(facet_count, 3);
        for (GEO::index_t i = 0; i < facet_count; ++i) {
            for (GEO::index_t j = 0; j < 3; ++j) {
                mesh.facets.set_vertex(base_facet + i, j, facets[i][j]);
            }
        }
    }
    mesh.facets.connect();
    // The builder reads points through get_pointf(); leaving the mesh in the
    // double precision used above trips a geogram assertion.
    mesh.vertices.set_single_precision();
}

// Rotation taking +Y onto `direction` (unit). Uses an arbitrary perpendicular
// when the two are antiparallel, where the axis is undefined.
[[nodiscard]] auto orient_y_to(const glm::vec3 direction) -> glm::mat4
{
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const float     d = glm::dot(up, direction);
    if (d > 0.9999f) {
        return glm::mat4{1.0f};
    }
    if (d < -0.9999f) {
        return glm::rotate(glm::mat4{1.0f}, glm::pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
    }
    const glm::vec3 axis = glm::normalize(glm::cross(up, direction));
    return glm::rotate(glm::mat4{1.0f}, std::acos(d), axis);
}

} // anonymous namespace

auto bone_tail_in_joint_space(const erhe::scene::Skin& skin, const std::size_t joint_index) -> glm::vec3
{
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = skin.skin_data.joints;
    if (joint_index >= joints.size()) {
        return glm::vec3{0.2f, 0.0f, 0.0f};
    }
    const std::shared_ptr<erhe::scene::Node>& joint = joints[joint_index];
    if (!joint) {
        return glm::vec3{0.2f, 0.0f, 0.0f};
    }

    // First child joint wins: its local translation IS the tail offset.
    for (std::size_t j = 0, end = joints.size(); j < end; ++j) {
        if (j == joint_index) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node>& other = joints[j];
        if (other && (other->get_parent_node() == joint)) {
            return other->parent_from_node_transform().get_translation();
        }
    }

    // Leaf joint: point along local +Y, as long as this joint's own offset from
    // its parent - the same "how long is a bone here" cue the line
    // visualization uses.
    const std::shared_ptr<erhe::scene::Node> parent = joint->get_parent_node();
    if (parent) {
        const float length = glm::length(joint->parent_from_node_transform().get_translation());
        if (length > 0.0f) {
            return glm::vec3{0.0f, length, 0.0f};
        }
    }
    return glm::vec3{0.2f, 0.0f, 0.0f};
}

Bone_visualization::Bone_visualization(App_context& context, erhe::scene_renderer::Mesh_memory& mesh_memory)
    : m_context    {context}
    , m_mesh_memory{mesh_memory}
{
}

Bone_visualization::~Bone_visualization() noexcept = default;

void Bone_visualization::set_width_scale(const float width_scale)
{
    m_width_scale = width_scale;
}

auto Bone_visualization::get_width_scale() const -> float
{
    return m_width_scale;
}

void Bone_visualization::ensure_primitive()
{
    if (m_bone_primitive) {
        return;
    }

    auto render_geometry   = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    make_bone(render_geometry->get_mesh());
    make_bone(raytrace_geometry->get_mesh());
    // A hand-built GEO::Mesh carries no normal attribute, and the primitive
    // builder writes vertex normals from facet_normal. The solid bone style
    // shades with Shader_debug::vdotn - literally dot(V, N) - so without this
    // the bones would come out flat black.
    {
        erhe::geometry::Mesh_attributes attributes{render_geometry->get_mesh()};
        erhe::geometry::compute_facet_normals(render_geometry->get_mesh(), attributes);
    }

    m_bone_primitive = std::make_shared<erhe::primitive::Primitive>(render_geometry, raytrace_geometry);
    const bool render_ok = m_bone_primitive->make_renderable_mesh(
        erhe::primitive::Build_info{
            .primitive_types{ .fill_triangles = true },
            .buffer_info = m_mesh_memory.make_primitive_buffer_info()
        },
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(render_ok);
    const bool raytrace_ok = m_bone_primitive->make_raytrace();
    ERHE_VERIFY(raytrace_ok);

    m_material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name = "bone",
            .data = {
                .base_color = glm::vec4{0.7f, 0.7f, 0.8f, 1.0f},
                .bxdf_model = erhe::primitive::Bxdf_model::unlit
            }
        }
    );
}

auto Bone_visualization::make_proxy(const std::shared_ptr<erhe::scene::Node>& joint) -> Proxy
{
    Proxy proxy;
    proxy.joint = joint;
    proxy.node  = std::make_shared<erhe::scene::Node>("bone proxy");
    proxy.mesh  = std::make_shared<erhe::scene::Mesh>("bone proxy");
    proxy.mesh->add_primitive(m_bone_primitive, m_material);
    proxy.mesh->layer_id = Mesh_layer_id::bone;

    // bone_proxy is what keeps this out of the item tree, save, export and
    // prefabs; id is added/removed by update() to gate picking. Deliberately no
    // `content` bit - a proxy must never be mistaken for scene content.
    proxy.mesh->enable_flag_bits(erhe::Item_flags::bone_proxy | erhe::Item_flags::visible);
    proxy.node->enable_flag_bits(erhe::Item_flags::bone_proxy | erhe::Item_flags::visible);

    proxy.node->attach(proxy.mesh);
    proxy.node->set_parent(joint);
    return proxy;
}

void Bone_visualization::set_proxy_transform(Proxy& proxy, const glm::vec3 tail_local)
{
    const float length = glm::length(tail_local);
    const float half_width = std::max(m_width_scale * length, c_min_half_width);

    glm::mat4 transform{1.0f};
    if (length > 0.0f) {
        transform = orient_y_to(tail_local / length);
    }
    transform = glm::scale(transform, glm::vec3{half_width, std::max(length, c_min_half_width), half_width});

    // Head is the joint origin, so the proxy's local translation stays zero.
    proxy.node->set_parent_from_node(transform);
    proxy.tail_local  = tail_local;
    proxy.width_scale = m_width_scale;
}

void Bone_visualization::update(const bool visible, const bool pickable)
{
    ensure_primitive();

    for (auto& [joint_ptr, proxy] : m_proxies) {
        proxy.alive = false;
    }

    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        if (scene_root) {
            update_scene(*scene_root, visible, pickable);
        }
    }

    // Drop proxies whose joint or skin went away.
    for (auto i = m_proxies.begin(); i != m_proxies.end(); ) {
        if (i->second.alive) {
            ++i;
            continue;
        }
        if (i->second.mesh) {
            m_joint_by_proxy_mesh.erase(i->second.mesh.get());
        }
        if (i->second.node) {
            i->second.node->set_node_parent(nullptr);
        }
        i = m_proxies.erase(i);
    }
}

void Bone_visualization::update_scene(Scene_root& scene_root, const bool visible, const bool pickable)
{
    const std::vector<std::shared_ptr<erhe::scene::Skin>>& skins = scene_root.get_scene().get_skins();
    for (const std::shared_ptr<erhe::scene::Skin>& skin : skins) {
        if (!skin) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = skin->skin_data.joints;
        for (std::size_t i = 0, end = joints.size(); i < end; ++i) {
            const std::shared_ptr<erhe::scene::Node>& joint = joints[i];
            if (!joint) {
                continue;
            }

            auto found = m_proxies.find(joint.get());
            if (found == m_proxies.end()) {
                Proxy proxy = make_proxy(joint);
                m_joint_by_proxy_mesh[proxy.mesh.get()] = joint;
                found = m_proxies.emplace(joint.get(), std::move(proxy)).first;
                set_proxy_transform(found->second, bone_tail_in_joint_space(*skin, i));
            } else {
                // Only rebuild the transform when the bone shape actually
                // changed. Under a rotation-only animation - the common case -
                // the child's local translation is constant, so this is a
                // compare and nothing else; the joint's own animation reaches
                // the proxy through the parent link.
                const glm::vec3 tail_local = bone_tail_in_joint_space(*skin, i);
                if ((tail_local != found->second.tail_local) || (m_width_scale != found->second.width_scale)) {
                    set_proxy_transform(found->second, tail_local);
                }
            }

            Proxy& proxy = found->second;
            proxy.alive = true;
            proxy.mesh->set_visible(visible);
            if (pickable) {
                proxy.mesh->enable_flag_bits(erhe::Item_flags::id);
            } else {
                proxy.mesh->disable_flag_bits(erhe::Item_flags::id);
            }
            // Raytrace: set the instance mask directly rather than deriving it
            // from the flags, because bone_proxy must stay set (it is the
            // proxy's identity) while pickability toggles. Mask 0 = unhittable,
            // so in object mode a ray passes straight through to the mesh.
            proxy.mesh->set_rt_mask(pickable ? Raytrace_node_mask::bone : Raytrace_node_mask::none);
        }
    }
}

auto Bone_visualization::get_joint_for_proxy(const erhe::scene::Mesh* mesh) const -> std::shared_ptr<erhe::scene::Node>
{
    const auto i = m_joint_by_proxy_mesh.find(mesh);
    if (i == m_joint_by_proxy_mesh.end()) {
        return {};
    }
    return i->second.lock();
}

}
