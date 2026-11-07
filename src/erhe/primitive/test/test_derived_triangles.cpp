#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <gtest/gtest.h>

#include <geogram/mesh/mesh.h>

#include <cstring>
#include <memory>
#include <optional>

// derive_triangle_soup() / prepare_derived_primitive_for_export()
// (doc/erhe/primitive.md "Derived triangles"): the triangles of a
// geometry-normative primitive with quad facets.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;

[[nodiscard]] auto make_box_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geometry->get_mesh(), 1.0f, 2.0f, 3.0f);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
                erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
        }
    );
    return geometry;
}

// A geometry-normative primitive built the way the editor builds brush meshes.
class Built_primitive
{
public:
    explicit Built_primitive(const erhe::primitive::Normal_style normal_style)
        : geometry {make_box_geometry()}
        , primitive{geometry}
    {
        const Vertex_format format{
            Vertex_stream{
                0,
                {
                    {Format::format_32_vec3_float, Vertex_attribute_usage::position,  0},
                    {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    0},
                    {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0}
                }
            }
        };
        const erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info = {
                .normal_style       = normal_style,
                .index_type         = Format::format_32_scalar_uint,
                .vertex_format      = format,
                .vertex_buffer_sink = vertex_sink,
                .index_buffer_sink  = index_sink
            },
            .normal_style = normal_style
        };
        built = primitive.make_renderable_mesh(build_info, normal_style);
    }

    erhe::buffer::Cpu_buffer                  vertex_buffer{"test_vertex", 64 * 1024};
    erhe::buffer::Cpu_buffer                  index_buffer {"test_index",  64 * 1024};
    erhe::primitive::Cpu_vertex_buffer_sink   vertex_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink    index_sink {index_buffer};
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    erhe::primitive::Primitive                primitive;
    bool                                      built{false};
};

[[nodiscard]] auto same_soup(const erhe::primitive::Triangle_soup& a, const erhe::primitive::Triangle_soup& b) -> bool
{
    return
        (a.primitive_type == b.primitive_type) &&
        (a.index_data     == b.index_data) &&
        (a.vertex_data    == b.vertex_data) &&
        (a.vertex_format.streams.size() == b.vertex_format.streams.size()) &&
        (a.vertex_format.streams.front().stride == b.vertex_format.streams.front().stride);
}

TEST(Derived_triangles, two_derivations_of_one_geometry_are_identical)
{
    Built_primitive a{erhe::primitive::Normal_style::polygon_normals};
    Built_primitive b{erhe::primitive::Normal_style::polygon_normals};
    ASSERT_TRUE(a.built && b.built);
    ASSERT_FALSE(a.primitive.render_shape->has_triangle_soup());

    const erhe::primitive::Derived_triangle_soup first  = erhe::primitive::derive_triangle_soup(*a.primitive.render_shape);
    const erhe::primitive::Derived_triangle_soup second = erhe::primitive::derive_triangle_soup(*a.primitive.render_shape);
    const erhe::primitive::Derived_triangle_soup other  = erhe::primitive::derive_triangle_soup(*b.primitive.render_shape);
    ASSERT_TRUE(first.soup)  << first.refusal;
    ASSERT_TRUE(second.soup) << second.refusal;
    ASSERT_TRUE(other.soup)  << other.refusal;
    EXPECT_TRUE(same_soup(*first.soup, *second.soup));
    EXPECT_TRUE(same_soup(*first.soup, *other.soup));

    // Six quads: twelve fill triangles, one vertex per corner, texcoords carried.
    EXPECT_EQ(first.soup->index_data.size(), 36u);
    EXPECT_EQ(first.soup->get_vertex_count(), 24u);
    EXPECT_NE(first.soup->vertex_format.streams.front().find_attribute(Vertex_attribute_usage::tex_coord, 0), nullptr);
    EXPECT_NE(first.soup->vertex_format.streams.front().find_attribute(Vertex_attribute_usage::color, 0), nullptr);

    // The anisotropy control the build writes: 1, 1 where the Geometry has none.
    const erhe::dataformat::Vertex_attribute* aniso = first.soup->vertex_format.streams.front().find_attribute(
        Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control
    );
    ASSERT_NE(aniso, nullptr);
    const std::size_t stride = first.soup->vertex_format.streams.front().stride;
    for (std::size_t vertex = 0, end = first.soup->get_vertex_count(); vertex < end; ++vertex) {
        float value[2];
        std::memcpy(value, first.soup->vertex_data.data() + (vertex * stride) + aniso->offset, sizeof(value));
        EXPECT_EQ(value[0], 1.0f);
        EXPECT_EQ(value[1], 1.0f);
    }
}

TEST(Derived_triangles, aniso_control_reaches_the_core_and_back)
{
    Built_primitive box{erhe::primitive::Normal_style::polygon_normals};
    ASSERT_TRUE(box.built);
    erhe::geometry::Mesh_attributes& attributes = box.geometry->get_attributes();
    for (GEO::index_t facet = 0, end = box.geometry->get_mesh().facets.nb(); facet < end; ++facet) {
        attributes.facet_aniso_control.set(facet, GEO::vec2f{0.25f * static_cast<float>(facet % 4), 0.5f});
    }
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_derived_primitive_for_export(*box.primitive.render_shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const erhe::primitive::Triangle_soup& core = result.prepared->core;
    ASSERT_NE(core.vertex_format.streams.front().find_attribute(Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control), nullptr);

    // The loader's conversion of the core puts the stream into corner_aniso_control.
    erhe::geometry::Geometry          loaded{"loaded"};
    erhe::primitive::Element_mappings mappings;
    ASSERT_TRUE(erhe::primitive::mesh_from_triangle_soup(core, result.prepared->vertex_points, result.prepared->point_count, loaded.get_mesh(), mappings));
    const GEO::Mesh& mesh = loaded.get_mesh();
    for (GEO::index_t corner = 0, end = mesh.facet_corners.nb(); corner < end; ++corner) {
        const std::optional<GEO::vec2f> value = loaded.get_attributes().corner_aniso_control.try_get(corner);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->y, 0.5f);
    }
}

TEST(Derived_triangles, normal_style_selects_the_derived_normals)
{
    Built_primitive polygon{erhe::primitive::Normal_style::polygon_normals};
    Built_primitive none   {erhe::primitive::Normal_style::none};
    ASSERT_TRUE(polygon.built && none.built);
    const erhe::primitive::Derived_triangle_soup flat   = erhe::primitive::derive_triangle_soup(*polygon.primitive.render_shape);
    const erhe::primitive::Derived_triangle_soup smooth = erhe::primitive::derive_triangle_soup(*none.primitive.render_shape);
    ASSERT_TRUE(flat.soup && smooth.soup);
    EXPECT_EQ(flat.soup->index_data, smooth.soup->index_data);
    EXPECT_NE(flat.soup->vertex_data, smooth.soup->vertex_data);
}

TEST(Derived_triangles, prepared_core_of_quads)
{
    Built_primitive box{erhe::primitive::Normal_style::polygon_normals};
    ASSERT_TRUE(box.built);
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_derived_primitive_for_export(*box.primitive.render_shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const erhe::primitive::Prepared_primitive_export& prepared = result.prepared.value();
    EXPECT_EQ(prepared.core.index_data.size(), 36u);
    // Flat normals keep the four corners of a face together and split the faces.
    EXPECT_EQ(prepared.core.get_vertex_count(), 24u);
    EXPECT_EQ(prepared.point_count, 8u);
    EXPECT_EQ(prepared.vertex_points.size(), prepared.core.get_vertex_count());
    EXPECT_TRUE(erhe::primitive::validate_prepared_core(
        prepared.core.index_data, erhe::primitive::get_float_positions(prepared.core), prepared.content_hash
    ).empty());
    EXPECT_TRUE(erhe::primitive::validate_vertex_points(
        prepared.vertex_points, prepared.point_count, erhe::primitive::get_float_positions(prepared.core)
    ).empty());

    // The loader's conversion of (core, vertex_points) accepts it: facet t is core triangle t.
    erhe::geometry::Geometry          loaded{"loaded"};
    erhe::primitive::Element_mappings mappings;
    ASSERT_TRUE(erhe::primitive::mesh_from_triangle_soup(prepared.core, prepared.vertex_points, prepared.point_count, loaded.get_mesh(), mappings));
    EXPECT_EQ(loaded.get_mesh().facets.nb(), 12u);
    EXPECT_EQ(loaded.get_mesh().vertices.nb(), 8u);
}

TEST(Derived_triangles, geometry_without_facets_is_refused)
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("empty");
    erhe::primitive::Primitive_render_shape   shape{geometry};
    const erhe::primitive::Derived_triangle_soup derived = erhe::primitive::derive_triangle_soup(shape);
    EXPECT_FALSE(derived.soup);
    EXPECT_EQ(derived.refusal, "no facets");
}

} // anonymous namespace
