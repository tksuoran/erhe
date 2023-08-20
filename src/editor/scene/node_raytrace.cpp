#include "scene/node_raytrace.hpp"

#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe_renderer/line_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_raytrace/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_defer/defer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace editor
{

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::Node_attachment;
using erhe::Item_flags;

auto raytrace_node_mask(erhe::Item& item) -> uint32_t
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

Raytrace_primitive::Raytrace_primitive(
    erhe::scene::Mesh*         mesh,
    std::size_t                primitive_index,
    erhe::raytrace::IGeometry* rt_geometry
)
    : mesh           {mesh}
    , primitive_index{primitive_index}
{
    const std::string name = fmt::format("{}[{}]", mesh->get_name(), primitive_index);
    rt_instance = erhe::raytrace::IInstance::create_unique(name);
    rt_scene    = erhe::raytrace::IScene::create_unique(name);
    rt_instance->set_user_data(this);
    rt_instance->set_scene(rt_scene.get());
    rt_scene   ->attach(rt_geometry);
    if (mesh->is_visible()) {
        rt_instance->enable();
    } else {
        rt_instance->disable();
    }
    rt_scene   ->commit();
    rt_instance->commit();
}

Node_raytrace::Node_raytrace(
    const std::shared_ptr<erhe::scene::Mesh>& mesh
)
{
    initialize(mesh, mesh->mesh_data.primitives);
}

Node_raytrace::Node_raytrace(
    const std::shared_ptr<erhe::scene::Mesh>&      mesh,
    const std::vector<erhe::primitive::Primitive>& primitives
)
{
    initialize(mesh, primitives);
}

void Node_raytrace::initialize(
    const std::shared_ptr<erhe::scene::Mesh>&      mesh,
    const std::vector<erhe::primitive::Primitive>& primitives
)
{
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        const auto primitive = primitives[i];
        const auto& geometry_primitive = primitive.geometry_primitive;
        if (!geometry_primitive) {
            continue;
        }
        const auto& rt_geometry = geometry_primitive->raytrace.rt_geometry;
        if (rt_geometry) {
            m_rt_primitives.emplace_back(mesh.get(), i, rt_geometry.get());
        } else {
            log_raytrace->info("No raytrace geometry in {}", mesh->get_name());
        }
    }
}

Node_raytrace::~Node_raytrace() noexcept
{
    if (m_rt_scene != nullptr) {
        detach_from_scene(m_rt_scene);
    }
}

auto Node_raytrace::get_static_type() -> uint64_t
{
    return erhe::Item_type::node_attachment | erhe::Item_type::raytrace;
}

auto Node_raytrace::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node_raytrace::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Node_raytrace::handle_item_host_update(
    erhe::Item_host* const old_item_host,
    erhe::Item_host* const new_item_host
)
{
    ERHE_VERIFY(old_item_host != new_item_host);
    Scene_root* old_scene_root = static_cast<Scene_root*>(old_item_host);
    Scene_root* new_scene_root = static_cast<Scene_root*>(new_item_host);
    log_raytrace->trace(
        "RT {} node {} old host = {} new host = {}",
        get_label(), get_node()->get_name(),
        (old_scene_root != nullptr) ? old_scene_root->get_name().c_str() : "",
        (new_scene_root != nullptr) ? new_scene_root->get_name().c_str() : ""
    );

    // NOTE: This also keeps this alive if old host has the only shared_ptr to it
    const auto shared_this = std::static_pointer_cast<Node_raytrace>(shared_from_this());

    if (old_item_host != nullptr) {
        old_scene_root->unregister_node_raytrace(shared_this);
    }
    if (new_item_host != nullptr) {
        new_scene_root->register_node_raytrace(shared_this);
    }
}

void Node_raytrace::handle_node_transform_update()
{
    ERHE_PROFILE_FUNCTION();

    for (auto& rt_primitive : m_rt_primitives) {
        rt_primitive.rt_instance->set_transform(m_node->world_from_node());
        rt_primitive.rt_instance->commit();
    }
}

void Node_raytrace::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    const bool visibility_changed = (
        (old_flag_bits ^ new_flag_bits) & erhe::Item_flags::visible
    ) == erhe::Item_flags::visible;
    if (!visibility_changed) {
        return;
    }

    for (auto& rt_primitive : m_rt_primitives) {
        const bool visible = (new_flag_bits & erhe::Item_flags::visible) == erhe::Item_flags::visible;
        if (visible && !rt_primitive.rt_instance->is_enabled()) {
            rt_primitive.rt_instance->enable();
        } else if (!visible && rt_primitive.rt_instance->is_enabled()) {
            rt_primitive.rt_instance->disable();
        }
    }
}

void Node_raytrace::properties_imgui()
{
    ImGui::Text("%d instances", static_cast<int>(m_rt_primitives.size()));
    for (auto& rt_primitive : m_rt_primitives) {
        ImGui::BulletText(
            "%s %s",
            rt_primitive.rt_instance->debug_label().data(),
            rt_primitive.rt_instance->is_enabled() ? "Enabled" : "Disabled"
        );
    }
}

auto Node_raytrace::get_rt_primitives() const -> const std::vector<Raytrace_primitive>&
{
    return m_rt_primitives;
}

void Node_raytrace::set_mask(const uint32_t mask)
{
    for (auto& rt_primitive : m_rt_primitives) {
        rt_primitive.rt_instance->set_mask(mask);
    }
}

void Node_raytrace::attach_to_scene(erhe::raytrace::IScene* rt_scene)
{
    ERHE_VERIFY(rt_scene != nullptr);
    ERHE_VERIFY(m_rt_scene == nullptr);
    for (auto& rt_primitive : m_rt_primitives) {
        rt_scene->attach(rt_primitive.rt_instance.get());
    }
    m_rt_scene = rt_scene;
}

void Node_raytrace::detach_from_scene(erhe::raytrace::IScene* rt_scene)
{
    ERHE_VERIFY(rt_scene == m_rt_scene);
    ERHE_VERIFY(m_rt_scene != nullptr);
    for (auto& rt_primitive : m_rt_primitives) {
        rt_scene->detach(rt_primitive.rt_instance.get());
    }
    m_rt_scene = nullptr;
}

auto is_raytrace(const erhe::Item* const scene_item) -> bool
{
    if (scene_item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(
        scene_item->get_type(),
        erhe::Item_type::raytrace
    );
}

auto is_raytrace(const std::shared_ptr<erhe::Item>& scene_item) -> bool
{
    return is_raytrace(scene_item.get());
}

auto get_node_raytrace(const erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>
{
    for (const auto& attachment : node->get_attachments()) {
        auto node_raytrace = as<Node_raytrace>(attachment);
        if (node_raytrace) {
            return node_raytrace;
        }
    }
    return {};
}

[[nodiscard]] auto get_hit_node(const erhe::raytrace::Hit& hit) -> erhe::scene::Node*
{
    if ((hit.geometry == nullptr) || (hit.instance == nullptr)) {
        return nullptr;
    }

    void* const user_data          = hit.instance->get_user_data();
    auto* const raytrace_primitive = static_cast<Raytrace_primitive*>(user_data);
    if (raytrace_primitive == nullptr) {
        log_raytrace->error("This should not happen");
        return nullptr;
    }

    auto* const mesh = raytrace_primitive->mesh;
    if (mesh == nullptr) {
        log_raytrace->error("This should not happen");
        return nullptr;
    }
    return mesh->get_node();
}

[[nodiscard]] auto get_hit_normal(const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>
{
    if ((hit.geometry == nullptr) || (hit.instance == nullptr)) {
        return {};
    }

    void* const user_data          = hit.instance->get_user_data();
    auto* const raytrace_primitive = static_cast<Raytrace_primitive*>(user_data);
    if (raytrace_primitive == nullptr) {
        log_raytrace->error("This should not happen");
        return {};
    }
    auto* const mesh = raytrace_primitive->mesh;
    if (mesh == nullptr) {
        log_raytrace->error("This should not happen");
        return {};
    }
    auto* const node = mesh->get_node();
    if (node == nullptr) {
        log_raytrace->error("This should not happen");
        return {};
    }

    if (raytrace_primitive->primitive_index >= mesh->mesh_data.primitives.size()) {
        log_raytrace->error("This should not happen");
        return {};
    }

    const auto& primitive = mesh->mesh_data.primitives[raytrace_primitive->primitive_index];
    if (!primitive.geometry_primitive) {
        log_raytrace->error("This should not happen");
        return {};
    }

    using namespace erhe::primitive;
    const Geometry_primitive& geometry_primitive        = *primitive.geometry_primitive.get();
    const Geometry_mesh&      geometry_mesh             = geometry_primitive.gl_geometry_mesh;
    const auto&               triangle_id_to_polygon_id = geometry_mesh.primitive_id_to_polygon_id;
    if (hit.triangle_id >= triangle_id_to_polygon_id.size()) {
        log_raytrace->error("This should not happen");
        return {};
    }
    const auto polygon_id = triangle_id_to_polygon_id[hit.triangle_id];

    using namespace erhe::geometry;
    const auto& geometry = geometry_primitive.source_geometry;
    if (!geometry) {
        return {};
    }

    if (polygon_id >= geometry->get_polygon_count()) {
        log_raytrace->error("This should not happen");
        return {};
    }

    auto* const polygon_normals = geometry->polygon_attributes().find<glm::vec3>(
        erhe::geometry::c_polygon_normals
    );
    if (
        (polygon_normals == nullptr) ||
        !polygon_normals->has(polygon_id)
    ) {
        return {};
    }

    return polygon_normals->get(polygon_id);
}

void draw_ray_hit(
    erhe::renderer::Line_renderer& line_renderer,
    const erhe::raytrace::Ray&     ray,
    const erhe::raytrace::Hit&     hit,
    const Ray_hit_style&           style
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

[[nodiscard]] auto project_ray(
    erhe::raytrace::IScene* const raytrace_scene,
    erhe::scene::Mesh*            ignore_mesh,
    erhe::raytrace::Ray&          ray,
    erhe::raytrace::Hit&          hit
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    erhe::scene::Node*             ignore_node     = ignore_mesh->get_node();
    std::shared_ptr<Node_raytrace> ignore_raytrace = get_node_raytrace(ignore_node);
    bool stored_visibility_state{false};
    if (ignore_raytrace) {
        stored_visibility_state = ignore_raytrace->is_visible();
        ignore_raytrace->hide();
    }
    ERHE_DEFER(
        if (ignore_raytrace) {
            ignore_raytrace->set_visible(stored_visibility_state);
        }
    );

    raytrace_scene->intersect(ray, hit);
    return hit.instance != nullptr;
}

}
