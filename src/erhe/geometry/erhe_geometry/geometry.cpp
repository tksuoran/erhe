#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_log/log_geogram.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/basic/command_line.h>
#include <geogram/basic/geometry.h>
#include <geogram/basic/matrix.h>
#include <geogram/delaunay/delaunay.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>

#include <fmt/format.h>

#include <cmath>
#include <unordered_map>

void set_point(GEO::MeshVertices& mesh_vertices, GEO::index_t vertex, GEO::vec3 p)
{
    //if (mesh_vertices.single_precision()) {
    mesh_vertices.point(vertex) = p;
    //}
}

void set_pointf(GEO::MeshVertices& mesh_vertices, GEO::index_t vertex, GEO::vec3f p)
{
    float* ptr = mesh_vertices.single_precision_point_ptr(vertex);
    ptr[0] = p.x;
    ptr[1] = p.y;
    ptr[2] = p.z;
}

auto get_point(const GEO::MeshVertices& mesh_vertices, GEO::index_t vertex) -> GEO::vec3
{
    return mesh_vertices.point(vertex);
}

auto get_pointf(const GEO::MeshVertices& mesh_vertices, GEO::index_t vertex) -> GEO::vec3f
{
    const float* const p = mesh_vertices.single_precision_point_ptr(vertex);
    return GEO::vec3f{p[0], p[1], p[2]};
}

const GEO::vec3f& mesh_vertexf(const GEO::Mesh& M, GEO::index_t v) {
    geo_debug_assert(M.vertices.dimension() >= 3);
    return *(const GEO::vec3f*) (M.vertices.single_precision_point_ptr(v));
}

const GEO::vec3f& mesh_corner_vertex(const GEO::Mesh& M, GEO::index_t c) {
    return mesh_vertexf(M, M.facet_corners.vertex(c));
}

auto mesh_facet_normalf(const GEO::Mesh& M, GEO::index_t f) -> GEO::vec3f
{
    GEO::vec3f result(0.0f, 0.0f, 0.0f);
    GEO::index_t c1 = M.facets.corners_begin(f);
    GEO::index_t v1 = M.facet_corners.vertex(c1);
    const GEO::vec3f& p1 = mesh_vertexf(M, v1);
    for(GEO::index_t c2=c1+1; c2<M.facets.corners_end(f); ++c2) {
        GEO::index_t c3 = M.facets.next_corner_around_facet(f,c2);
        GEO::index_t v2 = M.facet_corners.vertex(c2);
        GEO::index_t v3 = M.facet_corners.vertex(c3);
        const GEO::vec3f& p2 = mesh_vertexf(M, v2);
        const GEO::vec3f& p3 = mesh_vertexf(M, v3);
        result += GEO::cross(p2 - p1, p3 - p1);
    }
    return result;
}

auto mesh_facet_centerf(const GEO::Mesh& M, GEO::index_t f) -> GEO::vec3f
{
    GEO::vec3f result(0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    for(GEO::index_t c = M.facets.corners_begin(f);
        c < M.facets.corners_end(f); ++c) {
        result += mesh_corner_vertex(M, c);
        count += 1.0f;
    }
    return (1.0f / count) * result;
}

Attribute_descriptor::Attribute_descriptor(
    int                usage_index,
    const std::string& name,
    Transform_mode     transform_mode,
    Interpolation_mode interpolation_mode
)
    : usage_index       {usage_index}
    , name              {name}
    , transform_mode    {transform_mode}
    , interpolation_mode{interpolation_mode}
    , present_name      {fmt::format("present_{}", name)}
{
}

void Attribute_descriptors::insert(const Attribute_descriptor& descriptor)
{
    s_descriptors.push_back(descriptor);
}

std::vector<Attribute_descriptor> Attribute_descriptors::s_descriptors{};

void Attribute_descriptors::init()
{
    insert(s_normal         );
    insert(s_tangent        );
    insert(s_bitangent      );
    insert(s_texcoord_0     );
    insert(s_texcoord_1     );
    insert(s_color_0        );
    insert(s_color_1        );
    insert(s_joint_indices_0);
    insert(s_joint_indices_1);
    insert(s_joint_weights_0);
    insert(s_joint_weights_1);

    insert(s_valency_edge_count);
    insert(s_id                );
    insert(s_normal_smooth     );
    insert(s_centroid          );
    insert(s_aniso_control     );
}

auto Attribute_descriptors::get(int usage_index, const char* name) -> const Attribute_descriptor*
{
    for (const Attribute_descriptor& descriptor : s_descriptors) {
        if ((descriptor.usage_index == usage_index) && (descriptor.name == name)) {
            return &descriptor;
        }
    }
    return nullptr;
}

Mesh_attributes::Mesh_attributes(const GEO::Mesh& mesh)
    : m_mesh{mesh}
    , facet_id                 {mesh.facets       .attributes(), Attribute_descriptors::s_id                }
    , facet_centroid           {mesh.facets       .attributes(), Attribute_descriptors::s_centroid          }
    , facet_normal             {mesh.facets       .attributes(), Attribute_descriptors::s_normal            }
    , facet_tangent            {mesh.facets       .attributes(), Attribute_descriptors::s_tangent           }
    , facet_bitangent          {mesh.facets       .attributes(), Attribute_descriptors::s_bitangent         }
    , facet_color_0            {mesh.facets       .attributes(), Attribute_descriptors::s_color_0           }
    , facet_color_1            {mesh.facets       .attributes(), Attribute_descriptors::s_color_1           }
    , facet_aniso_control      {mesh.facets       .attributes(), Attribute_descriptors::s_aniso_control     }
    , vertex_normal            {mesh.vertices     .attributes(), Attribute_descriptors::s_normal            }
    , vertex_normal_smooth     {mesh.vertices     .attributes(), Attribute_descriptors::s_normal_smooth     }
    , vertex_texcoord_0        {mesh.vertices     .attributes(), Attribute_descriptors::s_texcoord_0        }
    , vertex_texcoord_1        {mesh.vertices     .attributes(), Attribute_descriptors::s_texcoord_1        }
    , vertex_tangent           {mesh.vertices     .attributes(), Attribute_descriptors::s_tangent           }
    , vertex_bitangent         {mesh.vertices     .attributes(), Attribute_descriptors::s_bitangent         }
    , vertex_color_0           {mesh.vertices     .attributes(), Attribute_descriptors::s_color_0           }
    , vertex_color_1           {mesh.vertices     .attributes(), Attribute_descriptors::s_color_1           }
    , vertex_joint_indices_0   {mesh.vertices     .attributes(), Attribute_descriptors::s_joint_indices_0   }
    , vertex_joint_indices_1   {mesh.vertices     .attributes(), Attribute_descriptors::s_joint_indices_1   }
    , vertex_joint_weights_0   {mesh.vertices     .attributes(), Attribute_descriptors::s_joint_weights_0   }
    , vertex_joint_weights_1   {mesh.vertices     .attributes(), Attribute_descriptors::s_joint_weights_1   }
    , vertex_aniso_control     {mesh.vertices     .attributes(), Attribute_descriptors::s_aniso_control     }
    , vertex_valency_edge_count{mesh.vertices     .attributes(), Attribute_descriptors::s_valency_edge_count}
    , corner_normal            {mesh.facet_corners.attributes(), Attribute_descriptors::s_normal            }
    , corner_texcoord_0        {mesh.facet_corners.attributes(), Attribute_descriptors::s_texcoord_0        }
    , corner_texcoord_1        {mesh.facet_corners.attributes(), Attribute_descriptors::s_texcoord_1        }
    , corner_tangent           {mesh.facet_corners.attributes(), Attribute_descriptors::s_tangent           }
    , corner_bitangent         {mesh.facet_corners.attributes(), Attribute_descriptors::s_bitangent         }
    , corner_color_0           {mesh.facet_corners.attributes(), Attribute_descriptors::s_color_0           }
    , corner_color_1           {mesh.facet_corners.attributes(), Attribute_descriptors::s_color_1           }
    , corner_aniso_control     {mesh.facet_corners.attributes(), Attribute_descriptors::s_aniso_control     }
{
}

void Mesh_attributes::unbind()
{
    facet_id                 .unbind();
    facet_centroid           .unbind();
    facet_normal             .unbind();
    facet_tangent            .unbind();
    facet_bitangent          .unbind();
    facet_color_0            .unbind();
    facet_color_1            .unbind();
    facet_aniso_control      .unbind();
    vertex_normal            .unbind();
    vertex_normal_smooth     .unbind();
    vertex_texcoord_0        .unbind();
    vertex_texcoord_1        .unbind();
    vertex_tangent           .unbind();
    vertex_bitangent         .unbind();
    vertex_color_0           .unbind();
    vertex_color_1           .unbind();
    vertex_joint_indices_0   .unbind();
    vertex_joint_indices_1   .unbind();
    vertex_joint_weights_0   .unbind();
    vertex_joint_weights_1   .unbind();
    vertex_aniso_control     .unbind();
    vertex_valency_edge_count.unbind();
    corner_normal            .unbind();
    corner_texcoord_0        .unbind();
    corner_texcoord_1        .unbind();
    corner_tangent           .unbind();
    corner_bitangent         .unbind();
    corner_color_0           .unbind();
    corner_color_1           .unbind();
    corner_aniso_control     .unbind();
}

void Mesh_attributes::bind()
{
    facet_id                 .bind(m_mesh.facets       .attributes());
    facet_centroid           .bind(m_mesh.facets       .attributes());
    facet_normal             .bind(m_mesh.facets       .attributes());
    facet_tangent            .bind(m_mesh.facets       .attributes());
    facet_bitangent          .bind(m_mesh.facets       .attributes());
    facet_color_0            .bind(m_mesh.facets       .attributes());
    facet_color_1            .bind(m_mesh.facets       .attributes());
    facet_aniso_control      .bind(m_mesh.facets       .attributes());
    vertex_normal            .bind(m_mesh.vertices     .attributes());
    vertex_normal_smooth     .bind(m_mesh.vertices     .attributes());
    vertex_texcoord_0        .bind(m_mesh.vertices     .attributes());
    vertex_texcoord_1        .bind(m_mesh.vertices     .attributes());
    vertex_tangent           .bind(m_mesh.vertices     .attributes());
    vertex_bitangent         .bind(m_mesh.vertices     .attributes());
    vertex_color_0           .bind(m_mesh.vertices     .attributes());
    vertex_color_1           .bind(m_mesh.vertices     .attributes());
    vertex_joint_indices_0   .bind(m_mesh.vertices     .attributes());
    vertex_joint_indices_1   .bind(m_mesh.vertices     .attributes());
    vertex_joint_weights_0   .bind(m_mesh.vertices     .attributes());
    vertex_joint_weights_1   .bind(m_mesh.vertices     .attributes());
    vertex_aniso_control     .bind(m_mesh.vertices     .attributes());
    vertex_valency_edge_count.bind(m_mesh.vertices     .attributes());
    corner_normal            .bind(m_mesh.facet_corners.attributes());
    corner_texcoord_0        .bind(m_mesh.facet_corners.attributes());
    corner_texcoord_1        .bind(m_mesh.facet_corners.attributes());
    corner_tangent           .bind(m_mesh.facet_corners.attributes());
    corner_bitangent         .bind(m_mesh.facet_corners.attributes());
    corner_color_0           .bind(m_mesh.facet_corners.attributes());
    corner_color_1           .bind(m_mesh.facet_corners.attributes());
    corner_aniso_control     .bind(m_mesh.facet_corners.attributes());
}

Mesh_attributes::~Mesh_attributes()
{
    static int counter = 0;
    ++counter;
}

auto count_mesh_facet_triangles(const GEO::Mesh& mesh) -> std::size_t
{
    std::size_t triangle_count = 0;
    for (GEO::index_t facet : mesh.facets) {
        GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
        if (corner_count < 3) {
            continue;
        }
        triangle_count += static_cast<std::size_t>(corner_count - 2);
    }
    return triangle_count;
}

auto get_mesh_info(const GEO::Mesh& mesh) -> Mesh_info
{
    const GEO::index_t facet_count    = mesh.facets.nb();
    const GEO::index_t corner_count   = mesh.facet_corners.nb();
    const std::size_t  triangle_count = count_mesh_facet_triangles(mesh);
    const GEO::index_t edge_count     = mesh.edges.nb();
    return Mesh_info{
        .facet_count    = facet_count,
        .corner_count   = corner_count,
        .triangle_count = triangle_count,
        .edge_count     = edge_count,

        // Additional vertices needed for centroids
        // 3 indices per triangle after triangulation, 2 indices per edge, 1 per corner, 1 index per centroid
        .vertex_count_corners        = corner_count,
        .vertex_count_centroids      = facet_count,
        .index_count_fill_triangles  = 3 * triangle_count,
        .index_count_edge_lines      = 2 * edge_count,
        .index_count_corner_points   = corner_count,
        .index_count_centroid_points = facet_count
    };
}

void compute_facet_normals(GEO::Mesh& mesh, Mesh_attributes& attributes)
{
    Attribute_present<GEO::vec3f>& facet_normal_attribute = attributes.facet_normal;
    for (GEO::index_t facet = 0, end = mesh.facets.nb(); facet < end; ++facet) {
        const GEO::vec3f facet_normal = GEO::normalize(mesh_facet_normalf(mesh, facet));
        facet_normal_attribute.set(facet, facet_normal);
    }
}

void compute_facet_centroids(GEO::Mesh& mesh, Mesh_attributes& attributes)
{
    Attribute_present<GEO::vec3f>& facet_centroid = attributes.facet_centroid;
    for (GEO::index_t facet = 0, end = mesh.facets.nb(); facet < end; ++facet) {
        const GEO::vec3f centroid = mesh_facet_centerf(mesh, facet);
        facet_centroid.set(facet, centroid);
    }
}

void compute_mesh_vertex_normal_smooth(GEO::Mesh& mesh, Mesh_attributes& attributes)
{
    Attribute_present<GEO::vec3f>& vertex_normal_smooth = attributes.vertex_normal_smooth;
    vertex_normal_smooth.fill(GEO::vec3f{0.0f, 0.0f, 0.0f});
    for (GEO::index_t facet : mesh.facets) {
        GEO::vec3f facet_normal = GEO::normalize(mesh_facet_normalf(mesh, facet));
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            const GEO::vec3f old_sum = vertex_normal_smooth.get(vertex);
            const GEO::vec3f new_sum = old_sum + facet_normal;
            vertex_normal_smooth.set(vertex, new_sum);
        }
    }
    for (GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f sum = vertex_normal_smooth.get(vertex);
        const GEO::vec3f normalized = GEO::normalize(sum);
        vertex_normal_smooth.set(vertex, normalized);
    }
}

#if 0
void Mesh_info::trace(const std::shared_ptr<spdlog::logger>& log) const
{
    log->trace("{} vertex corners vertices", vertex_count_corners);
    log->trace("{} centroid vertices",       vertex_count_centroids);
    log->trace("{} fill triangles indices",  index_count_fill_triangles);
    log->trace("{} edge lines indices",      index_count_edge_lines);
    log->trace("{} corner points indices",   index_count_corner_points);
    log->trace("{} centroid points indices", index_count_centroid_points);
}

void Geometry::generate_texture_coordinates_spherical()
{
    ERHE_PROFILE_FUNCTION();

    log_geometry->trace("{} for {}", __func__, name);

    compute_polygon_normals();
    compute_point_normals(c_point_normals);
    const auto* const polygon_normals      = polygon_attributes().find          <vec3>(c_polygon_normals     );
    const auto* const corner_normals       = corner_attributes ().find          <vec3>(c_corner_normals      );
    const auto* const point_normals        = point_attributes  ().find          <vec3>(c_point_normals       );
    const auto* const point_normals_smooth = point_attributes  ().find          <vec3>(c_point_normals_smooth);
          auto* const corner_texcoords     = corner_attributes ().find_or_create<vec2>(c_corner_texcoords    );
    const auto* const point_locations      = point_attributes  ().find          <vec3>(c_point_locations     );

    for (Polygon_id polygon_id = 0; polygon_id < m_next_polygon_id; ++polygon_id) {
        Polygon& polygon = polygons[polygon_id];
        for (
            Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
            end = polygon.first_polygon_corner_id + polygon.corner_count;
            polygon_corner_id < end;
            ++polygon_corner_id
        ) {
            const Corner_id corner_id = polygon_corners[polygon_corner_id];
            const Corner&   corner    = corners[corner_id];
            const Point_id  point_id  = corner.point_id;

            glm::vec3 normal{0.0f, 1.0f, 0.0f};
            if ((point_locations != nullptr) && point_locations->has(point_id)) {
                normal = glm::normalize(point_locations->get(point_id));
            } else if ((corner_normals != nullptr) && corner_normals->has(corner_id)) {
                normal = corner_normals->get(corner_id);
            } else if ((point_normals != nullptr) && point_normals->has(point_id)) {
                normal = point_normals->get(point_id);
            } else if ((point_normals_smooth != nullptr) && point_normals_smooth->has(point_id)) {
                normal = point_normals_smooth->get(point_id);
            } else if ((polygon_normals != nullptr) && polygon_normals->has(polygon_id)) {
                normal = polygon_normals->get(polygon_id);
            } else {
                ERHE_FATAL("No normal sources");
            }

            float u;
            float v;
            //if (std::abs(normal.x) >= std::abs(normal.y) && std::abs(normal.x) >= normal.z)
            {
                u = (-normal.z / std::abs(normal.x) + 1.0f) / 2.0f;
                v = (-normal.y / std::abs(normal.x) + 1.0f) / 2.0f;
            }
            //else if (std::abs(normal.y) >= std::abs(normal.x) && std::abs(normal.y) >= normal.z) {
            //    u = (-normal.z / std::abs(normal.y) + 1.0f) / 2.0f;
            //    v = (-normal.x / std::abs(normal.y) + 1.0f) / 2.0f;
            //} else {
            //    u = (-normal.x / std::abs(normal.z) + 1.0f) / 2.0f;
            //    v = (-normal.y / std::abs(normal.z) + 1.0f) / 2.0f;
            //}
            corner_texcoords->put(corner_id, glm::vec2{u, v});
        }
    }
}

auto Geometry::has_polygon_texture_coordinates() const -> bool
{
    return m_serial_polygon_texture_coordinates == m_serial;
}
#endif

void generate_mesh_facet_texture_coordinates(GEO::Mesh& mesh, GEO::index_t facet, Mesh_attributes& attributes)
{
    // TODO Should cached normals centroids from attributes be used?
    const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
    if (corner_count < 3) {
        return;
    }
    //const GEO::vec3 facet_normal = GEO::Geom::mesh_facet_normal(facet); //attributes.facet_normal.get(facet);
    //const double length = GEO::length(facet_normal);
    //const double length2 = GEO::length2(facet_normal);
    //if (length < 0.9) {
    //    return;
    //}
    //static_cast<void>(length);
    //static_cast<void>(length2);

    const GEO::MeshFacets& mesh_facets = mesh.facets;
    const auto& mesh_vertices = mesh.vertices;

    //const GEO::index_t p0_corner   = mesh_facets.corner(facet, 0);
    const GEO::index_t p0_vertex   = mesh_facets.vertex(facet, 0);
    const GEO::vec3f   p0_position = get_pointf(mesh_vertices, p0_vertex);
    const GEO::vec3f   centroid    = mesh_facet_centerf(mesh, facet); // GEO::vec3{attributes.facet_centroid.get(facet)};
    const GEO::vec3f   normal      = GEO::normalize(mesh_facet_normalf(mesh, facet));
    const GEO::vec3f   view        = normal;
    const GEO::vec3f   edge        = GEO::normalize(p0_position - centroid);
    const GEO::vec3f   side        = GEO::normalize(GEO::cross(view, edge));
    const double view_length2 = GEO::length2(view);
    const double edge_length2 = GEO::length2(edge);
    const double side_length2 = GEO::length2(side);
    if (
        (view_length2 < 0.9) ||
        (edge_length2 < 0.9) ||
        (side_length2 < 0.9)
    )
    {
        // Degenerate polygon
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{0.0f, 0.0f});
        }
        return;
    }

    GEO::mat4f transform;
    transform(0, 0) = edge[0]; // X+ axis is column 0
    transform(1, 0) = edge[1];
    transform(2, 0) = edge[2];
    transform(3, 0) = 0.0f;
    transform(0, 1) = side[0]; // Y+ axis is column 1
    transform(1, 1) = side[1];
    transform(2, 1) = side[2];
    transform(3, 1) = 0.0f;
    transform(0, 2) = view[0]; // Z+ axis is column 2
    transform(1, 2) = view[1];
    transform(2, 2) = view[2];
    transform(3, 2) = 0.0f;
    transform(0, 3) = centroid[0]; // Translation is column 3
    transform(1, 3) = centroid[1];
    transform(2, 3) = centroid[2];
    transform(3, 3) = 1.0f;

    const GEO::mat4f inverse_transform = transform.inverse();

    // First pass - for scale
    float max_distance = 0.0f;
    std::vector<std::pair<GEO::index_t, GEO::vec2f>> unscaled_uvs;
    for (GEO::index_t i = 0; i < corner_count; ++i) {
        const GEO::index_t corner          = mesh_facets.corner(facet, i);
        const GEO::index_t vertex          = mesh_facets.vertex(facet, i);
        const GEO::vec3f   position        = get_pointf(mesh_vertices, vertex);
        const GEO::vec3f   planar_position = GEO::transform_point(inverse_transform, position);
        const GEO::vec2f   uv              = GEO::vec2f{planar_position.x, planar_position.y};
        unscaled_uvs.emplace_back(corner, uv);
        const float distance = GEO::length(uv);
        max_distance = std::max(distance, max_distance);
    }
    const float scale = 1.0f / max_distance;

    // Second pass - generate texture coordinates
    for (const auto& unscaled_uv : unscaled_uvs) {
        attributes.corner_texcoord_0.set(unscaled_uv.first, scale * unscaled_uv.second);
    }
}

void generate_mesh_facet_texture_coordinates(GEO::Mesh& mesh, Mesh_attributes& attributes)
{
    //compute_facet_normals(mesh, attributes);
    //compute_facet_centroids(mesh, attributes);
    for (GEO::index_t facet : mesh.facets) {
        generate_mesh_facet_texture_coordinates(mesh, facet, attributes);
    }
}

#if 0
auto Geometry::get_mass_properties() -> Mass_properties
{
    ERHE_PROFILE_FUNCTION();

    if (!compute_polygon_centroids()) {
        return Mass_properties{
            0.0f,
            glm::vec3{0.0f},
            glm::mat3{1.0f}
        };
    }

#if !defined(ERHE_USE_GEOMETRIC_TOOLS)
    const auto* const point_locations   = point_attributes  ().find<vec3>(c_point_locations);
    const auto* const polygon_centroids = polygon_attributes().find<vec3>(c_polygon_centroids);

    float sum{0.0f};
    for_each_polygon_const([&](auto& i) {
        if (i.polygon.corner_count < 3) {
            return;
        }
        const vec3 p0 = polygon_centroids->get(i.polygon_id);
        i.polygon.for_each_corner_neighborhood_const(*this, [&](auto& j) {
            const vec3 p1 = point_locations->get(j.corner.point_id);
            const vec3 p2 = point_locations->get(j.next_corner.point_id);
            const mat3 m{p0, p1, p2};

            //float a = dot(cross(p0, p1), p2);
            const float b = glm::determinant(m);
            sum += std::fabs(b);
        });
    });

    const float mass = sum / 6.0f;
    return Mass_properties{
        .volume            = mass,
        .center_of_gravity = glm::vec3{0.0f},
        .inertial          = glm::mat3{1.0f}
    };
#else
    // Copy vertex data in gt format
    std::vector<gte::Vector3<float>> vertex_data;
    vertex_data.reserve(point_count());
    for_each_point_const([&](auto& i)
    {
        glm::vec3 position = point_locations->get(i.point_id);
        vertex_data.push_back(
            gte::Vector3<float>{position.x, position.y, position.z}
        );
    });

    const int triangle_count = static_cast<int>(count_polygon_triangles());

    std::vector<int> triangles;
    triangles.reserve(triangle_count * 3);
    for_each_polygon([&](auto& i) {
        const Corner_id first_corner_id = i.polygon.first_polygon_corner_id;
        const Point_id  first_point_id  = corners[first_corner_id].point_id;
        i.polygon.for_each_corner_neighborhood(*this, [&](auto& j) {
            const Corner_id corner_id      = j.corner_id;
            const Corner_id next_corner_id = j.next_corner_id;
            const Point_id  point_id       = corners[corner_id].point_id;
            const Point_id  next_point_id  = corners[next_corner_id].point_id;
            if (first_point_id == point_id) {
                return;
            }
            if (first_point_id == next_point_id) {
                return;
            }
            triangles.push_back(first_point_id);
            triangles.push_back(point_id);
            triangles.push_back(next_point_id);
        });
    });

    ERHE_VERIFY(triangles.size() == triangle_count * 3);

    //auto vertex_data = reinterpret_cast<gte::Vector3<float> const*>(point_locations->values.data());
    float mass{0.0f};
    gte::Vector3<float> center_of_mass{0.0f, 0.0f, 0.0f};
    gte::Matrix3x3<float> inertia{};
    gte::ComputeMassProperties<float>(
        vertex_data.data(),
        triangle_count,
        triangles.data(),
        true, // inertia relative to center of mass
        mass,
        center_of_mass,
        inertia
    );

    return Mass_properties{
        std::abs(mass),
        glm::vec3{
            center_of_mass[0],
            center_of_mass[1],
            center_of_mass[2]
        },
        glm::mat3{
            inertia(0, 0), inertia(1, 0), inertia(2, 0),
            inertia(0, 1), inertia(1, 1), inertia(2, 1),
            inertia(0, 2), inertia(1, 2), inertia(2, 2)
        }
    };
#endif
}
#endif

auto make_convex_hull(const GEO::Mesh& source, GEO::Mesh& destination) -> bool
{
    try {
        const GEO::index_t nb_pts = source.vertices.nb();
        const GEO::index_t dim = source.vertices.dimension();
        GEO::vector<double> points;
        points.reserve(nb_pts * dim);
        for (GEO::index_t v : source.vertices) {
            const float* p = source.vertices.single_precision_point_ptr(v);
            for (GEO::index_t c = 0; c < dim; ++c) {
                points.push_back(static_cast<double>(p[c]));
            }
        }
        GEO::CmdLine::set_arg("algo:delaunay", "PDEL");
        GEO::Delaunay_var delaunay = GEO::Delaunay::create(GEO::coord_index_t(dim));
        delaunay->set_keeps_infinite(true);
        delaunay->set_vertices(nb_pts, points.data());
        destination.vertices.set_dimension(dim);
        GEO::vector<GEO::index_t> triangles_indices;
        for (GEO::index_t t = delaunay->nb_finite_cells(); t < delaunay->nb_cells(); ++t) {
            for (GEO::index_t lv = 0; lv < 4; ++lv) {
                GEO::signed_index_t v = delaunay->cell_vertex(t, lv);
                if (v != GEO::NO_INDEX) {
                    triangles_indices.push_back(GEO::index_t(v));
                }
            }
        }
        destination.facets.assign_triangle_mesh(3, points, triangles_indices, true);
        destination.vertices.remove_isolated();
        destination.vertices.set_single_precision();
        return true;
    } catch (...) {
        return false;
    }
}

void copy_attributes(const Mesh_attributes& s, Mesh_attributes& d)
{
    copy_attribute<GEO::vec3f>(s.facet_id,               d.facet_id              );
    copy_attribute<GEO::vec3f>(s.facet_centroid,         d.facet_centroid        );
    copy_attribute<GEO::vec4f>(s.facet_color_0,          d.facet_color_0         );
    copy_attribute<GEO::vec4f>(s.facet_color_1,          d.facet_color_1         );
    copy_attribute<GEO::vec3f>(s.facet_normal,           d.facet_normal          );
    copy_attribute<GEO::vec4f>(s.facet_tangent,          d.facet_tangent         );
    copy_attribute<GEO::vec3f>(s.facet_bitangent,        d.facet_bitangent       );
    copy_attribute<GEO::vec2f>(s.facet_aniso_control,    d.facet_aniso_control   );
    copy_attribute<GEO::vec3f>(s.vertex_normal,          d.vertex_normal         );
    copy_attribute<GEO::vec3f>(s.vertex_normal_smooth,   d.vertex_normal_smooth  );
    copy_attribute<GEO::vec2f>(s.vertex_texcoord_0,      d.vertex_texcoord_0     );
    copy_attribute<GEO::vec2f>(s.vertex_texcoord_1,      d.vertex_texcoord_1     );
    copy_attribute<GEO::vec4f>(s.vertex_tangent,         d.vertex_tangent        );
    copy_attribute<GEO::vec3f>(s.vertex_bitangent,       d.vertex_bitangent      );
    copy_attribute<GEO::vec4f>(s.vertex_color_0,         d.vertex_color_0        );
    copy_attribute<GEO::vec4f>(s.vertex_color_1,         d.vertex_color_1        );
    copy_attribute<GEO::vec4f>(s.vertex_joint_weights_0, d.vertex_joint_weights_0);
    copy_attribute<GEO::vec4f>(s.vertex_joint_weights_1, d.vertex_joint_weights_1);
    copy_attribute<GEO::vec4u>(s.vertex_joint_indices_0, d.vertex_joint_indices_0);
    copy_attribute<GEO::vec4u>(s.vertex_joint_indices_1, d.vertex_joint_indices_1);
    copy_attribute<GEO::vec2f>(s.vertex_aniso_control,   d.vertex_aniso_control  );
    copy_attribute<GEO::vec3f>(s.corner_normal,          d.corner_normal         );
    copy_attribute<GEO::vec2f>(s.corner_texcoord_0,      d.corner_texcoord_0     );
    copy_attribute<GEO::vec2f>(s.corner_texcoord_1,      d.corner_texcoord_1     );
    copy_attribute<GEO::vec4f>(s.corner_tangent,         d.corner_tangent        );
    copy_attribute<GEO::vec3f>(s.corner_bitangent,       d.corner_bitangent      );
    copy_attribute<GEO::vec4f>(s.corner_color_0,         d.corner_color_0        );
    copy_attribute<GEO::vec4f>(s.corner_color_1,         d.corner_color_1        );
    copy_attribute<GEO::vec2f>(s.corner_aniso_control,   d.corner_aniso_control  );
}

void merge_attributes(GEO::AttributesManager& dst, GEO::AttributesManager& src, GEO::index_t dst_base_element)
{
    const GEO::index_t src_element_count = src.size();
    GEO::vector<std::string> attribute_names;
    src.list_attribute_names(attribute_names);
    for (std::string& attribute_name : attribute_names) {
        GEO::AttributeStore* src_attribute_store = src.find_attribute_store(attribute_name);
        GEO::AttributeStore* dst_attribute_store = dst.find_attribute_store(attribute_name);
        if (dst_attribute_store == nullptr) {
            dst_attribute_store = GEO::AttributeStore::create_attribute_store_by_element_type_name(
                src_attribute_store->element_typeid_name(),
                src_attribute_store->dimension()
            );
            dst.bind_attribute_store(attribute_name, dst_attribute_store);
        }
        const GEO::index_t new_size = dst_base_element + src_element_count;
        dst_attribute_store->resize(new_size);
        const std::size_t  element_size = src_attribute_store->element_size();
        const std::size_t  item_size    = element_size * src_attribute_store->dimension();
        const std::byte*   src_data     = static_cast<const std::byte*>(src_attribute_store->data());
              std::byte*   dst_data     = static_cast<      std::byte*>(dst_attribute_store->data());
        const std::size_t  dst_offset   = dst_base_element  * item_size;
        const std::size_t  byte_count   = src_element_count * item_size;
        memcpy(dst_data + dst_offset, src_data, byte_count);
    }
}

void transform_mesh(
    const GEO::Mesh&       source_mesh,
    const Mesh_attributes& source_attributes,
    GEO::Mesh&             destination_mesh,
    Mesh_attributes&       destination_attributes,
    const GEO::mat4f&      transform
)
{
    if (transform.is_identity()) {
        if (&source_mesh != &destination_mesh) {
            destination_attributes.unbind();
            destination_mesh.copy(source_mesh, true);
            destination_attributes.bind();
            copy_attributes(source_attributes, destination_attributes);
        }
        return;
    }

    if (&source_mesh != &destination_mesh) {
        destination_attributes.unbind();
        destination_mesh.copy(source_mesh, false);
        destination_attributes.bind();
    }

    const Mesh_attributes& s = source_attributes;
    Mesh_attributes& d = destination_attributes;

    for (GEO::index_t vertex : source_mesh.vertices) {
        const GEO::vec3f p           = get_pointf(source_mesh.vertices, vertex);
        const GEO::vec3f transformed = GEO::transform_point(transform, p);
        set_pointf(destination_mesh.vertices, vertex, transformed);
    }

    if (transform.is_identity()) {
        copy_attributes(s, d);
        return;
    } else {
        // TODO: Recompute facet_id, facet_centroid, facet_normal ?
        transform_attribute<GEO::vec3f>(s.facet_id,               d.facet_id              , transform);
        transform_attribute<GEO::vec3f>(s.facet_centroid,         d.facet_centroid        , transform);
        transform_attribute<GEO::vec4f>(s.facet_color_0,          d.facet_color_0         , transform);
        transform_attribute<GEO::vec4f>(s.facet_color_1,          d.facet_color_1         , transform);
        transform_attribute<GEO::vec3f>(s.facet_normal,           d.facet_normal          , transform);
        transform_attribute<GEO::vec4f>(s.facet_tangent,          d.facet_tangent         , transform);
        transform_attribute<GEO::vec3f>(s.facet_bitangent,        d.facet_bitangent       , transform);
        transform_attribute<GEO::vec2f>(s.facet_aniso_control,    d.facet_aniso_control   , transform);
        transform_attribute<GEO::vec3f>(s.vertex_normal,          d.vertex_normal         , transform);
        transform_attribute<GEO::vec3f>(s.vertex_normal_smooth,   d.vertex_normal_smooth  , transform);
        transform_attribute<GEO::vec2f>(s.vertex_texcoord_0,      d.vertex_texcoord_0     , transform);
        transform_attribute<GEO::vec2f>(s.vertex_texcoord_1,      d.vertex_texcoord_1     , transform);
        transform_attribute<GEO::vec4f>(s.vertex_tangent,         d.vertex_tangent        , transform);
        transform_attribute<GEO::vec3f>(s.vertex_bitangent,       d.vertex_bitangent      , transform);
        transform_attribute<GEO::vec4f>(s.vertex_color_0,         d.vertex_color_0        , transform);
        transform_attribute<GEO::vec4f>(s.vertex_color_1,         d.vertex_color_1        , transform);
        transform_attribute<GEO::vec4f>(s.vertex_joint_weights_0, d.vertex_joint_weights_0, transform);
        transform_attribute<GEO::vec4f>(s.vertex_joint_weights_1, d.vertex_joint_weights_1, transform);
        transform_attribute<GEO::vec4u>(s.vertex_joint_indices_0, d.vertex_joint_indices_0, transform);
        transform_attribute<GEO::vec4u>(s.vertex_joint_indices_1, d.vertex_joint_indices_1, transform);
        transform_attribute<GEO::vec2f>(s.vertex_aniso_control,   d.vertex_aniso_control  , transform);
        transform_attribute<GEO::vec3f>(s.corner_normal,          d.corner_normal         , transform);
        transform_attribute<GEO::vec2f>(s.corner_texcoord_0,      d.corner_texcoord_0     , transform);
        transform_attribute<GEO::vec2f>(s.corner_texcoord_1,      d.corner_texcoord_1     , transform);
        transform_attribute<GEO::vec4f>(s.corner_tangent,         d.corner_tangent        , transform);
        transform_attribute<GEO::vec3f>(s.corner_bitangent,       d.corner_bitangent      , transform);
        transform_attribute<GEO::vec4f>(s.corner_color_0,         d.corner_color_0        , transform);
        transform_attribute<GEO::vec4f>(s.corner_color_1,         d.corner_color_1        , transform);
        transform_attribute<GEO::vec2f>(s.corner_aniso_control,   d.corner_aniso_control  , transform);
    }

    const float transform_determinant = GEO::det(transform);
    if (transform_determinant < 0.0) {
        for (GEO::index_t facet : destination_mesh.facets) {
            destination_mesh.facets.flip(facet);
        }
    }
}

void transform_mesh(const GEO::Mesh& source_mesh, GEO::Mesh& destination_mesh, const GEO::mat4f& transform)
{
    const Mesh_attributes source_attributes{source_mesh};
    Mesh_attributes destination_attributes{destination_mesh};
    transform_mesh(source_mesh, source_attributes, destination_mesh, destination_attributes, transform);
}

void transform_mesh(GEO::Mesh& mesh, const GEO::mat4f& transform)
{
    Mesh_attributes attributes{mesh};
    transform_mesh(mesh, attributes, mesh, attributes, transform);
}

namespace erhe::geometry {

Geometry::Geometry()
    : m_mesh      {3, true}
    , m_attributes{m_mesh}
{
}

Geometry::Geometry(std::string_view name)
    : m_mesh      {3, true}
    , m_attributes{m_mesh}
    , m_name      {name}
{
}

void Geometry::merge_with_transform(const Geometry& src, const GEO::mat4f& transform)
{
    const GEO::Mesh& src_mesh = src.get_mesh();
    GEO::Mesh& dst_mesh = get_mesh();

    // Append vertices
    const GEO::index_t src_vertex_count = src_mesh.vertices.nb();
    const GEO::index_t dst_base_vertex  = dst_mesh.vertices.create_vertices(src_vertex_count);
    log_geometry->trace("adding {} vertices - base vertex = {}", src_vertex_count, dst_base_vertex);

    get_attributes().unbind();
    //dst_mesh.vertices.unbind();
    merge_attributes(dst_mesh.vertices.attributes(), src_mesh.vertices.attributes(), dst_base_vertex);
    //dst_mesh.vertices.bind();
    get_attributes().bind();

    for (GEO::index_t src_vertex : src_mesh.vertices) {
        const GEO::vec3f   p           = get_pointf(src_mesh.vertices, src_vertex);
        const GEO::vec3f   transformed = GEO::transform_point(transform, p);
        const GEO::index_t dst_vertex  = dst_base_vertex + src_vertex;
        set_pointf(dst_mesh.vertices, dst_vertex, transformed);
        log_geometry->trace("add transformed src vertex {} dst vertex {} position = {}", src_vertex, dst_vertex, transformed);
    }

    // Append facets
    const GEO::index_t src_facet_count = src_mesh.facets.nb();
    const GEO::index_t dst_base_corner = dst_mesh.facet_corners.nb();
    const GEO::index_t dst_base_facet  = dst_mesh.facets.nb();
    log_geometry->trace(
        "adding {} facets - base facet = {}, src facet corner count = {}, dst facet corner base = {}",
        src_facet_count, dst_base_facet, src_mesh.facet_corners.nb(), dst_base_corner
    );
    for (GEO::index_t src_facet : src_mesh.facets) {
        const GEO::index_t corner_count = src_mesh.facets.nb_corners(src_facet);
        const GEO::index_t dst_facet    = dst_mesh.facets.create_polygon(corner_count);
        log_geometry->trace("src facet {} dst_mesh facet {} with {} corners", src_facet, dst_facet, corner_count);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t dst_corner = dst_mesh.facets.corner(dst_facet, local_facet_corner);
            const GEO::index_t src_corner = src_mesh.facets.corner(src_facet, local_facet_corner);
            const GEO::index_t src_vertex = src_mesh.facet_corners.vertex(src_corner);
            const GEO::index_t dst_vertex = dst_base_vertex + src_vertex;
            dst_mesh.facet_corners.set_vertex(dst_corner, dst_vertex);
            log_geometry->trace(
                "    corner {} - src corner {} dst corner {} src vertex {} dst vertex {}",
                local_facet_corner,
                src_corner, dst_corner,
                src_vertex, dst_vertex
            );
        }
    }
    get_attributes().unbind();
    //dst_mesh.vertices.unbind();
    merge_attributes(dst_mesh.facets       .attributes(), src_mesh.facets       .attributes(), dst_base_facet);
    merge_attributes(dst_mesh.facet_corners.attributes(), src_mesh.facet_corners.attributes(), dst_base_corner);
    //dst_mesh.vertices.bind();
    get_attributes().bind();

    // Append edges
    const GEO::index_t src_edge_count = src_mesh.edges.nb();
    const GEO::index_t dst_base_edge  = dst_mesh.edges.create_edges(src_edge_count);
    log_geometry->trace("adding {} edges - dst base edge = {}", src_edge_count, dst_base_edge);
    for (GEO::index_t src_edge : src_mesh.edges) {
        const GEO::index_t src_vertex_0 = src_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t src_vertex_1 = src_mesh.edges.vertex(src_edge, 1);
        const GEO::index_t dst_vertex_0 = dst_base_vertex + src_vertex_0;
        const GEO::index_t dst_vertex_1 = dst_base_vertex + src_vertex_1;
        const GEO::index_t dst_edge     = dst_base_edge + src_edge;
        dst_mesh.edges.set_vertex(dst_edge, 0, dst_vertex_0);
        dst_mesh.edges.set_vertex(dst_edge, 1, dst_vertex_1);
        log_geometry->trace(
            "src edge {} dst edge {} vertex 0 src = {} dst = {}, vertex 1 src = {} dst = {}",
            src_edge, dst_edge,
            src_vertex_0, dst_vertex_0,
            src_vertex_1, dst_vertex_1
        );
    }
    get_attributes().unbind();
    //dst_mesh.vertices.unbind();
    merge_attributes(dst_mesh.edges.attributes(), src_mesh.edges.attributes(), dst_base_edge);
    //dst_mesh.vertices.bind();
    get_attributes().bind();

    dst_mesh.facets.connect();
}

void transform(const Geometry& source, Geometry& destination, const GEO::mat4f& transform)
{
    transform_mesh(source.get_mesh(), source.get_attributes(), destination.get_mesh(), destination.get_attributes(), transform);
}

void Geometry::copy_with_transform(const Geometry& source, const GEO::mat4f& transform_)
{
    // TODO add m_mesh.clear(false, false) ?
    transform(source, *this, transform_);

    if (&source != this) {
        m_name                = source.get_name();
        m_vertex_to_corners   = source.m_vertex_to_corners;
        m_corner_to_facet     = source.m_corner_to_facet;
        m_edge_to_facets      = source.m_edge_to_facets;
        m_vertex_to_edges     = source.m_vertex_to_edges;
        m_vertex_pair_to_edge = source.m_vertex_pair_to_edge;
    }
}

auto Geometry::get_name() const -> const std::string&
{
    return m_name;
}

auto Geometry::get_mesh() -> GEO::Mesh&
{
    return m_mesh;
}

auto Geometry::get_mesh() const -> const GEO::Mesh&
{
    return m_mesh;
}

auto Geometry::get_vertex_corners(GEO::index_t vertex) const -> const std::vector<GEO::index_t>&
{
    return m_vertex_to_corners[vertex];
}

auto Geometry::get_vertex_edges(GEO::index_t vertex) const -> const std::vector<GEO::index_t>&
{
    return m_vertex_to_edges[vertex];
}

auto Geometry::get_corner_facet(GEO::index_t corner) const -> GEO::index_t
{
    return m_corner_to_facet[corner];
}

auto Geometry::get_edge_facets(GEO::index_t edge) const -> const std::vector<GEO::index_t>&
{
    ERHE_VERIFY(edge < m_edge_to_facets.size());
    return m_edge_to_facets[edge];
}

auto Geometry::get_edge(const GEO::index_t v0, const GEO::index_t v1) const -> GEO::index_t
{
    ERHE_VERIFY(v0 != v1);
    const GEO::index_t lo = std::min(v0, v1);
    const GEO::index_t hi = std::max(v0, v1);
    const auto i = m_vertex_pair_to_edge.find({lo, hi});
    if (i != m_vertex_pair_to_edge.end()) {
        return i->second;
    }
    return GEO::NO_EDGE;
}

auto Geometry::get_attributes() -> Mesh_attributes&
{
    return m_attributes;
}

auto Geometry::get_attributes() const -> const Mesh_attributes&
{
    return m_attributes;
}

void Geometry::set_name(std::string_view name)
{
    m_name = std::string{name};
}

void Geometry::build_edges()
{
    m_mesh.edges.clear();

    GEO::index_t facet_edge_count = 0;

    m_vertex_pair_to_edge.clear();
    m_vertex_to_edges.clear();
    m_vertex_to_edges.resize(m_mesh.vertices.nb());

    // First pass - shared edges
    for (GEO::index_t facet : m_mesh.facets) {
        const GEO::index_t facet_corner_count = m_mesh.facets.nb_corners(facet);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < facet_corner_count; ++local_facet_corner) {
            const GEO::index_t corner       = m_mesh.facets.corner(facet, local_facet_corner);
            const GEO::index_t next_corner_ = m_mesh.facets.corner(facet, (local_facet_corner + 1) % facet_corner_count);
            const GEO::index_t next_corner  = m_mesh.facets.next_corner_around_facet(facet, corner);
            ERHE_VERIFY(next_corner_ == next_corner);
            const GEO::index_t a            = m_mesh.facet_corners.vertex(corner);
            const GEO::index_t b            = m_mesh.facet_corners.vertex(next_corner);
            ++facet_edge_count;
            ERHE_VERIFY(a != b);
            if (a < b) { // This does not work for non-shared edges going wrong direction
                const GEO::index_t edge = m_mesh.edges.create_edge(a, b);
                const std::pair<GEO::index_t, GEO::index_t> key{a, b};
                m_vertex_pair_to_edge.insert({key, edge});
                m_vertex_to_edges[a].push_back(edge);
                m_vertex_to_edges[b].push_back(edge);

                //erhe::geometry::log_geometry->info(
                //    "pass 1: created edge {} from facet {} corners {} and {} - vertices {} and {}",
                //    edge, facet, corner, next_corner, a, b
                //);
            //} else {
            //    erhe::geometry::log_geometry->info(
            //        "pass 1: NOT created edge from facet {} corners {} and {} - vertices {} and {}",
            //        facet, corner, next_corner, a, b
            //    );
            }
        }
    }

    // Second pass - non-shared edges wrong direction or non-manifold wrong direction
        for (GEO::index_t facet : m_mesh.facets) {
            const GEO::index_t facet_corner_count = m_mesh.facets.nb_corners(facet);
            for (GEO::index_t local_facet_corner = 0; local_facet_corner < facet_corner_count; ++local_facet_corner) {
                const GEO::index_t corner      = m_mesh.facets.corner(facet, local_facet_corner);
                const GEO::index_t next_corner = m_mesh.facets.corner(facet, (local_facet_corner + 1) % facet_corner_count);
                const GEO::index_t a           = m_mesh.facet_corners.vertex(corner);
                const GEO::index_t b           = m_mesh.facet_corners.vertex(next_corner);
            const GEO::index_t lo          = std::min(a, b);
            const GEO::index_t hi          = std::max(a, b);
            ERHE_VERIFY(lo != hi);
            const std::pair<GEO::index_t, GEO::index_t> key{lo, hi};
            const auto i = m_vertex_pair_to_edge.find(key);
            if (i != m_vertex_pair_to_edge.end()) {
                //const GEO::index_t edge = i->second;
                //erhe::geometry::log_geometry->info(
                //    "pass 2: found existing edge {} from facet {} corners {} and {} - vertices {} and {}",
                //    edge, facet, corner, next_corner, a, b
                //);
                continue;
            }
            const GEO::index_t edge = m_mesh.edges.create_edge(lo, hi);
            m_vertex_pair_to_edge.insert({key, edge});
            m_vertex_to_edges[a].push_back(edge);
            m_vertex_to_edges[b].push_back(edge);

            //erhe::geometry::log_geometry->info(
            //    "pass 2: created edge {} from facet {} corners {} and {} - vertices {} and {}",
            //    edge, facet, corner, next_corner, a, b
            //);
        }
    }

    m_edge_to_facets.clear();
    const GEO::index_t edge_count = m_mesh.edges.nb();
    m_edge_to_facets.resize(edge_count);
    for (GEO::index_t facet : m_mesh.facets) {
        const GEO::index_t facet_corner_count = m_mesh.facets.nb_corners(facet);
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < facet_corner_count; ++local_facet_corner) {
            const GEO::index_t corner      = m_mesh.facets.corner(facet, local_facet_corner);
            const GEO::index_t next_corner = m_mesh.facets.corner(facet, (local_facet_corner + 1) % facet_corner_count);
            const GEO::index_t a           = m_mesh.facet_corners.vertex(corner);
            const GEO::index_t b           = m_mesh.facet_corners.vertex(next_corner);
            const GEO::index_t lo          = std::min(a, b);
            const GEO::index_t hi          = std::max(a, b);
            ERHE_VERIFY(lo != hi);
            const std::pair<GEO::index_t, GEO::index_t> key{lo, hi};
            const auto edge_i = m_vertex_pair_to_edge.find(key);
            ERHE_VERIFY(m_vertex_pair_to_edge.find(key) != m_vertex_pair_to_edge.end());
            const GEO::index_t edge = edge_i->second;
            ERHE_VERIFY(edge < edge_count);
            m_edge_to_facets[edge].push_back(facet);
            //m_vertex_to_edges[lo].push_back(edge);
            //m_vertex_to_edges[hi].push_back(edge);
        }
    }
}

void Geometry::process(const uint64_t flags)
{
    if (flags & process_flag_connect) {
        m_mesh.facets.connect();
    }
    if (flags & process_flag_build_edges) {
        update_connectivity();
        build_edges();
    }
    if (flags & process_flag_merge_coplanar_neighbors) {
        merge_coplanar_neighbors();
        update_connectivity();
        build_edges();
    }
    if (flags & process_flag_compute_smooth_vertex_normals) {
        compute_mesh_vertex_normal_smooth(m_mesh, m_attributes);
    }
    if (flags & process_flag_compute_facet_centroids) {
        compute_facet_centroids(m_mesh, m_attributes);
    }
    if (flags & process_flag_generate_facet_texture_coordinates) {
        generate_mesh_facet_texture_coordinates();
    }
    if (flags & process_flag_debug_trace) {
        debug_trace();
    }
}

void Geometry::generate_mesh_facet_texture_coordinates()
{
    ::generate_mesh_facet_texture_coordinates(m_mesh, m_attributes);
}

void build_extra_connectivity(
    GEO::Mesh&                              mesh,
    std::vector<std::vector<GEO::index_t>>& vertex_to_corners,
    std::vector<GEO::index_t>&              corner_to_facet
)
{
    const GEO::index_t corner_count = mesh.facet_corners.nb();
    const GEO::index_t vertex_count = mesh.vertices.nb();
    corner_to_facet.resize(corner_count);
    vertex_to_corners.clear();
    vertex_to_corners.resize(vertex_count);
    for (GEO::index_t corner : mesh.facet_corners) {
        const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
        vertex_to_corners[vertex].push_back(corner);
        corner_to_facet[corner] = GEO::NO_INDEX;
    }
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            corner_to_facet[corner] = facet;
        }
    }

    class Vertex_corner_info
    {
    public:
        GEO::index_t vertex     {GEO::NO_INDEX};
        GEO::index_t corner     {GEO::NO_INDEX};
        GEO::index_t prev_vertex{GEO::NO_INDEX};
        GEO::index_t next_vertex{GEO::NO_INDEX};
        bool         used       {false};
    };

    std::vector<Vertex_corner_info> vertex_corner_infos;
    vertex_corner_infos.reserve(20);

    for (GEO::index_t vertex : mesh.vertices) {
        std::vector<GEO::index_t>& vertex_corners = vertex_to_corners[vertex];
        if (vertex_corners.size() < 3) {
            return;
        }
        vertex_corner_infos.clear();

        // for each corner, find the matching facet, and record what are prev and next vertices in that facet
        for (GEO::index_t vertex_corner : vertex_corners) {
            const GEO::index_t facet              = corner_to_facet[vertex_corner];
            const GEO::index_t facet_corner_count = mesh.facets.nb_corners(facet);
            for (GEO::index_t local_facet_corner = 0; local_facet_corner < facet_corner_count; ++local_facet_corner) {
                const GEO::index_t facet_corner = mesh.facets.corner(facet, local_facet_corner);
                if (facet_corner == vertex_corner) {
                    const GEO::index_t prev_facet_corner = mesh.facets.corner(facet, (local_facet_corner + facet_corner_count - 1) % facet_corner_count);
                    const GEO::index_t next_facet_corner = mesh.facets.corner(facet, (local_facet_corner + 1) % facet_corner_count);
                    vertex_corner_infos.push_back(
                        Vertex_corner_info{
                            .vertex      = vertex,
                            .corner      = vertex_corner,
                            .prev_vertex = mesh.facet_corners.vertex(prev_facet_corner),
                            .next_vertex = mesh.facet_corners.vertex(next_facet_corner),
                            .used        = false
                        }
                    );
                    break;
                }
            }
        }

        // Find chained entries
        for (GEO::index_t left_slot = 0, end = static_cast<GEO::index_t>(vertex_corner_infos.size()); left_slot < end; ++left_slot) {
            const GEO::index_t  left_next_slot = (left_slot + 1) % end;
            Vertex_corner_info& left_entry     = vertex_corner_infos[left_slot];
            Vertex_corner_info& next           = vertex_corner_infos[left_next_slot];
            for (GEO::index_t right_slot = 0; right_slot< end; ++right_slot) {
                Vertex_corner_info& right_entry = vertex_corner_infos[right_slot];
                if (right_entry.used) {
                    continue;
                }
                if (right_entry.next_vertex == left_entry.prev_vertex) {
                    right_entry.used = true;
                    if (right_slot != left_next_slot) {
                        std::swap(next, right_entry);
                    }
                    break;
                }
            }
            vertex_corners[left_slot] = left_entry.corner;
        }
    }
}

void Geometry::update_connectivity()
{
    build_extra_connectivity(m_mesh, m_vertex_to_corners, m_corner_to_facet);
}


void Geometry::collect_corners_from_facet(Edge_collapse_context& context, GEO::index_t facet, std::optional<GEO::index_t> trigger_vertex)
{
    context.facets_to_delete.push_back(facet);

    const GEO::index_t corner_count = m_mesh.facets.nb_corners(facet);

    GEO::index_t corner_rotation = 0;
    if (trigger_vertex.has_value()) {
        for (GEO::index_t local_facet_corner = 0; local_facet_corner < corner_count; ++local_facet_corner) {
            const GEO::index_t corner = m_mesh.facets.corner(facet, local_facet_corner);
            const GEO::index_t vertex = m_mesh.facet_corners.vertex(corner);
            if (vertex == trigger_vertex.value()) {
                // log_geometry->debug("Found trigger vertex {} from local corner {}", vertex, local_facet_corner);
                corner_rotation = local_facet_corner;
                break;
            }
        }
    }

    for (GEO::index_t local_facet_corner_ = 0; local_facet_corner_ < corner_count; ++local_facet_corner_) {
        const GEO::index_t local_facet_corner = (local_facet_corner_ + corner_rotation) % corner_count;
        const GEO::index_t local_edge         = local_facet_corner;
        const GEO::index_t corner             = m_mesh.facets.corner(facet, local_facet_corner);
        const GEO::index_t vertex             = m_mesh.facet_corners.vertex(corner);
        const GEO::index_t adjacent_facet     = m_mesh.facets.adjacent(facet, local_edge);
        if (!context.is_edge_facet(adjacent_facet)) {
            // log_geometry->trace("Add vertex {} corner {} from facet {}", vertex, corner, facet);
            context.merged_face_corners.push_back(vertex);
        } else {
            if (!context.is_started(adjacent_facet)) {
                // log_geometry->trace("Edge vertex {} corner {} from facet {} -> adjacent facet {} call", vertex, corner, facet, adjacent_facet);
                collect_corners_from_facet(context, adjacent_facet, vertex);
            }
            // else {
            //     log_geometry->trace("Edge vertex {} corner {} from facet {} -> adjacent facet {} already done", vertex, corner, facet, adjacent_facet);
            // }
        }
    }
}

void Geometry::merge_coplanar_neighbors()
{
    constexpr float epsilon = 0.99f;

    std::vector<GEO::index_t> edges_to_merge;
    for (GEO::index_t edge : m_mesh.edges) {
        std::optional<GEO::vec3f> reference_normal;
        bool can_merge = true;
        for (GEO::index_t facet : m_edge_to_facets.at(edge)) {
            GEO::vec3f facet_normal = mesh_facet_normalf(m_mesh, facet);
            if (!reference_normal.has_value()) {
                reference_normal = facet_normal;
                continue;
            }
            float dp = GEO::dot(reference_normal.value(), facet_normal);
            if (dp < epsilon) {
                can_merge = false;
                break;
            }
        }

        if (can_merge) {
            edges_to_merge.push_back(edge);
        }
    }

    // {
    //     std::stringstream ss;
    //     ss << fmt::format("Found {} mergable edges:", edges_to_merge.size());
    //     for (GEO::index_t edge : edges_to_merge) {
    //         ss << " " << edge;
    //     }
    //     log_geometry->debug(ss.str());
    // }

    GEO::vector<GEO::index_t> facets_to_delete;
    std::vector<std::vector<GEO::index_t>> new_facets;
    for (GEO::index_t edge : edges_to_merge) {
        Edge_collapse_context context{
            .edge             = edge,
            .v0               = m_mesh.edges.vertex(edge, 0),
            .v1               = m_mesh.edges.vertex(edge, 1),
            .facets_to_delete = facets_to_delete,
            .edge_facets      = m_edge_to_facets.at(edge)
        };
        ERHE_VERIFY(context.edge_facets.size() == 2);

        // log_geometry->debug("  Mergable edge {} vertex {} to {}", edge, context.v0, context.v1);

        collect_corners_from_facet(context, context.edge_facets.front(), {});

        // std::stringstream ss;
        // ss << "    Original facets";
        // for (GEO::index_t facet : context.facets_to_delete) {
        //     ss << " " << facet;
        // }
        // ss << " - combined corners:";
        // for (GEO::index_t corner : context. merged_face_corners) {
        //     ss << " " << corner;
        // }
        // log_geometry->debug("{}", ss.str());

        new_facets.push_back(std::move(context.merged_face_corners));
    }
    m_mesh.facets.delete_elements(facets_to_delete);
    for (const auto& new_facet_corners : new_facets) {
        GEO::index_t corner_count = static_cast<GEO::index_t>(new_facet_corners.size());
        GEO::index_t new_facet = m_mesh.facets.create_polygon(corner_count);
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            m_mesh.facets.set_vertex(new_facet, local_corner, new_facet_corners[local_corner]);
        }
    }
    m_mesh.facets.connect();
}

void Geometry::debug_trace() const
{
    for (GEO::index_t corner : m_mesh.facet_corners) {
        std::stringstream ss;
        ss << fmt::format("corner {:2} = vertex {:2}", corner, m_mesh.facet_corners.vertex(corner));
        if (corner < m_corner_to_facet.size()) {
            ss << fmt::format(" facet {:2}", m_corner_to_facet.at(corner));
        }
        log_geometry->info(ss.str());
    }

    for (GEO::index_t vertex : m_mesh.vertices) {
        GEO::vec3f position = get_pointf(m_mesh.vertices, vertex);
        log_geometry->info("vertex {:2} position = {}", vertex, position);
        if (vertex < m_vertex_to_corners.size()) {
            std::stringstream corners_ss;
            std::stringstream facets_ss;
            corners_ss << fmt::format("vertex {:2} corners = ", vertex);
            facets_ss  << fmt::format("vertex {:2} facets = ", vertex);
            bool first = true;
            bool have_facets = true;
            for (GEO::index_t corner : m_vertex_to_corners.at(vertex)) {
                if (!first) {
                    corners_ss << ", ";
                    facets_ss << ", ";
                }
                corners_ss << fmt::format("{:2}", corner);
                if (corner < m_corner_to_facet.size()) {
                    facets_ss << fmt::format("{:2}", m_corner_to_facet.at(corner));
                } else {
                    have_facets = false;
                }
                first = false;
            }
            log_geometry->info("{}", corners_ss.str());
            if (have_facets) {
                log_geometry->info("{}", facets_ss.str());
            }
        }
    }

    for (GEO::index_t facet : m_mesh.facets) {
        std::stringstream corners_ss;
        std::stringstream vertices_ss;
        corners_ss  << fmt::format("facet {:2} corners = ", facet);
        vertices_ss << fmt::format("facet {:2} vertices = ", facet);
        bool first = true;
        for (GEO::index_t corner : m_mesh.facets.corners(facet)) {
            if (!first) {
                corners_ss  << ", ";
                vertices_ss <<", ";
            }
            corners_ss  << fmt::format("{:2}", corner);
            vertices_ss << fmt::format("{:2}", m_mesh.facet_corners.vertex(corner));
            first = false;
        };
        log_geometry->info("{}", corners_ss.str());
        log_geometry->info("{}", vertices_ss.str());
    }

    for (GEO::index_t edge : m_mesh.edges) {
        std::stringstream ss;
        const GEO::index_t vertex_0 = m_mesh.edges.vertex(edge, 0);
        const GEO::index_t vertex_1 = m_mesh.edges.vertex(edge, 1);
        ss << fmt::format("edge {:2} = {:2} .. {:2}", edge, vertex_0, vertex_1);
        if (edge < m_edge_to_facets.size()) {
            ss << " : ";
            for (GEO::index_t facet : m_edge_to_facets.at(edge)) {
                ss << fmt::format("{:2} ", facet);
            }
        }
        log_geometry->info("{}", ss.str());
    }
}

void Geometry::clear_debug()
{
    m_debug_texts.clear();
    m_debug_lines.clear();
}

void Geometry::add_debug_text(GEO::index_t reference_vertex, GEO::index_t reference_facet, glm::vec3 position, uint32_t color, std::string_view text) const
{
    m_debug_texts.push_back(
        Debug_text{
            .reference_vertex = reference_vertex,
            .reference_facet  = reference_facet,
            .position         = position,
            .color            = color,
            .text             = std::string{text}
        }
    );
}

void Geometry::add_debug_line(GEO::index_t reference_vertex, GEO::index_t reference_facet, glm::vec3 p0, glm::vec3 p1, glm::vec4 color, float width) const
{
    m_debug_lines.push_back(
        Debug_line{
            reference_vertex,
            reference_facet,
            {
                Debug_vertex{ .position = p0, .color = color, .width = width},
                Debug_vertex{ .position = p1, .color = color, .width = width}
            }
        }
    );
}

void Geometry::access_debug_entries(std::function<void(std::vector<Debug_text>& debug_texts, std::vector<Debug_line>& debug_lines)> op)
{
    op(m_debug_texts, m_debug_lines);
}

} // namespace erhe::geometry

