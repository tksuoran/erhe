#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"

#include <gtest/gtest.h>

#include <geogram/mesh/mesh.h>

// A Geometry with no facets is a legal mesh operation result (every face
// deleted, a merge by distance that collapses the whole mesh). A Primitive
// built from it is a valid empty primitive: the renderable build succeeds with
// an empty Buffer_mesh (no ranges, no allocations), the raytrace build
// succeeds with no IGeometry, and nothing reports triangles.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;

[[nodiscard]] auto make_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position, 0},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0}
            }
        }
    };
}

[[nodiscard]] auto make_processed_geometry(const std::size_t lone_vertex_count) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("empty");
    GEO::Mesh& mesh = geometry->get_mesh();
    for (std::size_t i = 0; i < lone_vertex_count; ++i) {
        const GEO::index_t vertex = mesh.vertices.create_vertex();
        erhe::geometry::set_pointf(mesh.vertices, vertex, GEO::vec3f{0.25f, 0.5f, 0.75f});
    }
    // The flags Mesh_operation processes an operation result with.
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    return geometry;
}

void check_empty_primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
{
    ASSERT_EQ(geometry->get_mesh().facets.nb(), 0u);

    const Vertex_format format = make_format();

    erhe::buffer::Cpu_buffer                vertex_buffer{"test_vertex", 1024};
    erhe::buffer::Cpu_buffer                index_buffer {"test_index",  1024};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_buffer_sink {index_buffer};

    erhe::primitive::Primitive primitive{geometry};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = {
            .normal_style       = erhe::primitive::Normal_style::corner_normals,
            .index_type         = Format::format_32_scalar_uint,
            .vertex_format      = format,
            .vertex_buffer_sink = vertex_buffer_sink,
            .index_buffer_sink  = index_buffer_sink
        },
        .normal_style = erhe::primitive::Normal_style::corner_normals
    };

    EXPECT_TRUE(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    EXPECT_TRUE(primitive.make_raytrace());

    ASSERT_TRUE(primitive.render_shape);
    EXPECT_FALSE(primitive.optimized_render_shape);
    EXPECT_FALSE(primitive.has_renderable_triangles());
    EXPECT_FALSE(primitive.has_raytrace_triangles());

    const erhe::primitive::Buffer_mesh& buffer_mesh = primitive.render_shape->get_renderable_mesh();
    EXPECT_TRUE(buffer_mesh.vertex_buffer_ranges.empty());
    EXPECT_EQ(buffer_mesh.index_buffer_range.count, 0u);
    for (const erhe::primitive::Primitive_mode mode : {
        erhe::primitive::Primitive_mode::polygon_fill,
        erhe::primitive::Primitive_mode::edge_lines,
        erhe::primitive::Primitive_mode::corner_points,
        erhe::primitive::Primitive_mode::polygon_centroids
    }) {
        EXPECT_EQ(buffer_mesh.index_range(mode).index_count, 0u) << erhe::primitive::c_str(mode);
    }
    // Nothing was allocated from the sinks.
    EXPECT_EQ(vertex_buffer.get_used_byte_count(), 0u);
    EXPECT_EQ(index_buffer.get_used_byte_count(), 0u);

    const std::shared_ptr<erhe::primitive::Primitive_shape> raytrace_shape = primitive.get_shape_for_raytrace();
    ASSERT_TRUE(raytrace_shape);
    EXPECT_FALSE(raytrace_shape->get_raytrace().get_raytrace_geometry());
    EXPECT_FALSE(raytrace_shape->has_real_raytrace());

    // The bounding volume still covers the mesh vertices.
    const erhe::math::Aabb bounding_box = primitive.get_bounding_box();
    EXPECT_TRUE(bounding_box.is_valid());
}

} // namespace

TEST(EmptyGeometryBuild, no_vertices_no_facets)
{
    check_empty_primitive(make_processed_geometry(0));
}

TEST(EmptyGeometryBuild, lone_vertex_no_facets)
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_processed_geometry(1);
    check_empty_primitive(geometry);

    erhe::primitive::Primitive primitive{geometry};
    // Bounds of a lone vertex: a point box at that vertex, built without a
    // renderable build having allocated anything.
    const Vertex_format format = make_format();
    erhe::buffer::Cpu_buffer                vertex_buffer{"test_vertex", 1024};
    erhe::buffer::Cpu_buffer                index_buffer {"test_index",  1024};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_buffer_sink {index_buffer};
    const erhe::primitive::Build_info build_info{
        .primitive_types = { .fill_triangles = true },
        .buffer_info = {
            .normal_style       = erhe::primitive::Normal_style::corner_normals,
            .index_type         = Format::format_32_scalar_uint,
            .vertex_format      = format,
            .vertex_buffer_sink = vertex_buffer_sink,
            .index_buffer_sink  = index_buffer_sink
        },
        .normal_style = erhe::primitive::Normal_style::corner_normals
    };
    ASSERT_TRUE(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    const erhe::math::Aabb bounding_box = primitive.get_bounding_box();
    EXPECT_FLOAT_EQ(bounding_box.min.x, 0.25f);
    EXPECT_FLOAT_EQ(bounding_box.min.y, 0.5f);
    EXPECT_FLOAT_EQ(bounding_box.min.z, 0.75f);
    EXPECT_FLOAT_EQ(bounding_box.max.x, 0.25f);
    EXPECT_FLOAT_EQ(bounding_box.max.y, 0.5f);
    EXPECT_FLOAT_EQ(bounding_box.max.z, 0.75f);
}
