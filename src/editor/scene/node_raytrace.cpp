#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "editor_log.hpp"

#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/build_info.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/raytrace/igeometry.hpp"
#include "erhe/raytrace/iinstance.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::Node_attachment;
using erhe::scene::Item_flags;

auto raytrace_node_mask(erhe::scene::Item& item) -> uint32_t
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
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
)
{
    // Just float vec3 position
    auto vertex_format = std::make_shared<erhe::graphics::Vertex_format>(
        std::initializer_list<erhe::graphics::Vertex_attribute>{
            erhe::graphics::Vertex_attribute{
                .usage =
                {
                    .type      = erhe::graphics::Vertex_attribute::Usage_type::position
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type =
                {
                    .type      = gl::Vertex_attrib_type::float_,
                    .dimension = 3
                }
            }
        }
    );

    //const auto   index_type   = gl::Draw_elements_type::unsigned_int;
    const std::size_t index_stride = 4;

    const erhe::geometry::Mesh_info mesh_info = geometry->get_mesh_info();

    vertex_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry->name + "_vertex",
        mesh_info.vertex_count_corners * vertex_format->stride()
    );
    index_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry->name + "_index",
        mesh_info.index_count_fill_triangles * index_stride
    );

    erhe::primitive::Raytrace_buffer_sink buffer_sink{
        *vertex_buffer.get(),
        *index_buffer.get()
    };

    erhe::primitive::Build_info build_info{&buffer_sink};
    build_info.buffer.index_type = gl::Draw_elements_type::unsigned_int;

    // Just triangle indices and position
    build_info.format.features = {
        .fill_triangles  = true,
        .edge_lines      = false,
        .corner_points   = false,
        .centroid_points = false,
        .position        = true,
        .normal          = false,
        .normal_flat     = false,
        .normal_smooth   = false,
        .tangent         = false,
        .bitangent       = false,
        .color           = false,
        .texcoord        = false,
        .id              = false
    };
    build_info.buffer.vertex_format = vertex_format;

    primitive_geometry = make_primitive(
        *geometry.get(),
        build_info,
        erhe::primitive::Normal_style::none
    );
}

Node_raytrace::Node_raytrace(
    const std::shared_ptr<erhe::geometry::Geometry>& source_geometry
)
    : m_primitive      {std::make_shared<Raytrace_primitive>(source_geometry)}
    , m_source_geometry{source_geometry}
{
    initialize();
}

void Node_raytrace::initialize()
{
    ERHE_PROFILE_FUNCTION

    m_geometry = erhe::raytrace::IGeometry::create_unique(
        m_source_geometry->name + "_triangle_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );

    const auto& vertex_buffer_range   = m_primitive->primitive_geometry.vertex_buffer_range;
    const auto& index_buffer_range    = m_primitive->primitive_geometry.index_buffer_range;
    const auto& triangle_fill_indices = m_primitive->primitive_geometry.triangle_fill_indices;

    m_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_FLOAT3,
        m_primitive->vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_count    = index_buffer_range.count;
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    m_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_UINT3,
        m_primitive->index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    SPDLOG_LOGGER_INFO(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        m_geometry->commit();
    }

    {
        ERHE_PROFILE_SCOPE("create scene");
        m_scene = erhe::raytrace::IScene::create_unique(
            m_source_geometry->name + "_scene"
        );
    }

    m_scene->attach(m_geometry.get());

    m_instance = erhe::raytrace::IInstance::create_unique(
        m_source_geometry->name + "_instance_geometry"
    );

    m_instance->set_scene(m_scene.get());
    {
        ERHE_PROFILE_SCOPE("instance commit");
        m_instance->commit();
    }
    m_instance->set_user_data(this);

    const bool visible = is_visible();
    if (visible) {
        m_instance->enable();
    } else {
        m_instance->disable();
    }
}

Node_raytrace::Node_raytrace(
    const std::shared_ptr<erhe::geometry::Geometry>& source_geometry,
    const std::shared_ptr<Raytrace_primitive>&       primitive
)
    : m_primitive      {primitive}
    , m_source_geometry{source_geometry}
{
    initialize();
}

Node_raytrace::~Node_raytrace() noexcept
{
    // TODO
}

auto Node_raytrace::static_type() -> uint64_t
{
    return
        erhe::scene::Item_type::node_attachment |
        erhe::scene::Item_type::raytrace;
}

auto Node_raytrace::static_type_name() -> const char*
{
    return "Node_raytrace";
}

auto Node_raytrace::get_type() const -> uint64_t
{
    return static_type();
}

auto Node_raytrace::type_name() const -> const char*
{
    return static_type_name();
}

void Node_raytrace::handle_node_scene_host_update(
    erhe::scene::Scene_host* old_scene_host,
    erhe::scene::Scene_host* new_scene_host
)
{
    ERHE_VERIFY(old_scene_host != new_scene_host);

    if (old_scene_host != nullptr) {
        log_raytrace->trace("detaching {} from raytrace world", m_instance->debug_label());
        Scene_root* old_scene_root = reinterpret_cast<Scene_root*>(old_scene_host);
        ERHE_VERIFY(old_scene_root != nullptr);
        auto& raytrace_scene = old_scene_root->raytrace_scene();
        raytrace_scene.detach(raytrace_instance());
    }
    if (new_scene_host != nullptr) {
        log_raytrace->trace("attaching {} to raytrace world", m_instance->debug_label());
        ERHE_VERIFY(m_node);
        Scene_root* new_scene_root = reinterpret_cast<Scene_root*>(new_scene_host);
        ERHE_VERIFY(new_scene_root != nullptr);
        auto& raytrace_scene = new_scene_root->raytrace_scene();
        raytrace_scene.attach(raytrace_instance());
        uint32_t mask = 0;
        for (const auto& node_attachment : m_node->attachments()) {
            mask = mask | raytrace_node_mask(*node_attachment.get());
        }
        m_instance->set_mask(mask);
    }
}

void Node_raytrace::handle_node_transform_update()
{
    ERHE_PROFILE_FUNCTION

    m_instance->set_transform(m_node->world_from_node());
    m_instance->commit();
}

void Node_raytrace::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    const bool visibility_changed = (
        (old_flag_bits ^ new_flag_bits) & erhe::scene::Item_flags::visible
    ) == erhe::scene::Item_flags::visible;
    if (!visibility_changed) {
        return;
    }

    ERHE_VERIFY(m_instance);

    const bool visible = (new_flag_bits & erhe::scene::Item_flags::visible) == erhe::scene::Item_flags::visible;
    if (visible && !m_instance->is_enabled()) {
        SPDLOG_LOGGER_TRACE(log_raytrace, "enabling {} node raytrace", get_node()->name());
        m_instance->enable();
    } else if (!visible && m_instance->is_enabled()) {
        SPDLOG_LOGGER_TRACE(log_raytrace, "disable {} node raytrace", get_node()->name());
        m_instance->disable();
    }

    SPDLOG_LOGGER_TRACE(
        log_raytrace,
        "node raytrace enabled = {}",
        get_node()->name(),
        node_visible,
        m_instance->is_enabled()
    );
}

auto Node_raytrace::source_geometry() const -> std::shared_ptr<erhe::geometry::Geometry>
{
    return m_source_geometry;
}

auto Node_raytrace::raytrace_primitive() -> Raytrace_primitive*
{
    return m_primitive.get();
}

auto Node_raytrace::raytrace_primitive() const -> const Raytrace_primitive*
{
    return m_primitive.get();
}

auto Node_raytrace::raytrace_geometry() -> IGeometry*
{
    return m_geometry.get();
}

auto Node_raytrace::raytrace_geometry() const -> const IGeometry*
{
    return m_geometry.get();
}

auto Node_raytrace::raytrace_instance() -> IInstance*
{
    return m_instance.get();
}

auto Node_raytrace::raytrace_instance() const -> const IInstance*
{
    return m_instance.get();
}

[[nodiscard]] auto Node_raytrace::get_hit_normal(const erhe::raytrace::Hit& hit) -> std::optional<glm::vec3>
{
    auto* node = get_node();
    if (
        (node == nullptr)  ||
        !is_mesh(node)     ||
        !m_primitive       ||
        !m_source_geometry ||
        hit.primitive_id > m_primitive->primitive_geometry.primitive_id_to_polygon_id.size()
    ) {
        return {};
    }

    const auto polygon_id = m_primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
    if (polygon_id < m_source_geometry->get_polygon_count()) {
        auto* const polygon_normals = m_source_geometry->polygon_attributes().find<glm::vec3>(
            erhe::geometry::c_polygon_normals
        );
        if (
            (polygon_normals != nullptr) &&
            polygon_normals->has(polygon_id)
        ) {
            return polygon_normals->get(polygon_id);
        }
    }
    return {};
}

auto is_raytrace(const erhe::scene::Item* const scene_item) -> bool
{
    if (scene_item == nullptr) {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(
        scene_item->get_type(),
        erhe::scene::Item_type::raytrace
    );
}

auto is_raytrace(const std::shared_ptr<erhe::scene::Item>& scene_item) -> bool
{
    return is_raytrace(scene_item.get());
}

auto as_raytrace(erhe::scene::Item* scene_item) -> Node_raytrace*
{
    if (scene_item == nullptr) {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_type(),
            erhe::scene::Item_type::raytrace
        )
    ) {
        return nullptr;
    }
    return static_cast<Node_raytrace*>(scene_item);
}

auto as_raytrace(
    const std::shared_ptr<erhe::scene::Item>& scene_item
) -> std::shared_ptr<Node_raytrace>
{
    if (!scene_item) {
        return {};
    }
    using namespace erhe::toolkit;
    if (
        !test_all_rhs_bits_set(
            scene_item->get_type(),
            erhe::scene::Item_type::raytrace
        )
    ) {
        return {};
    }
    return std::static_pointer_cast<Node_raytrace>(scene_item);
}

auto get_raytrace(const erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>
{
    for (const auto& attachment : node->attachments()) {
        auto node_raytrace = as_raytrace(attachment);
        if (node_raytrace) {
            return node_raytrace;
        }
    }
    return {};
}

void draw_ray_hit(
    erhe::application::Line_renderer& line_renderer,
    const erhe::raytrace::Ray&        ray,
    const erhe::raytrace::Hit&        hit,
    const Ray_hit_style&              style
)
{
    void* user_data     = hit.instance->get_user_data();
    auto* raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
    if (raytrace_node == nullptr) {
        return;
    }

    const auto local_normal_opt = raytrace_node->get_hit_normal(hit);
    if (!local_normal_opt.has_value()) {
        return;
    }

    const glm::vec3 position        = ray.origin + ray.t_far * ray.direction;
    const glm::vec3 local_normal    = local_normal_opt.value();
    const glm::mat4 world_from_node = raytrace_node->get_node()->world_from_node();
    const glm::vec3 N{world_from_node * glm::vec4{local_normal, 0.0f}};
    const glm::vec3 T = erhe::toolkit::safe_normalize_cross<float>(N, ray.direction);
    const glm::vec3 B = erhe::toolkit::safe_normalize_cross<float>(T, N);

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
    std::size_t count{0};
    for (;;) {
        raytrace_scene->intersect(ray, hit);
        if (hit.instance == nullptr) {
            return false;
        }
        void* user_data     = hit.instance->get_user_data();
        auto* raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
        if (raytrace_node == nullptr) {
            return false;
        }
        auto* node = raytrace_node->get_node(); // TODO get_mesh() ?
        if (node == nullptr) {
            return false;
        }
        const auto& mesh = get_mesh(node);
        if (mesh.get() == ignore_mesh) {
            ray.origin = ray.origin + ray.t_far * ray.direction + 0.001f * ray.direction;
            ++count;
            if (count > 100) {
                return false;
            }
            continue;
        }
        return true;
    }
}

}
