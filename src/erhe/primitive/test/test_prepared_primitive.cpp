#include "mesh_compare.hpp"

#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"

#include <gtest/gtest.h>

#include <geogram/mesh/mesh.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

// prepare_primitive_for_export() (doc/plans/gltf_fast_load.md step 4): the
// prepared core is a valid ERHE_mesh_optimized core, its vertex_points are
// an ERHE_mesh_topology payload that rebuilds the same Geometry as the
// colocate path over the core, and its BVH is accepted for the core.

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Element_mappings;
using erhe::primitive::Prepared_primitive_export;
using erhe::primitive::Prepared_bvh_source;
using erhe::primitive::Primitive_render_shape;
using erhe::primitive::Triangle_soup;
using erhe::primitive::Vertex_colocation;
using erhe_primitive_test::compare_meshes;
using erhe_primitive_test::process_like_gltf_import;

enum class Soup_tangents : unsigned int { with_tangents, without_tangents };

[[nodiscard]] auto make_format(const Soup_tangents tangents) -> Vertex_format
{
    Vertex_stream stream{0};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0);
    if (tangents == Soup_tangents::with_tangents) {
        stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::tangent, 0);
    }
    stream.emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord,     0);
    stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color,         0);
    stream.emplace_back(Format::format_16_vec4_uint,  Vertex_attribute_usage::joint_indices, 0);
    stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, 0);
    stream.finalize_stride();
    Vertex_format format;
    format.streams.push_back(std::move(stream));
    return format;
}

class Soup_writer
{
public:
    explicit Soup_writer(Triangle_soup& soup)
        : m_soup{soup}
    {
    }

    // Texcoords are a function of the grid position so that copies of one
    // grid point weld; the tangent sign alternates per cell column.
    auto add_vertex(const float x, const float y, const float z) -> uint32_t
    {
        const Vertex_stream& stream = m_soup.vertex_format.streams.front();
        const std::size_t    vertex = m_soup.vertex_data.size() / stream.stride;
        m_soup.vertex_data.resize((vertex + 1) * stream.stride, uint8_t{0});
        uint8_t* const base = m_soup.vertex_data.data() + (vertex * stream.stride);
        const float    gx          = std::round(x);
        const float    gy          = std::round(y);
        const float    position[3] = {x, y, z};
        const float    normal  [3] = {0.0f, 0.0f, 1.0f};
        const float    tangent [4] = {1.0f, 0.0f, 0.0f, 1.0f};
        const float    texcoord[2] = {0.25f * gx, 0.5f * gy};
        const float    color   [4] = {0.125f * gx, 0.5f, 0.25f * gy, 1.0f};
        const uint16_t joints  [4] = {static_cast<uint16_t>(gx), 1u, 2u, 3u};
        // Not normalized: the core carries the normalized weights.
        const float    weights [4] = {0.5f, 0.25f, 0.125f, 0.0f};
        write(base, Vertex_attribute_usage::position,      position, sizeof(position));
        write(base, Vertex_attribute_usage::normal,        normal,   sizeof(normal));
        write(base, Vertex_attribute_usage::tangent,       tangent,  sizeof(tangent));
        write(base, Vertex_attribute_usage::tex_coord,     texcoord, sizeof(texcoord));
        write(base, Vertex_attribute_usage::color,         color,    sizeof(color));
        write(base, Vertex_attribute_usage::joint_indices, joints,   sizeof(joints));
        write(base, Vertex_attribute_usage::joint_weights, weights,  sizeof(weights));
        return static_cast<uint32_t>(vertex);
    }

    void add_triangle(const uint32_t a, const uint32_t b, const uint32_t c)
    {
        m_soup.index_data.push_back(a);
        m_soup.index_data.push_back(b);
        m_soup.index_data.push_back(c);
    }

private:
    void write(uint8_t* const base, const Vertex_attribute_usage usage, const void* const data, const std::size_t size)
    {
        const Vertex_attribute* const attribute = m_soup.vertex_format.streams.front().find_attribute(usage, 0);
        if (attribute == nullptr) {
            return; // e.g. the soup without tangents
        }
        std::memcpy(base + attribute->offset, data, size);
    }

    Triangle_soup& m_soup;
};

constexpr int grid_x = 4;
constexpr int grid_y = 3;
constexpr uint32_t expected_point_count = (grid_x + 1) * (grid_y + 1);
constexpr uint32_t expected_triangle_count = 2 * grid_x * grid_y;

// A 4 x 3 grid of quads, two triangles each. Some triangles share vertices
// with the previous one, the others have their own copies, some copies are
// nudged by 3e-6 (inside the 1e-5 colocation tolerance), and one triangle
// collapses onto two points.
[[nodiscard]] auto make_soup(const Soup_tangents tangents) -> std::shared_ptr<Triangle_soup>
{
    std::shared_ptr<Triangle_soup> soup = std::make_shared<Triangle_soup>();
    soup->vertex_format = make_format(tangents);
    Soup_writer writer{*soup};
    uint32_t copy = 0;
    const auto corner = [&](const int x, const int y) -> uint32_t {
        const float nudge = ((copy++ % 3u) == 0u) ? 3.0e-6f : 0.0f;
        return writer.add_vertex(static_cast<float>(x) + nudge, static_cast<float>(y), 0.0f - nudge);
    };
    for (int y = 0; y < grid_y; ++y) {
        for (int x = 0; x < grid_x; ++x) {
            const uint32_t a = corner(x,     y);
            const uint32_t b = corner(x + 1, y);
            const uint32_t c = corner(x + 1, y + 1);
            writer.add_triangle(a, b, c);
            if (((x + y) % 2) == 0) {
                const uint32_t d = corner(x, y + 1);
                writer.add_triangle(a, c, d);
            } else {
                const uint32_t a2 = corner(x,     y);
                const uint32_t c2 = corner(x + 1, y + 1);
                const uint32_t d  = corner(x,     y + 1);
                writer.add_triangle(a2, c2, d);
            }
        }
    }
    {
        const uint32_t a = corner(0, 0);
        const uint32_t b = corner(0, 0);
        const uint32_t c = corner(1, 0);
        writer.add_triangle(a, b, c);
    }
    return soup;
}

[[nodiscard]] auto attribute_bytes(const Triangle_soup& soup, const Vertex_attribute_usage usage, const std::size_t vertex) -> const uint8_t*
{
    const Vertex_stream&    stream    = soup.vertex_format.streams.front();
    const Vertex_attribute* attribute = stream.find_attribute(usage, 0);
    EXPECT_NE(attribute, nullptr);
    return soup.vertex_data.data() + (vertex * stream.stride) + attribute->offset;
}

[[nodiscard]] auto read_vec4(const Triangle_soup& soup, const Vertex_attribute_usage usage, const std::size_t vertex) -> GEO::vec4f
{
    GEO::vec4f value{0.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(&value.x, attribute_bytes(soup, usage, vertex), 4 * sizeof(float));
    return value;
}

// Byte-by-byte FNV-1a over the spec's byte sequence, independent of
// compute_mesh_content_hash().
[[nodiscard]] auto reference_content_hash(const Prepared_primitive_export& prepared) -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    const auto add_byte = [&hash](const uint8_t byte) {
        hash = (hash ^ byte) * 0x100000001b3ull;
    };
    for (const uint32_t index : prepared.core.index_data) {
        for (int shift = 0; shift < 32; shift += 8) {
            add_byte(static_cast<uint8_t>((index >> shift) & 0xffu));
        }
    }
    for (std::size_t vertex = 0, end = prepared.core.get_vertex_count(); vertex < end; ++vertex) {
        const uint8_t* const position = attribute_bytes(prepared.core, Vertex_attribute_usage::position, vertex);
        for (std::size_t i = 0; i < 3; ++i) {
            uint32_t bits = 0;
            std::memcpy(&bits, position + (i * sizeof(float)), sizeof(float));
            for (int shift = 0; shift < 32; shift += 8) {
                add_byte(static_cast<uint8_t>((bits >> shift) & 0xffu));
            }
        }
    }
    return hash;
}

void check_core_contract(const Prepared_primitive_export& prepared)
{
    const Triangle_soup& core         = prepared.core;
    const std::size_t    vertex_count = core.get_vertex_count();
    const std::size_t    stride       = core.vertex_format.streams.front().stride;
    ASSERT_GT(vertex_count, 0u);
    ASSERT_EQ(core.index_data.size() % 3, 0u);
    EXPECT_EQ(core.index_data.size() / 3, expected_triangle_count);
    ASSERT_EQ(prepared.vertex_points.size(), vertex_count);

    // Every index in range, every vertex referenced.
    std::vector<bool> referenced(vertex_count, false);
    for (const uint32_t index : core.index_data) {
        ASSERT_LT(index, vertex_count);
        referenced[index] = true;
    }
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        EXPECT_TRUE(referenced[vertex]) << "vertex " << vertex << " unreferenced";
    }

    // Bitwise weld: no two vertices identical.
    std::set<std::string> distinct;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        distinct.emplace(reinterpret_cast<const char*>(core.vertex_data.data() + (vertex * stride)), stride);
    }
    EXPECT_EQ(distinct.size(), vertex_count);

    // No degenerate triangles.
    for (std::size_t triangle = 0, end = core.index_data.size() / 3; triangle < end; ++triangle) {
        const uint8_t* p[3];
        for (std::size_t i = 0; i < 3; ++i) {
            p[i] = attribute_bytes(core, Vertex_attribute_usage::position, core.index_data[(3 * triangle) + i]);
        }
        EXPECT_NE(std::memcmp(p[0], p[1], 12), 0) << "triangle " << triangle;
        EXPECT_NE(std::memcmp(p[0], p[2], 12), 0) << "triangle " << triangle;
        EXPECT_NE(std::memcmp(p[1], p[2], 12), 0) << "triangle " << triangle;
    }

    // vertex_points: first-reference order, every point referenced, and one
    // position per point.
    EXPECT_EQ(prepared.point_count, expected_point_count);
    uint32_t next_point = 0;
    std::vector<std::size_t> first_vertex_of_point;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const uint32_t point = prepared.vertex_points[vertex];
        ASSERT_LE(point, next_point) << "vertex " << vertex;
        if (point == next_point) {
            first_vertex_of_point.push_back(vertex);
            ++next_point;
        }
        const uint8_t* const a = attribute_bytes(core, Vertex_attribute_usage::position, vertex);
        const uint8_t* const b = attribute_bytes(core, Vertex_attribute_usage::position, first_vertex_of_point[point]);
        EXPECT_EQ(std::memcmp(a, b, 12), 0) << "vertex " << vertex << " position differs from its point";
    }
    EXPECT_EQ(next_point, prepared.point_count);

    // Joint weights normalized; tangents present and unit-sign.
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const GEO::vec4f weights = read_vec4(core, Vertex_attribute_usage::joint_weights, vertex);
        const GEO::vec4f expected = erhe::primitive::normalize_joint_weights(GEO::vec4f{0.5f, 0.25f, 0.125f, 0.0f});
        EXPECT_EQ(std::memcmp(&weights.x, &expected.x, sizeof(GEO::vec4f)), 0) << "vertex " << vertex;
        const GEO::vec4f tangent = read_vec4(core, Vertex_attribute_usage::tangent, vertex);
        EXPECT_TRUE((tangent.w == 1.0f) || (tangent.w == -1.0f)) << "vertex " << vertex;
        EXPECT_GT((tangent.x * tangent.x) + (tangent.y * tangent.y) + (tangent.z * tangent.z), 0.5f) << "vertex " << vertex;
    }

    EXPECT_EQ(prepared.content_hash, reference_content_hash(prepared));
    const std::vector<float> positions = erhe::primitive::get_float_positions(core);
    EXPECT_EQ(prepared.content_hash, erhe::primitive::compute_mesh_content_hash(core.index_data, positions));
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const glm::vec3 p{positions[3 * vertex], positions[(3 * vertex) + 1], positions[(3 * vertex) + 2]};
        EXPECT_LE(glm::length(p - prepared.bounding_sphere.center), prepared.bounding_sphere.radius * 1.0001f);
    }
}

// The Geometry a loader builds from (core, vertex_points) equals the one the
// colocate path builds over the core, bit for bit, before and after process.
void check_topology_reproduces_colocate_path(const Prepared_primitive_export& prepared)
{
    erhe::geometry::Geometry colocated{"colocated"};
    Element_mappings         colocated_mappings;
    Vertex_colocation        colocation;
    erhe::primitive::mesh_from_triangle_soup(prepared.core, colocated.get_mesh(), colocated_mappings, colocation);
    EXPECT_EQ(colocation.vertex_points, prepared.vertex_points);
    EXPECT_EQ(colocation.point_count,   prepared.point_count);

    erhe::geometry::Geometry stored{"stored"};
    Element_mappings         stored_mappings;
    ASSERT_TRUE(
        erhe::primitive::mesh_from_triangle_soup(
            prepared.core, prepared.vertex_points, prepared.point_count, stored.get_mesh(), stored_mappings
        )
    );
    // Facet t is core triangle t.
    ASSERT_EQ(stored_mappings.triangle_to_mesh_facet.size(), prepared.core.index_data.size() / 3);
    for (std::size_t t = 0; t < stored_mappings.triangle_to_mesh_facet.size(); ++t) {
        EXPECT_EQ(stored_mappings.triangle_to_mesh_facet[t], t);
    }
    EXPECT_EQ(colocated_mappings.triangle_to_mesh_facet,             stored_mappings.triangle_to_mesh_facet);
    EXPECT_EQ(colocated_mappings.mesh_corner_to_vertex_buffer_index, stored_mappings.mesh_corner_to_vertex_buffer_index);
    {
        SCOPED_TRACE("after conversion");
        compare_meshes(colocated.get_mesh(), stored.get_mesh());
    }
    process_like_gltf_import(colocated);
    process_like_gltf_import(stored);
    {
        SCOPED_TRACE("after process");
        compare_meshes(colocated.get_mesh(), stored.get_mesh());
    }
}

// The BVH is accepted for the core by validation and by a commit.
void check_bvh_accepted(const Prepared_primitive_export& prepared)
{
    if (erhe::raytrace::get_prebuilt_bvh_backend().empty()) {
        EXPECT_EQ(prepared.bvh_source, Prepared_bvh_source::none);
        return;
    }
    ASSERT_TRUE(prepared.bvh.has_value());
    const std::vector<float> positions     = erhe::primitive::get_float_positions(prepared.core);
    const uint64_t           triangle_hash = erhe::raytrace::compute_triangle_hash(prepared.core.index_data, positions);
    const std::size_t        triangle_count = prepared.core.index_data.size() / 3;
    EXPECT_EQ(prepared.bvh->triangle_hash,  triangle_hash);
    EXPECT_EQ(prepared.bvh->triangle_count, triangle_count);
    EXPECT_EQ(
        erhe::raytrace::validate_prebuilt_bvh(
            prepared.bvh.value(),
            erhe::raytrace::get_prebuilt_bvh_backend(),
            erhe::raytrace::get_prebuilt_bvh_library(),
            triangle_hash,
            triangle_count,
            erhe::raytrace::bvh_v2_traversal_stack_size
        ),
        std::string{}
    );

    erhe::buffer::Cpu_buffer vertex_buffer{"test_vertex", positions.size() * sizeof(float), 16};
    erhe::buffer::Cpu_buffer index_buffer {"test_index",  prepared.core.index_data.size() * sizeof(uint32_t), 16};
    std::memcpy(vertex_buffer.get_span().data(), positions.data(), positions.size() * sizeof(float));
    std::memcpy(index_buffer .get_span().data(), prepared.core.index_data.data(), prepared.core.index_data.size() * sizeof(uint32_t));
    std::unique_ptr<erhe::raytrace::IGeometry> geometry = erhe::raytrace::IGeometry::create_unique(
        "test", erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    geometry->set_buffer(erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX, 0, Format::format_32_vec3_float, &vertex_buffer, 0, 12, positions.size() / 3);
    geometry->set_buffer(erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,  0, Format::format_32_vec3_uint,  &index_buffer,  0, 12, triangle_count);
    erhe::raytrace::Prebuilt_bvh copy = prepared.bvh.value();
    ASSERT_TRUE(geometry->set_prebuilt_bvh(std::move(copy)));
    geometry->commit();
    EXPECT_EQ(geometry->get_bvh_source(), erhe::raytrace::Bvh_source::prebuilt) << geometry->get_prebuilt_bvh_rejection();
}

TEST(Prepared_primitive, committed_raytrace_bvh_is_reused)
{
    Primitive_render_shape shape{make_soup(Soup_tangents::with_tangents)};
    ASSERT_TRUE(shape.make_raytrace());
    ASSERT_TRUE(shape.has_real_raytrace());

    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const Prepared_primitive_export& prepared = result.prepared.value();
    EXPECT_EQ(prepared.dropped_degenerate_triangle_count, 0u);
    check_core_contract(prepared);
    check_topology_reproduces_colocate_path(prepared);
    if (!erhe::raytrace::get_prebuilt_bvh_backend().empty()) {
        EXPECT_EQ(prepared.bvh_source, Prepared_bvh_source::reused);
    }
    check_bvh_accepted(prepared);
}

TEST(Prepared_primitive, missing_tangents_are_computed_and_bvh_is_built)
{
    // No raytrace on the shape: the BVH is built over the core.
    Primitive_render_shape shape{make_soup(Soup_tangents::without_tangents)};
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const Prepared_primitive_export& prepared = result.prepared.value();
    ASSERT_NE(prepared.core.vertex_format.streams.front().find_attribute(Vertex_attribute_usage::tangent, 0), nullptr);
    check_core_contract(prepared);
    check_topology_reproduces_colocate_path(prepared);
    if (!erhe::raytrace::get_prebuilt_bvh_backend().empty()) {
        EXPECT_EQ(prepared.bvh_source, Prepared_bvh_source::rebuilt);
    }
    check_bvh_accepted(prepared);
}

// The core values are the content build's: building the shape's Geometry
// into the core's format gives the same per-corner bytes as the core holds
// for that corner's vertex.
TEST(Prepared_primitive, core_matches_content_build)
{
    Primitive_render_shape shape{make_soup(Soup_tangents::without_tangents)};
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(shape);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const Prepared_primitive_export& prepared = result.prepared.value();
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape.get_geometry();
    ASSERT_TRUE(geometry);
    const GEO::Mesh&  mesh   = geometry->get_mesh();
    const std::size_t stride = prepared.core.vertex_format.streams.front().stride;

    erhe::buffer::Cpu_buffer vertex_buffer{"test_vertex", mesh.facet_corners.nb() * stride};
    erhe::buffer::Cpu_buffer index_buffer {"test_index",  mesh.facet_corners.nb() * sizeof(uint32_t)};
    erhe::primitive::Cpu_vertex_buffer_sink vertex_sink{{&vertex_buffer}};
    erhe::primitive::Cpu_index_buffer_sink  index_sink {index_buffer};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {.fill_triangles = true},
        .buffer_info = {
            .normal_style       = erhe::primitive::Normal_style::corner_normals,
            .index_type         = Format::format_32_scalar_uint,
            .vertex_format      = prepared.core.vertex_format,
            .vertex_buffer_sink = vertex_sink,
            .index_buffer_sink  = index_sink
        }
    };
    Element_mappings mappings;
    {
        erhe::primitive::Buffer_mesh buffer_mesh;
        ASSERT_TRUE(erhe::primitive::build_buffer_mesh(buffer_mesh, mesh, build_info, mappings, erhe::primitive::Normal_style::corner_normals));
    }
    // Every built corner vertex has an identical vertex in the core.
    std::set<std::string> core_vertices;
    for (std::size_t vertex = 0, end = prepared.core.get_vertex_count(); vertex < end; ++vertex) {
        core_vertices.emplace(reinterpret_cast<const char*>(prepared.core.vertex_data.data() + (vertex * stride)), stride);
    }
    const std::byte* const built = vertex_buffer.get_span().data();
    for (std::size_t vertex = 0, end = mesh.facet_corners.nb(); vertex < end; ++vertex) {
        const std::string bytes{reinterpret_cast<const char*>(built + (vertex * stride)), stride};
        EXPECT_TRUE(core_vertices.contains(bytes)) << "built vertex " << vertex << " not in the core";
    }
}

TEST(Prepared_primitive, attribute_set_gap_is_refused)
{
    std::shared_ptr<Triangle_soup> soup = std::make_shared<Triangle_soup>();
    Vertex_stream stream{0};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position,  0);
    stream.emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1);
    stream.finalize_stride();
    soup->vertex_format.streams.push_back(std::move(stream));
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::size_t vertex_stride = soup->vertex_format.streams.front().stride;
    soup->vertex_data.resize(3 * vertex_stride, uint8_t{0});
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        std::memcpy(soup->vertex_data.data() + (vertex * vertex_stride), &positions[3 * vertex], 12);
    }
    soup->index_data = {0, 1, 2};
    Primitive_render_shape shape{soup};
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(shape);
    EXPECT_FALSE(result.prepared.has_value());
    EXPECT_FALSE(result.skip_reason.empty());
}

// Load-side checks of ERHE_mesh_optimized and ERHE_mesh_topology
// (doc/gltf_extensions/ERHE_mesh_optimized.md, ERHE_mesh_topology.md
// "Validation / fallback").
TEST(Prepared_primitive, load_validation_of_core_and_vertex_points)
{
    // A quad: vertices 0..3, two triangles; vertex 4 duplicates vertex 0's
    // position (a seam vertex of point 0).
    const std::vector<float> positions{
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    const std::vector<uint32_t> indices{0, 1, 2, 4, 2, 3};
    const uint64_t hash = erhe::primitive::compute_mesh_content_hash(indices, positions);
    EXPECT_EQ(erhe::primitive::validate_prepared_core(indices, positions, hash), "");
    EXPECT_EQ(erhe::primitive::validate_prepared_core(indices, positions, hash + 1), "content_hash mismatch");
    EXPECT_EQ(erhe::primitive::validate_prepared_core(std::vector<uint32_t>{0, 1, 2, 4, 2}, positions, hash), "index count is not a positive multiple of 3");
    EXPECT_EQ(erhe::primitive::validate_prepared_core(std::vector<uint32_t>{0, 1, 2, 4, 2, 5}, positions, hash), "index out of range");
    EXPECT_EQ(erhe::primitive::validate_prepared_core(std::vector<uint32_t>{0, 1, 2, 0, 2, 3}, positions, hash), "unreferenced vertex");
    EXPECT_EQ(erhe::primitive::validate_prepared_core(std::vector<uint32_t>{0, 1, 2, 4, 0, 3}, positions, hash), "degenerate triangle");

    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3, 0}, 4, positions), "");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{1, 0, 2, 3, 1}, 4, positions), "not in first-reference order");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3, 1}, 4, positions), "point vertices differ in position");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3, 4}, 4, positions), "point id out of range");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3, 0}, 5, positions), "point_count mismatch");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3}, 4, positions), "vertex_points size mismatch");
    EXPECT_EQ(erhe::primitive::validate_vertex_points(std::vector<uint32_t>{0, 1, 2, 3, 0}, 0, positions), "point_count out of range");
}


// Load path of a prepared primitive (doc/plans/gltf_fast_load.md step 6):
// base soup build, optimized variant packed from the core, and the raytrace
// from the stored BVH with an identity facet map - and no Geometry. A stored
// BVH that does not match the core installs no raytrace.
TEST(Prepared_primitive, prepared_meshes_build_without_geometry)
{
    Primitive_render_shape source{make_soup(Soup_tangents::with_tangents)};
    ASSERT_TRUE(source.make_raytrace());
    const erhe::primitive::Prepare_primitive_result result = erhe::primitive::prepare_primitive_for_export(source);
    ASSERT_TRUE(result.prepared.has_value()) << result.skip_reason;
    const Prepared_primitive_export& prepared = result.prepared.value();
    const bool has_bvh_backend = !erhe::raytrace::get_prebuilt_bvh_backend().empty();

    const Vertex_format& format       = prepared.core.vertex_format;
    const std::size_t    stride       = format.streams.front().stride;
    const std::size_t    vertex_count = prepared.core.get_vertex_count();
    const std::size_t    index_count  = prepared.core.get_index_count();

    const auto make_data = [&prepared](const uint64_t hash_delta) {
        std::shared_ptr<erhe::primitive::Prepared_primitive_data> data = std::make_shared<erhe::primitive::Prepared_primitive_data>();
        data->content_hash    = prepared.content_hash;
        data->bounding_box    = prepared.bounding_box;
        data->bounding_sphere = prepared.bounding_sphere;
        data->colocation      = Vertex_colocation{.vertex_points = prepared.vertex_points, .point_count = prepared.point_count};
        if (prepared.bvh.has_value()) {
            data->bvh = prepared.bvh;
            data->bvh->triangle_hash += hash_delta;
        }
        return std::shared_ptr<const erhe::primitive::Prepared_primitive_data>{data};
    };

    for (const uint64_t hash_delta : {uint64_t{0}, uint64_t{1}}) {
        erhe::buffer::Cpu_buffer vertex_buffer{"test_vertex", 2 * vertex_count * stride};
        erhe::buffer::Cpu_buffer index_buffer {"test_index",  2 * index_count * sizeof(uint32_t)};
        erhe::primitive::Cpu_vertex_buffer_sink vertex_sink{{&vertex_buffer}};
        erhe::primitive::Cpu_index_buffer_sink  index_sink {index_buffer};
        const erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info = {
                .normal_style            = erhe::primitive::Normal_style::corner_normals,
                .index_type              = Format::format_32_scalar_uint,
                .vertex_format           = format,
                .vertex_buffer_sink      = vertex_sink,
                .index_buffer_sink       = index_sink,
                .optimize_meshes         = true,
                .optimized_vertex_format = &format
            }
        };

        erhe::primitive::Primitive primitive{std::make_shared<Triangle_soup>(prepared.core), make_data(hash_delta)};
        ASSERT_TRUE(primitive.is_prepared());
        ASSERT_TRUE(primitive.make_prepared_meshes(build_info));
        EXPECT_TRUE(primitive.has_renderable_triangles());
        ASSERT_TRUE(primitive.optimized_render_shape);
        EXPECT_EQ(primitive.optimized_render_shape->get_renderable_mesh().index_range(erhe::primitive::Primitive_mode::polygon_fill).index_count, index_count);
        EXPECT_FALSE(primitive.render_shape->get_geometry_const());
        if (!has_bvh_backend) {
            EXPECT_FALSE(primitive.has_real_raytrace());
            continue;
        }
        if (hash_delta != 0) {
            // The backend refuses the stored BVH: nothing is installed and the
            // caller takes the proxy + deferred raytrace path.
            EXPECT_FALSE(primitive.has_raytrace_triangles());
            continue;
        }
        ASSERT_TRUE(primitive.has_real_raytrace());
        const erhe::primitive::Primitive_raytrace& raytrace = primitive.render_shape->get_raytrace();
        EXPECT_EQ(raytrace.get_bvh_source(), static_cast<unsigned int>(erhe::raytrace::Bvh_source::prebuilt));
        for (uint32_t triangle = 0; triangle < (index_count / 3); ++triangle) {
            EXPECT_EQ(raytrace.get_mesh_facet_from_triangle(triangle), triangle);
        }
    }
}

} // namespace
