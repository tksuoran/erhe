#include "scene/node_raytrace.hpp"
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
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

using erhe::raytrace::IGeometry;
using erhe::raytrace::IInstance;
using erhe::scene::INode_attachment;

auto raytrace_node_mask(erhe::scene::Node& node) -> uint32_t
{
    uint32_t result{0};
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::content     ) != 0) result |= Raytrace_node_mask::content     ;
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::shadow_cast ) != 0) result |= Raytrace_node_mask::shadow_cast ;
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::tool        ) != 0) result |= Raytrace_node_mask::tool        ;
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::brush       ) != 0) result |= Raytrace_node_mask::brush       ;
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::rendertarget) != 0) result |= Raytrace_node_mask::rendertarget;
    if ((node.get_visibility_mask() & erhe::scene::Node_visibility::controller  ) != 0) result |= Raytrace_node_mask::controller  ;
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

    m_flag_bits |= INode_attachment::c_flag_bit_is_raytrace;

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

auto Node_raytrace::node_attachment_type() const -> const char*
{
    return "Node_raytrace";
}

void Node_raytrace::on_attached_to(erhe::scene::Node* node)
{
    if (node == nullptr)
    {
        return;
    }
    const auto mask = raytrace_node_mask(*node);
    m_instance->set_mask(mask);
}

void Node_raytrace::on_node_transform_changed()
{
    ERHE_PROFILE_FUNCTION

    auto* node = get_node();
    m_instance->set_transform(node->world_from_node());
    m_instance->commit();
}

void Node_raytrace::on_node_visibility_mask_changed(const uint64_t mask)
{
    const bool node_visible = (mask & erhe::scene::Node_visibility::visible) != 0;
    if (node_visible && !m_instance->is_enabled())
    {
        SPDLOG_LOGGER_TRACE(log_raytrace, "enabling {} node raytrace", get_node()->name());
        m_instance->enable();
    }
    else if (!node_visible && m_instance->is_enabled())
    {
        SPDLOG_LOGGER_TRACE(log_raytrace, "disable {} node raytrace", get_node()->name());
        m_instance->disable();
    }

    SPDLOG_LOGGER_TRACE(
        log_raytrace,
        "node {} visible = {}, node raytrace enabled = {}",
        get_node()->name(),
        node_visible,
        m_instance->is_enabled()
    );
}

auto Node_raytrace::source_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    return m_source_geometry;
}

auto Node_raytrace::raytrace_primitive() -> Raytrace_primitive*
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
    )
    {
        return {};
    }

    const auto polygon_id = m_primitive->primitive_geometry.primitive_id_to_polygon_id[hit.primitive_id];
    if (polygon_id < m_source_geometry->get_polygon_count())
    {
        auto* const polygon_normals = m_source_geometry->polygon_attributes().find<glm::vec3>(erhe::geometry::c_polygon_normals);
        if (
            (polygon_normals != nullptr) &&
            polygon_normals->has(polygon_id)
        )
        {
            return polygon_normals->get(polygon_id);
        }
    }
    return {};
}

auto is_raytrace(const INode_attachment* const attachment) -> bool
{
    if (attachment == nullptr)
    {
        return false;
    }
    return (attachment->flag_bits() & INode_attachment::c_flag_bit_is_raytrace) == INode_attachment::c_flag_bit_is_raytrace;
}

auto is_raytrace(const std::shared_ptr<INode_attachment>& attachment) -> bool
{
    return is_raytrace(attachment.get());
}

auto as_raytrace(INode_attachment* attachment) -> Node_raytrace*
{
    if (attachment == nullptr)
    {
        return nullptr;
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_raytrace) == 0)
    {
        return nullptr;
    }
    return reinterpret_cast<Node_raytrace*>(attachment);
}

auto as_raytrace(
    const std::shared_ptr<INode_attachment>& attachment
) -> std::shared_ptr<Node_raytrace>
{
    if (!attachment)
    {
        return {};
    }
    if ((attachment->flag_bits() & INode_attachment::c_flag_bit_is_raytrace) == 0)
    {
        return {};
    }
    return std::dynamic_pointer_cast<Node_raytrace>(attachment);
}

auto get_raytrace(erhe::scene::Node* node) -> std::shared_ptr<Node_raytrace>
{
    for (const auto& attachment : node->attachments())
    {
        auto node_raytrace = as_raytrace(attachment);
        if (node_raytrace)
        {
            return node_raytrace;
        }
    }
    return {};
}

auto safe_normalize_cross(const glm::vec3& lhs, const glm::vec3& rhs)
{
    const float d = glm::dot(lhs, rhs);
    if (std::abs(d) > 0.999f)
    {
        return erhe::toolkit::min_axis(lhs);
    }

    const glm::vec3 c0 = glm::cross(lhs, rhs);
    if (glm::length(c0) < glm::epsilon<float>())
    {
        return erhe::toolkit::min_axis(lhs);
    }
    return glm::normalize(c0);
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
    if (raytrace_node == nullptr)
    {
        return;
    }

    const auto local_normal_opt = raytrace_node->get_hit_normal(hit);
    if (!local_normal_opt.has_value())
    {
        return;
    }

    const glm::vec3 position        = ray.origin + ray.t_far * ray.direction;
    const glm::vec3 local_normal    = local_normal_opt.value();
    const glm::mat4 world_from_node = raytrace_node->get_node()->world_from_node();
    const glm::vec3 N{world_from_node * glm::vec4{local_normal, 0.0f}};
    const glm::vec3 T = safe_normalize_cross(N, ray.direction);
    const glm::vec3 B = safe_normalize_cross(T, N);

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
    for (;;)
    {
        raytrace_scene->intersect(ray, hit);
        if (hit.instance == nullptr)
        {
            return false;
        }
        void* user_data     = hit.instance->get_user_data();
        auto* raytrace_node = reinterpret_cast<Node_raytrace*>(user_data);
        if (raytrace_node == nullptr)
        {
            return false;
        }
        auto* node = raytrace_node->get_node();
        if (node == ignore_mesh)
        {
            ray.origin = ray.origin + ray.t_far * ray.direction + 0.001f * ray.direction;
            ++count;
            if (count > 100)
            {
                return false;
            }
            continue;
        }
        return true;
    }
}

}
