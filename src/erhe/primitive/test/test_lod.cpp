#include "erhe_primitive/lod.hpp"

#include "erhe_primitive/edge_line_vertex.hpp"

#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/sphere.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <span>
#include <vector>

// LOD chain generation (generate_lod_chain()) and the chain storage on the
// optimized Buffer_mesh (both build paths). See
// doc/erhe/meshoptimizer_integration.md "Level of detail".

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Lod_chain_indices;
using erhe::primitive::Lod_settings;
using erhe::primitive::Lod_vertex_data;

constexpr float pi = 3.14159265358979323846f;

class Test_mesh
{
public:
    Lod_vertex_data       vertices;
    std::vector<uint32_t> indices;
};

void add_vertex(Test_mesh& mesh, const glm::vec3 position, const glm::vec3 normal, const glm::vec2 texcoord)
{
    mesh.vertices.positions.insert(mesh.vertices.positions.end(), {position.x, position.y, position.z});
    mesh.vertices.normals  .insert(mesh.vertices.normals  .end(), {normal.x, normal.y, normal.z});
    mesh.vertices.seam_attributes.insert(mesh.vertices.seam_attributes.end(), {texcoord.x, texcoord.y});
    mesh.vertices.seam_attribute_stride = 2;
    ++mesh.vertices.vertex_count;
}

// UV sphere with a texture seam: the u = 0 and u = 1 columns are separate
// vertices at the same positions. Vertex (stack, slice) is
// stack * (slice_count + 1) + slice.
[[nodiscard]] auto make_uv_sphere(const uint32_t slice_count, const uint32_t stack_count) -> Test_mesh
{
    Test_mesh mesh;
    for (uint32_t stack = 0; stack <= stack_count; ++stack) {
        const float v     = static_cast<float>(stack) / static_cast<float>(stack_count);
        const float theta = v * pi;
        for (uint32_t slice = 0; slice <= slice_count; ++slice) {
            const float u   = static_cast<float>(slice) / static_cast<float>(slice_count);
            // The seam column repeats slice 0 bit for bit, so the two share
            // their position exactly.
            const float phi = ((slice == slice_count) ? 0.0f : u) * 2.0f * pi;
            const glm::vec3 p{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            add_vertex(mesh, p, p, glm::vec2{u, v});
        }
    }
    const uint32_t row = slice_count + 1;
    for (uint32_t stack = 0; stack < stack_count; ++stack) {
        for (uint32_t slice = 0; slice < slice_count; ++slice) {
            const uint32_t a = (stack * row) + slice;
            const uint32_t b = a + 1;
            const uint32_t c = a + row;
            const uint32_t d = c + 1;
            if (stack != 0) {
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            }
            if (stack != (stack_count - 1)) {
                mesh.indices.insert(mesh.indices.end(), {b, d, c});
            }
        }
    }
    return mesh;
}

// Box of six separately textured faces: each face is a grid of its own
// vertices with its own atlas cell of texcoords (a texture seam along every
// box edge), normals are smooth (normalized position), so only the seams keep
// the faces apart. Face f owns vertices [f * (n + 1)^2, (f + 1) * (n + 1)^2).
[[nodiscard]] auto make_textured_box(const uint32_t n) -> Test_mesh
{
    Test_mesh mesh;
    const std::array<glm::vec3, 6> normals{
        glm::vec3{ 1.0f, 0.0f, 0.0f}, glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{ 0.0f, 1.0f, 0.0f}, glm::vec3{ 0.0f,-1.0f, 0.0f},
        glm::vec3{ 0.0f, 0.0f, 1.0f}, glm::vec3{ 0.0f, 0.0f,-1.0f}
    };
    for (std::size_t face = 0; face < normals.size(); ++face) {
        const glm::vec3 face_normal = normals[face];
        const float     atlas_u     = 2.0f * static_cast<float>(face);
        const glm::vec3 axis_u = (std::abs(face_normal.x) > 0.5f) ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
        const glm::vec3 axis_v = glm::cross(face_normal, axis_u);
        const uint32_t  first  = static_cast<uint32_t>(mesh.vertices.vertex_count);
        for (uint32_t j = 0; j <= n; ++j) {
            for (uint32_t i = 0; i <= n; ++i) {
                const float     s = (2.0f * static_cast<float>(i) / static_cast<float>(n)) - 1.0f;
                const float     t = (2.0f * static_cast<float>(j) / static_cast<float>(n)) - 1.0f;
                const glm::vec3 p = face_normal + (s * axis_u) + (t * axis_v);
                add_vertex(mesh, p, glm::normalize(p), glm::vec2{atlas_u + (0.5f * (s + 1.0f)), 0.5f * (t + 1.0f)});
            }
        }
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t a = first + (j * (n + 1)) + i;
                const uint32_t b = a + 1;
                const uint32_t c = a + n + 1;
                const uint32_t d = c + 1;
                // Wind so the triangle normal points out along face_normal.
                const glm::vec3 pa{mesh.vertices.positions[3 * a], mesh.vertices.positions[(3 * a) + 1], mesh.vertices.positions[(3 * a) + 2]};
                const glm::vec3 pb{mesh.vertices.positions[3 * b], mesh.vertices.positions[(3 * b) + 1], mesh.vertices.positions[(3 * b) + 2]};
                const glm::vec3 pc{mesh.vertices.positions[3 * c], mesh.vertices.positions[(3 * c) + 1], mesh.vertices.positions[(3 * c) + 2]};
                if (glm::dot(glm::cross(pb - pa, pc - pa), face_normal) > 0.0f) {
                    mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
                } else {
                    mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
                }
            }
        }
    }
    return mesh;
}

// Welded box surface (one vertex per position) of n x n quads per face, built
// as a deformed grid on each face with shared edge vertices: positions are
// looked up in a map so faces share their border vertices.
[[nodiscard]] auto make_welded_box(const uint32_t n) -> Test_mesh
{
    Test_mesh mesh;
    std::vector<std::array<int, 3>> keys;
    const auto get_vertex = [&](const std::array<int, 3>& key) -> uint32_t {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) {
                return static_cast<uint32_t>(i);
            }
        }
        keys.push_back(key);
        const glm::vec3 p{
            (2.0f * static_cast<float>(key[0]) / static_cast<float>(n)) - 1.0f,
            (2.0f * static_cast<float>(key[1]) / static_cast<float>(n)) - 1.0f,
            (2.0f * static_cast<float>(key[2]) / static_cast<float>(n)) - 1.0f
        };
        add_vertex(mesh, p, glm::normalize(p), glm::vec2{0.0f, 0.0f});
        return static_cast<uint32_t>(keys.size() - 1);
    };
    const int ni = static_cast<int>(n);
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const int u_axis = (axis + 1) % 3;
            const int v_axis = (axis + 2) % 3;
            for (int j = 0; j < ni; ++j) {
                for (int i = 0; i < ni; ++i) {
                    std::array<uint32_t, 4> quad{};
                    const int offsets[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    for (std::size_t k = 0; k < 4; ++k) {
                        std::array<int, 3> key{};
                        key[static_cast<std::size_t>(axis)]   = side * ni;
                        key[static_cast<std::size_t>(u_axis)] = i + offsets[k][0];
                        key[static_cast<std::size_t>(v_axis)] = j + offsets[k][1];
                        quad[k] = get_vertex(key);
                    }
                    if (side == 1) {
                        mesh.indices.insert(mesh.indices.end(), {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]});
                    } else {
                        mesh.indices.insert(mesh.indices.end(), {quad[0], quad[2], quad[1], quad[0], quad[3], quad[2]});
                    }
                }
            }
        }
    }
    return mesh;
}

// The chain invariants every test checks: levels within bounds, triangle
// counts strictly decreasing from level 0, errors strictly increasing from 0,
// and every index naming an input vertex.
void expect_valid_chain(const Lod_chain_indices& chain, const std::size_t level_0_index_count, const std::size_t vertex_count)
{
    EXPECT_LT(chain.levels.size(), erhe::primitive::max_lod_levels);
    std::size_t previous_index_count = level_0_index_count;
    float       previous_error       = 0.0f;
    std::size_t expected_first_index = 0;
    for (const Lod_chain_indices::Level& level : chain.levels) {
        EXPECT_EQ(level.first_index, expected_first_index);
        EXPECT_EQ(level.index_count % 3, 0u);
        EXPECT_LT(level.index_count, previous_index_count);
        EXPECT_GT(level.error, previous_error);
        previous_index_count  = level.index_count;
        previous_error        = level.error;
        expected_first_index += level.index_count;
    }
    EXPECT_EQ(chain.indices.size(), expected_first_index);
    for (const uint32_t index : chain.indices) {
        ASSERT_LT(index, vertex_count);
    }
}

[[nodiscard]] auto get_texcoord(const Test_mesh& mesh, const uint32_t vertex) -> glm::vec2
{
    return glm::vec2{mesh.vertices.seam_attributes[2 * vertex], mesh.vertices.seam_attributes[(2 * vertex) + 1]};
}

} // namespace

TEST(Lod, sphere_chain_is_monotonic)
{
    const Test_mesh         mesh  = make_uv_sphere(48, 24);
    const Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_GE(chain.levels.size(), 3u);
    expect_valid_chain(chain, mesh.indices.size(), mesh.vertices.vertex_count);
    // Default ratio 0.5: the first level lands near half of level 0.
    EXPECT_LE(chain.levels.front().index_count, (3 * mesh.indices.size()) / 4);
}

TEST(Lod, sphere_seam_keeps_texcoords)
{
    // Level 1 and later reference the real vertices; along the seam each side
    // keeps its own column of vertices. A triangle with a u = 0 seam vertex
    // lies on the u < 0.5 side, one with a u = 1 seam vertex on the u > 0.5
    // side; a triangle that took the seam vertex of the other side would
    // span the whole u range.
    const Test_mesh         mesh  = make_uv_sphere(48, 24);
    const Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_FALSE(chain.levels.empty());
    for (const Lod_chain_indices::Level& level : chain.levels) {
        for (std::size_t triangle = 0; triangle < (level.index_count / 3); ++triangle) {
            bool  has_u_0 = false;
            bool  has_u_1 = false;
            float u_min   = 1.0f;
            float u_max   = 0.0f;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const glm::vec2 uv = get_texcoord(mesh, chain.indices[level.first_index + (3 * triangle) + corner]);
                u_min = std::min(u_min, uv.x);
                u_max = std::max(u_max, uv.x);
                if ((uv.y > 0.0f) && (uv.y < 1.0f)) { // pole vertices are not on the seam
                    has_u_0 = has_u_0 || (uv.x == 0.0f);
                    has_u_1 = has_u_1 || (uv.x == 1.0f);
                }
            }
            if (has_u_0) {
                EXPECT_LT(u_max, 0.5f);
            }
            if (has_u_1) {
                EXPECT_GT(u_min, 0.5f);
            }
        }
    }
    // Level 1 still draws both seam columns.
    const Lod_chain_indices::Level& level_1 = chain.levels.front();
    bool has_u_0 = false;
    bool has_u_1 = false;
    for (std::size_t i = 0; i < level_1.index_count; ++i) {
        const glm::vec2 uv = get_texcoord(mesh, chain.indices[level_1.first_index + i]);
        const float     v  = uv.y;
        if ((v > 0.0f) && (v < 1.0f)) {
            has_u_0 = has_u_0 || (uv.x == 0.0f);
            has_u_1 = has_u_1 || (uv.x == 1.0f);
        }
    }
    EXPECT_TRUE(has_u_0);
    EXPECT_TRUE(has_u_1);
}

TEST(Lod, textured_box_seams_keep_faces_apart)
{
    // Every triangle of every level stays within one face: its corners are
    // vertices of that face, carrying that face's texcoords.
    const uint32_t          n          = 8;
    const uint32_t          face_count = (n + 1) * (n + 1);
    const Test_mesh         mesh       = make_textured_box(n);
    const Lod_chain_indices chain      = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_FALSE(chain.levels.empty());
    expect_valid_chain(chain, mesh.indices.size(), mesh.vertices.vertex_count);
    for (const Lod_chain_indices::Level& level : chain.levels) {
        for (std::size_t triangle = 0; triangle < (level.index_count / 3); ++triangle) {
            const uint32_t face = chain.indices[level.first_index + (3 * triangle)] / face_count;
            for (std::size_t corner = 1; corner < 3; ++corner) {
                EXPECT_EQ(chain.indices[level.first_index + (3 * triangle) + corner] / face_count, face);
            }
        }
    }
}

TEST(Lod, welded_box_chain_is_monotonic)
{
    const Test_mesh         mesh  = make_welded_box(8);
    const Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_GE(chain.levels.size(), 2u);
    expect_valid_chain(chain, mesh.indices.size(), mesh.vertices.vertex_count);
}

TEST(Lod, plain_cube_stops_early)
{
    // 24 vertices (flat normals: every edge is a hard edge), 12 triangles.
    // Nothing collapses without breaking the hard edges, so the chain stays
    // short - no level repeats the one before it.
    Test_mesh mesh = make_textured_box(1);
    for (std::size_t vertex = 0; vertex < mesh.vertices.vertex_count; ++vertex) {
        // make_textured_box() emits the faces +x, -x, +y, -y, +z, -z, four
        // vertices each.
        const std::size_t face = vertex / 4;
        glm::vec3 n{0.0f};
        n[static_cast<glm::length_t>(face / 2)] = ((face % 2) == 0) ? 1.0f : -1.0f;
        mesh.vertices.normals[(3 * vertex) + 0] = n.x;
        mesh.vertices.normals[(3 * vertex) + 1] = n.y;
        mesh.vertices.normals[(3 * vertex) + 2] = n.z;
    }
    const Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    EXPECT_LE(chain.levels.size(), 1u);
    expect_valid_chain(chain, mesh.indices.size(), mesh.vertices.vertex_count);
}

TEST(Lod, skinned_box_never_collapses_across_joints)
{
    // Joint 0 influences x < 0, joint 1 the rest. Vertices with a neighbour of
    // the other joint are locked; any triangle with an unlocked vertex must
    // then be all one joint, at every level, and the locked vertices stay.
    Test_mesh mesh = make_welded_box(8);
    const std::size_t vertex_count = mesh.vertices.vertex_count;
    std::vector<uint32_t> joint(vertex_count, 0);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        joint[vertex] = (mesh.vertices.positions[3 * vertex] < 0.0f) ? 0u : 1u;
        mesh.vertices.joint_indices.insert(mesh.vertices.joint_indices.end(), {static_cast<float>(joint[vertex]), 0.0f, 0.0f, 0.0f});
        mesh.vertices.joint_weights.insert(mesh.vertices.joint_weights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
    }
    std::vector<bool> locked(vertex_count, false);
    for (std::size_t triangle = 0; triangle < (mesh.indices.size() / 3); ++triangle) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t a = mesh.indices[(3 * triangle) + corner];
            const uint32_t b = mesh.indices[(3 * triangle) + ((corner + 1) % 3)];
            if (joint[a] != joint[b]) {
                locked[a] = true;
                locked[b] = true;
            }
        }
    }

    const Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_GE(chain.levels.size(), 1u);
    expect_valid_chain(chain, mesh.indices.size(), vertex_count);
    for (const Lod_chain_indices::Level& level : chain.levels) {
        std::set<uint32_t> referenced;
        for (std::size_t triangle = 0; triangle < (level.index_count / 3); ++triangle) {
            const uint32_t v0 = chain.indices[level.first_index + (3 * triangle) + 0];
            const uint32_t v1 = chain.indices[level.first_index + (3 * triangle) + 1];
            const uint32_t v2 = chain.indices[level.first_index + (3 * triangle) + 2];
            referenced.insert({v0, v1, v2});
            const bool has_unlocked = !locked[v0] || !locked[v1] || !locked[v2];
            if (has_unlocked) {
                EXPECT_EQ(joint[v0], joint[v1]);
                EXPECT_EQ(joint[v0], joint[v2]);
            }
        }
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            if (locked[vertex]) {
                EXPECT_TRUE(referenced.contains(static_cast<uint32_t>(vertex)));
            }
        }
    }
}

TEST(Lod, normal_merge_angle_lets_soft_splits_collapse)
{
    // A sphere whose seam copies differ only by slightly tilted normals: the
    // weld (default 20 degrees) merges them into one simplification vertex,
    // so the split no longer limits the simplification; with the angle at 0
    // the copies stay apart and behave like a seam.
    Test_mesh mesh = make_uv_sphere(32, 16);
    // Give the seam column the texcoords of column 0, so only the normals
    // (tilted by ~5 degrees) keep the copies apart.
    const uint32_t row = 33;
    for (uint32_t stack = 0; stack <= 16; ++stack) {
        const uint32_t seam  = (stack * row) + 32;
        const uint32_t first = stack * row;
        mesh.vertices.seam_attributes[2 * seam] = mesh.vertices.seam_attributes[2 * first];
        const glm::vec3 n{mesh.vertices.normals[3 * seam], mesh.vertices.normals[(3 * seam) + 1], mesh.vertices.normals[(3 * seam) + 2]};
        const glm::vec3 tilted = glm::normalize(n + glm::vec3{0.0f, 0.0f, 0.09f});
        mesh.vertices.normals[(3 * seam) + 0] = tilted.x;
        mesh.vertices.normals[(3 * seam) + 1] = tilted.y;
        mesh.vertices.normals[(3 * seam) + 2] = tilted.z;
    }
    Lod_settings no_weld{};
    no_weld.normal_merge_angle = 0.0f;
    const Lod_chain_indices welded   = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    const Lod_chain_indices unwelded = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, no_weld);
    expect_valid_chain(welded,   mesh.indices.size(), mesh.vertices.vertex_count);
    expect_valid_chain(unwelded, mesh.indices.size(), mesh.vertices.vertex_count);
    ASSERT_FALSE(welded.levels.empty());
    ASSERT_FALSE(unwelded.levels.empty());
    // The first step starts from the same triangles; without the split it
    // gets at least as far.
    EXPECT_LE(welded.levels.front().index_count, unwelded.levels.front().index_count);
}

TEST(Lod, rejects_bad_input)
{
    Test_mesh mesh = make_uv_sphere(8, 4);
    std::vector<uint32_t> bad = mesh.indices;
    bad.back() = static_cast<uint32_t>(mesh.vertices.vertex_count);
    EXPECT_TRUE(erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), bad, Lod_settings{}).is_empty());
    bad.pop_back();
    EXPECT_TRUE(erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), bad, Lod_settings{}).is_empty());
    Lod_settings one_level{};
    one_level.level_count = 1;
    EXPECT_TRUE(erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, one_level).is_empty());
}

namespace {

[[nodiscard]] auto make_float_format() -> Vertex_format
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

[[nodiscard]] auto make_sphere_geometry() -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("sphere");
    erhe::geometry::shapes::make_sphere(geometry->get_mesh(), 1.0f, 32, 16);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        }
    );
    return geometry;
}

[[nodiscard]] auto read_indices(erhe::buffer::Cpu_buffer& buffer, const erhe::primitive::Buffer_range& range) -> std::vector<uint32_t>
{
    std::vector<uint32_t> indices(range.count);
    std::memcpy(indices.data(), buffer.get_span().data() + range.byte_offset, range.count * sizeof(uint32_t));
    return indices;
}

[[nodiscard]] auto read_bytes(erhe::buffer::Cpu_buffer& buffer, const erhe::primitive::Buffer_range& range) -> std::vector<uint8_t>
{
    const std::size_t    byte_count = range.count * range.element_size;
    const uint8_t* const begin      = reinterpret_cast<const uint8_t*>(buffer.get_span().data()) + range.byte_offset;
    return std::vector<uint8_t>(begin, begin + byte_count);
}

class Cpu_build
{
public:
    Cpu_build()
        : vertex_buffer     {"test_vertex", 4 * 1024 * 1024}
        , index_buffer      {"test_index",  4 * 1024 * 1024}
        , vertex_buffer_sink{{&vertex_buffer}}
        , index_buffer_sink {index_buffer}
    {
    }

    [[nodiscard]] auto make_buffer_info(const Vertex_format& format) -> erhe::primitive::Buffer_info
    {
        return erhe::primitive::Buffer_info{
            .normal_style               = erhe::primitive::Normal_style::corner_normals,
            .index_type                 = Format::format_32_scalar_uint,
            .vertex_format              = format,
            .vertex_buffer_sink         = vertex_buffer_sink,
            .index_buffer_sink          = index_buffer_sink,
            .optimize_meshes            = true,
            .optimized_vertex_format    = &format,
            .optimized_vertex_input_key = 1
        };
    }

    erhe::buffer::Cpu_buffer                vertex_buffer;
    erhe::buffer::Cpu_buffer                index_buffer;
    erhe::primitive::Cpu_vertex_buffer_sink vertex_buffer_sink;
    erhe::primitive::Cpu_index_buffer_sink  index_buffer_sink;
};

} // namespace

TEST(Lod, geometry_path_stores_levels_after_level_0)
{
    const Vertex_format format = make_float_format();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_sphere_geometry();

    // Without settings: no chain, the index allocation is the fill range.
    Cpu_build plain_build;
    erhe::primitive::Primitive plain{geometry};
    {
        const erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info     = plain_build.make_buffer_info(format),
            .normal_style    = erhe::primitive::Normal_style::corner_normals
        };
        ASSERT_TRUE(plain.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    }
    ASSERT_TRUE(plain.optimized_render_shape);
    const erhe::primitive::Buffer_mesh& plain_mesh = plain.optimized_render_shape->get_renderable_mesh();
    EXPECT_TRUE(plain_mesh.lod_chain.is_empty());
    EXPECT_EQ(plain_mesh.index_buffer_range.count, plain_mesh.triangle_fill_indices.index_count);

    // With settings on the Primitive: the same level 0 bytes and vertices,
    // the levels after it in the same allocation.
    Cpu_build lod_build;
    erhe::primitive::Primitive with_lod{geometry};
    with_lod.lod_settings = Lod_settings{};
    {
        const erhe::primitive::Build_info build_info{
            .primitive_types = {.fill_triangles = true},
            .buffer_info     = lod_build.make_buffer_info(format),
            .normal_style    = erhe::primitive::Normal_style::corner_normals
        };
        ASSERT_TRUE(with_lod.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    }
    ASSERT_TRUE(with_lod.optimized_render_shape);
    const erhe::primitive::Buffer_mesh& lod_mesh = with_lod.optimized_render_shape->get_renderable_mesh();
    const erhe::primitive::Lod_chain&   chain    = lod_mesh.lod_chain;
    ASSERT_GE(chain.level_count, 3u);

    EXPECT_EQ(lod_mesh.triangle_fill_indices.first_index, plain_mesh.triangle_fill_indices.first_index);
    EXPECT_EQ(lod_mesh.triangle_fill_indices.index_count, plain_mesh.triangle_fill_indices.index_count);
    EXPECT_EQ(chain.levels[0].index_range.first_index, lod_mesh.triangle_fill_indices.first_index);
    EXPECT_EQ(chain.levels[0].index_range.index_count, lod_mesh.triangle_fill_indices.index_count);
    EXPECT_EQ(chain.levels[0].error, 0.0f);

    std::size_t next_first_index = lod_mesh.triangle_fill_indices.first_index + lod_mesh.triangle_fill_indices.index_count;
    for (std::size_t level = 1; level < chain.level_count; ++level) {
        const erhe::primitive::Lod_level& lod_level = chain.levels[level];
        EXPECT_EQ(lod_level.index_range.first_index, next_first_index);
        EXPECT_LT(lod_level.index_range.index_count, chain.levels[level - 1].index_range.index_count);
        EXPECT_GT(lod_level.error, chain.levels[level - 1].error);
        next_first_index += lod_level.index_range.index_count;
    }
    EXPECT_EQ(lod_mesh.index_buffer_range.count, next_first_index);

    const std::vector<uint32_t> plain_indices = read_indices(plain_build.index_buffer, plain_mesh.index_buffer_range);
    const std::vector<uint32_t> lod_indices   = read_indices(lod_build.index_buffer,   lod_mesh.index_buffer_range);
    ASSERT_GE(lod_indices.size(), plain_indices.size());
    EXPECT_TRUE(std::equal(plain_indices.begin(), plain_indices.end(), lod_indices.begin()));
    const std::size_t vertex_count = lod_mesh.vertex_buffer_ranges.front().count;
    for (const uint32_t index : lod_indices) {
        ASSERT_LT(index, vertex_count);
    }
    EXPECT_EQ(
        read_bytes(plain_build.vertex_buffer, plain_mesh.vertex_buffer_ranges.front()),
        read_bytes(lod_build.vertex_buffer,   lod_mesh.vertex_buffer_ranges.front())
    );

    // The base variant never carries a chain.
    EXPECT_TRUE(with_lod.render_shape->get_renderable_mesh().lod_chain.is_empty());
}

TEST(Lod, soup_path_stores_levels_after_level_0)
{
    // The UV sphere as a triangle soup (position, normal, texcoord).
    const Test_mesh mesh = make_uv_sphere(32, 16);
    const Vertex_format soup_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position,  0},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0}
            }
        }
    };
    std::shared_ptr<erhe::primitive::Triangle_soup> soup = std::make_shared<erhe::primitive::Triangle_soup>();
    soup->vertex_format = soup_format;
    soup->index_data    = mesh.indices;
    const std::size_t stride = soup_format.streams.front().stride;
    soup->vertex_data.assign(mesh.vertices.vertex_count * stride, uint8_t{0});
    for (std::size_t vertex = 0; vertex < mesh.vertices.vertex_count; ++vertex) {
        uint8_t* const destination = soup->vertex_data.data() + (vertex * stride);
        std::memcpy(destination + 0,  &mesh.vertices.positions      [3 * vertex], 3 * sizeof(float));
        std::memcpy(destination + 12, &mesh.vertices.normals        [3 * vertex], 3 * sizeof(float));
        std::memcpy(destination + 24, &mesh.vertices.seam_attributes[2 * vertex], 2 * sizeof(float));
    }

    erhe::primitive::Primitive primitive{soup};

    Cpu_build plain_build;
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> plain = erhe::primitive::make_optimized_render_shape(
        *primitive.render_shape, erhe::primitive::Element_mappings{}, erhe::primitive::Mesh_optimize_options{},
        plain_build.make_buffer_info(soup_format), std::filesystem::path{}, "plain"
    );
    ASSERT_TRUE(plain);
    const erhe::primitive::Buffer_mesh& plain_mesh = plain->get_renderable_mesh();
    EXPECT_TRUE(plain_mesh.lod_chain.is_empty());
    EXPECT_EQ(plain_mesh.index_buffer_range.count, plain_mesh.triangle_fill_indices.index_count);

    Cpu_build lod_build;
    const Lod_settings settings{};
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> with_lod = erhe::primitive::make_optimized_render_shape(
        *primitive.render_shape, erhe::primitive::Element_mappings{}, erhe::primitive::Mesh_optimize_options{},
        lod_build.make_buffer_info(soup_format), std::filesystem::path{}, "lod", &settings
    );
    ASSERT_TRUE(with_lod);
    const erhe::primitive::Buffer_mesh& lod_mesh = with_lod->get_renderable_mesh();
    const erhe::primitive::Lod_chain&   chain    = lod_mesh.lod_chain;
    ASSERT_GE(chain.level_count, 3u);
    std::size_t next_first_index = lod_mesh.triangle_fill_indices.index_count;
    for (std::size_t level = 1; level < chain.level_count; ++level) {
        EXPECT_EQ(chain.levels[level].index_range.first_index, next_first_index);
        next_first_index += chain.levels[level].index_range.index_count;
    }
    EXPECT_EQ(lod_mesh.index_buffer_range.count, next_first_index);

    const std::vector<uint32_t> plain_indices = read_indices(plain_build.index_buffer, plain_mesh.index_buffer_range);
    const std::vector<uint32_t> lod_indices   = read_indices(lod_build.index_buffer,   lod_mesh.index_buffer_range);
    EXPECT_TRUE(std::equal(plain_indices.begin(), plain_indices.end(), lod_indices.begin()));
    const std::size_t vertex_count = lod_mesh.vertex_buffer_ranges.front().count;
    for (const uint32_t index : lod_indices) {
        ASSERT_LT(index, vertex_count);
    }
    EXPECT_EQ(
        read_bytes(plain_build.vertex_buffer, plain_mesh.vertex_buffer_ranges.front()),
        read_bytes(lod_build.vertex_buffer,   lod_mesh.vertex_buffer_ranges.front())
    );
}

// Per-level edge lines (build_lod_edge_lines(), doc/plans/selection_outline_lod.md
// step 1): every edge of a level's triangles once, packed like the base edge
// lines, stored per level in the optimized variant's edge-line allocations.

namespace {

class Unpacked_endpoint
{
public:
    glm::vec3 position;
    glm::vec3 normal;
    float     packed_w;
};

[[nodiscard]] auto unpack_endpoints(const std::vector<uint8_t>& data, const std::size_t first, const std::size_t count) -> std::vector<Unpacked_endpoint>
{
    std::vector<Unpacked_endpoint> result;
    for (std::size_t i = first; i < first + count; ++i) {
        float values[erhe::primitive::edge_line_vertex_float_count];
        std::memcpy(values, data.data() + (i * erhe::primitive::edge_line_vertex_byte_size), sizeof(values));
        result.push_back(
            Unpacked_endpoint{
                .position = glm::vec3{values[0], values[1], values[2]},
                .normal   = glm::vec3{values[4], values[5], values[6]},
                .packed_w = values[7]
            }
        );
    }
    return result;
}

// The unique position-pair edges of a triangle list, counted independently of
// build_lod_edge_lines().
[[nodiscard]] auto count_position_edges(const Test_mesh& mesh, const std::span<const uint32_t> triangles) -> std::size_t
{
    std::set<std::pair<std::array<float, 3>, std::array<float, 3>>> edges;
    const auto get_position = [&](const uint32_t vertex) -> std::array<float, 3> {
        return {mesh.vertices.positions[3 * vertex], mesh.vertices.positions[(3 * vertex) + 1], mesh.vertices.positions[(3 * vertex) + 2]};
    };
    for (std::size_t corner = 0; corner < triangles.size(); ++corner) {
        const std::size_t          next = ((corner % 3) == 2) ? (corner - 2) : (corner + 1);
        const std::array<float, 3> a    = get_position(triangles[corner]);
        const std::array<float, 3> b    = get_position(triangles[next]);
        if (a != b) {
            edges.insert((a < b) ? std::make_pair(a, b) : std::make_pair(b, a));
        }
    }
    return edges.size();
}

} // namespace

TEST(Lod_edge_lines, endpoint_packing_signs_and_winding)
{
    // Edge a = (0,0,0) -> b = (1,0,0), face A above it in the z = 0 plane
    // (centroid y > 0), walking a -> b; face B below it, walking b -> a.
    const erhe::primitive::Edge_line_face face_a{.normal = glm::vec3{0.0f, 0.0f, 1.0f}, .center = glm::vec3{0.5f,  0.3f, 0.0f}, .walks_a_to_b = true};
    const erhe::primitive::Edge_line_face face_b{.normal = glm::vec3{0.0f, 0.0f, 1.0f}, .center = glm::vec3{0.5f, -0.3f, 0.0f}, .walks_a_to_b = false};
    float out[2 * erhe::primitive::edge_line_vertex_float_count];
    erhe::primitive::write_edge_line_endpoints(glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, &face_a, &face_b, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, out);
    // cross(n, b - a) = +y: toward face A's centroid (sign +1), away from
    // face B's (sign -1). |w| 1 walks a -> b, 2 walks b -> a.
    EXPECT_EQ(out[7],  1.0f);
    EXPECT_EQ(out[15], -2.0f);
    EXPECT_EQ(out[0], 0.0f);
    EXPECT_EQ(out[8], 1.0f);

    // Boundary edge: face B mirrors face A.
    erhe::primitive::write_edge_line_endpoints(glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, &face_a, nullptr, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, out);
    EXPECT_EQ(out[7],  1.0f);
    EXPECT_EQ(out[15], -2.0f);
    EXPECT_EQ(glm::vec3(out[12], out[13], out[14]), glm::vec3(0.0f, 0.0f, 1.0f));

    // No face: fallback normals, w 0 (the shader's degenerate frame).
    erhe::primitive::write_edge_line_endpoints(glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, nullptr, nullptr, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, out);
    EXPECT_EQ(out[7],  0.0f);
    EXPECT_EQ(out[15], 0.0f);
    EXPECT_EQ(glm::vec3(out[4], out[5], out[6]),    glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(glm::vec3(out[12], out[13], out[14]), glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(Lod_edge_lines, every_level_edge_once_across_seams)
{
    // The textured box splits every box edge and corner into separate
    // vertices (texture seams); the UV sphere has a seam column. Each level's
    // edges must be exactly the unique position-pair edges of its triangles,
    // and on these closed meshes every edge has two faces walking it in
    // opposite directions.
    const std::array<Test_mesh, 2> meshes{make_textured_box(6), make_uv_sphere(32, 16)};
    for (const Test_mesh& mesh : meshes) {
        Lod_settings settings{};
        settings.seams = erhe::primitive::Lod_seams::permissive;
        Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, settings);
        ASSERT_FALSE(chain.is_empty());
        erhe::primitive::build_lod_edge_lines(mesh.vertices.get_input(), chain);
        EXPECT_TRUE(chain.edge_line_joint_data.empty());
        std::size_t next_first = 0;
        for (const Lod_chain_indices::Level& level : chain.levels) {
            const std::span<const uint32_t> triangles{chain.indices.data() + level.first_index, level.index_count};
            EXPECT_EQ(level.first_edge_line_vertex, next_first);
            EXPECT_EQ(level.edge_line_vertex_count, 2 * count_position_edges(mesh, triangles));
            next_first += level.edge_line_vertex_count;
            const std::vector<Unpacked_endpoint> endpoints = unpack_endpoints(chain.edge_line_vertex_data, level.first_edge_line_vertex, level.edge_line_vertex_count);
            for (std::size_t edge = 0; edge < endpoints.size() / 2; ++edge) {
                const Unpacked_endpoint& a = endpoints[2 * edge];
                const Unpacked_endpoint& b = endpoints[(2 * edge) + 1];
                EXPECT_NE(a.position, b.position);
                ASSERT_NE(a.packed_w, 0.0f);
                ASSERT_NE(b.packed_w, 0.0f);
                // Opposite traversal: one face walks a -> b (|w| 1), the other b -> a (|w| 2).
                EXPECT_NE(std::abs(a.packed_w), std::abs(b.packed_w));
                EXPECT_NEAR(glm::length(a.normal), 1.0f, 1.0e-5f);
                EXPECT_NEAR(glm::length(b.normal), 1.0f, 1.0e-5f);
            }
        }
        EXPECT_EQ(chain.get_edge_line_vertex_count(), next_first);
    }
}

TEST(Lod_edge_lines, skinned_endpoints_carry_joints)
{
    Test_mesh mesh = make_welded_box(8);
    const std::size_t vertex_count = mesh.vertices.vertex_count;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const float joint = (mesh.vertices.positions[3 * vertex] < 0.0f) ? 0.0f : 1.0f;
        mesh.vertices.joint_indices.insert(mesh.vertices.joint_indices.end(), {joint, 0.0f, 0.0f, 0.0f});
        mesh.vertices.joint_weights.insert(mesh.vertices.joint_weights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
    }
    Lod_chain_indices chain = erhe::primitive::generate_lod_chain(mesh.vertices.get_input(), mesh.indices, Lod_settings{});
    ASSERT_FALSE(chain.is_empty());
    erhe::primitive::build_lod_edge_lines(mesh.vertices.get_input(), chain);
    const std::size_t endpoint_count = chain.get_edge_line_vertex_count();
    ASSERT_GT(endpoint_count, 0u);
    ASSERT_EQ(chain.edge_line_joint_data.size(), endpoint_count * erhe::primitive::edge_line_joint_byte_size);
    const std::vector<Unpacked_endpoint> endpoints = unpack_endpoints(chain.edge_line_vertex_data, 0, endpoint_count);
    for (std::size_t i = 0; i < endpoint_count; ++i) {
        uint32_t joint_indices[4];
        float    joint_weights[4];
        std::memcpy(joint_indices, chain.edge_line_joint_data.data() + (i * erhe::primitive::edge_line_joint_byte_size), sizeof(joint_indices));
        std::memcpy(joint_weights, chain.edge_line_joint_data.data() + (i * erhe::primitive::edge_line_joint_byte_size) + sizeof(joint_indices), sizeof(joint_weights));
        EXPECT_EQ(joint_indices[0], (endpoints[i].position.x < 0.0f) ? 0u : 1u);
        EXPECT_EQ(joint_weights[0], 1.0f);
    }
}

namespace {

class Edge_line_streams
{
public:
    Vertex_stream vertices{
        0,
        {
            {Format::format_32_vec4_float, Vertex_attribute_usage::position, 0},
            {Format::format_32_vec4_float, Vertex_attribute_usage::normal,   0}
        }
    };
    Vertex_stream joints{
        0,
        {
            {Format::format_32_vec4_uint,  Vertex_attribute_usage::joint_indices, 0},
            {Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, 0}
        }
    };
};

// Each level i > 0 has its own edge-line range, back to back in one
// allocation, holding packed endpoints with faces.
void expect_level_edge_lines(erhe::buffer::Cpu_buffer& vertex_buffer, const erhe::primitive::Lod_chain& chain)
{
    ASSERT_GE(chain.level_count, 2u);
    EXPECT_EQ(chain.levels[0].edge_line_vertex_range.count, 0u);
    std::size_t next_byte_offset = chain.levels[1].edge_line_vertex_range.byte_offset;
    for (std::size_t level = 1; level < chain.level_count; ++level) {
        const erhe::primitive::Buffer_range& range = chain.levels[level].edge_line_vertex_range;
        EXPECT_GT(range.count, 0u);
        EXPECT_EQ(range.count % 2, 0u);
        EXPECT_EQ(range.element_size, erhe::primitive::edge_line_vertex_byte_size);
        EXPECT_EQ(range.byte_offset, next_byte_offset);
        next_byte_offset += range.get_byte_size();
        EXPECT_EQ(chain.levels[level].edge_line_joint_range.count, 0u);
        const std::vector<uint8_t>           bytes     = read_bytes(vertex_buffer, range);
        const std::vector<Unpacked_endpoint> endpoints = unpack_endpoints(bytes, 0, range.count);
        for (const Unpacked_endpoint& endpoint : endpoints) {
            EXPECT_NE(endpoint.packed_w, 0.0f);
            EXPECT_NEAR(glm::length(endpoint.normal), 1.0f, 1.0e-5f);
        }
    }
}

[[nodiscard]] auto make_uv_sphere_soup(const Test_mesh& mesh, const Vertex_format& soup_format) -> std::shared_ptr<erhe::primitive::Triangle_soup>
{
    std::shared_ptr<erhe::primitive::Triangle_soup> soup = std::make_shared<erhe::primitive::Triangle_soup>();
    soup->vertex_format = soup_format;
    soup->index_data    = mesh.indices;
    const std::size_t stride = soup_format.streams.front().stride;
    soup->vertex_data.assign(mesh.vertices.vertex_count * stride, uint8_t{0});
    for (std::size_t vertex = 0; vertex < mesh.vertices.vertex_count; ++vertex) {
        uint8_t* const destination = soup->vertex_data.data() + (vertex * stride);
        std::memcpy(destination + 0,  &mesh.vertices.positions      [3 * vertex], 3 * sizeof(float));
        std::memcpy(destination + 12, &mesh.vertices.normals        [3 * vertex], 3 * sizeof(float));
        std::memcpy(destination + 24, &mesh.vertices.seam_attributes[2 * vertex], 2 * sizeof(float));
    }
    return soup;
}

} // namespace

TEST(Lod_edge_lines, geometry_path_stores_level_edge_lines)
{
    const Vertex_format format = make_float_format();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_sphere_geometry();
    const Edge_line_streams streams;

    Cpu_build build;
    erhe::primitive::Primitive primitive{geometry};
    primitive.lod_settings = Lod_settings{};
    erhe::primitive::Buffer_info buffer_info = build.make_buffer_info(format);
    buffer_info.edge_line_vertex_stream = &streams.vertices;
    buffer_info.edge_line_joint_stream  = &streams.joints;
    const erhe::primitive::Build_info build_info{
        .primitive_types = {.fill_triangles = true},
        .buffer_info     = buffer_info,
        .normal_style    = erhe::primitive::Normal_style::corner_normals
    };
    ASSERT_TRUE(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    ASSERT_TRUE(primitive.optimized_render_shape);
    const erhe::primitive::Buffer_mesh& mesh = primitive.optimized_render_shape->get_renderable_mesh();
    expect_level_edge_lines(build.vertex_buffer, mesh.lod_chain);
    // The optimized variant has no edge lines of its own.
    EXPECT_EQ(mesh.edge_line_vertex_buffer_range.count, 0u);
}

TEST(Lod_edge_lines, soup_path_stores_level_edge_lines)
{
    const Test_mesh     mesh = make_uv_sphere(32, 16);
    const Vertex_format soup_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position,  0},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0}
            }
        }
    };
    erhe::primitive::Primitive primitive{make_uv_sphere_soup(mesh, soup_format)};
    const Edge_line_streams streams;

    Cpu_build build;
    erhe::primitive::Buffer_info buffer_info = build.make_buffer_info(soup_format);
    buffer_info.edge_line_vertex_stream = &streams.vertices;
    buffer_info.edge_line_joint_stream  = &streams.joints;
    const Lod_settings settings{};
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> shape = erhe::primitive::make_optimized_render_shape(
        *primitive.render_shape, erhe::primitive::Element_mappings{}, erhe::primitive::Mesh_optimize_options{},
        buffer_info, std::filesystem::path{}, "lod", &settings
    );
    ASSERT_TRUE(shape);
    expect_level_edge_lines(build.vertex_buffer, shape->get_renderable_mesh().lod_chain);

    // Without a LOD request the soup optimize allocates no edge lines: the
    // vertex buffer holds only the variant's vertices.
    Cpu_build                    plain_build;
    erhe::primitive::Buffer_info plain_info = plain_build.make_buffer_info(soup_format);
    plain_info.edge_line_vertex_stream = &streams.vertices;
    const std::shared_ptr<erhe::primitive::Primitive_render_shape> plain = erhe::primitive::make_optimized_render_shape(
        *primitive.render_shape, erhe::primitive::Element_mappings{}, erhe::primitive::Mesh_optimize_options{},
        plain_info, std::filesystem::path{}, "plain"
    );
    ASSERT_TRUE(plain);
    EXPECT_EQ(
        plain_build.vertex_buffer.get_used_byte_count(),
        plain->get_renderable_mesh().vertex_buffer_ranges.front().get_byte_size()
    );
}

namespace {

// One face of an edge as packed in an edge-line endpoint, expressed for the
// edge walked from its lexicographically lower endpoint position to the
// higher one, so the two edge-line builds compare whatever endpoint order
// they stored.
class Canonical_face
{
public:
    glm::vec3 normal;
    float     interior_sign;
    bool      walks_lo_to_hi;
};

using Position_key = std::array<float, 3>;
using Edge_key     = std::pair<Position_key, Position_key>;

[[nodiscard]] auto to_key(const glm::vec3& p) -> Position_key
{
    return Position_key{p.x, p.y, p.z};
}

[[nodiscard]] auto collect_canonical_edges(const std::vector<Unpacked_endpoint>& endpoints) -> std::map<Edge_key, std::array<Canonical_face, 2>>
{
    std::map<Edge_key, std::array<Canonical_face, 2>> result;
    for (std::size_t edge = 0; edge < endpoints.size() / 2; ++edge) {
        const Unpacked_endpoint& a        = endpoints[2 * edge];
        const Unpacked_endpoint& b        = endpoints[(2 * edge) + 1];
        const Position_key       key_a    = to_key(a.position);
        const Position_key       key_b    = to_key(b.position);
        const bool               reversed = (key_b < key_a);
        // Walking b -> a negates the edge direction: the interior sign
        // (taken against cross(normal, edge direction)) and the walk flip.
        const auto make_face = [reversed](const Unpacked_endpoint& endpoint) -> Canonical_face {
            const float sign  = (endpoint.packed_w < 0.0f) ? -1.0f : 1.0f;
            const bool  a_to_b = (std::abs(endpoint.packed_w) == 1.0f);
            return Canonical_face{
                .normal         = endpoint.normal,
                .interior_sign  = reversed ? -sign : sign,
                .walks_lo_to_hi = reversed ? !a_to_b : a_to_b
            };
        };
        result[reversed ? Edge_key{key_b, key_a} : Edge_key{key_a, key_b}] = {make_face(a), make_face(b)};
    }
    return result;
}

} // namespace

TEST(Lod_edge_lines, packing_matches_the_base_edge_lines)
{
    // The same closed mesh through both edge-line builds: the base variant's
    // (Build_context::build_edge_lines(), polygon faces) and
    // build_lod_edge_lines() over a fan triangulation of the same polygons.
    // The sphere's quads are planar, so every polygon edge must carry the
    // same two faces in both: normal, interior sign and winding.
    const Vertex_format format = make_float_format();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("sphere");
    erhe::geometry::shapes::make_sphere(geometry->get_mesh(), 1.0f, 32, 16);
    geometry->process(
        {
            .flags =
                erhe::geometry::Geometry::process_flag_connect |
                erhe::geometry::Geometry::process_flag_build_edges |
                erhe::geometry::Geometry::process_flag_compute_facet_centroids |
                erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
        }
    );
    const Edge_line_streams streams;

    Cpu_build build;
    erhe::primitive::Primitive primitive{geometry};
    erhe::primitive::Buffer_info buffer_info = build.make_buffer_info(format);
    buffer_info.optimize_meshes         = false;
    buffer_info.edge_line_vertex_stream = &streams.vertices;
    const erhe::primitive::Build_info build_info{
        .primitive_types = {.fill_triangles = true, .edge_lines = true},
        .buffer_info     = buffer_info,
        .normal_style    = erhe::primitive::Normal_style::corner_normals
    };
    ASSERT_TRUE(primitive.make_renderable_mesh(build_info, erhe::primitive::Normal_style::corner_normals));
    const erhe::primitive::Buffer_range& base_range = primitive.render_shape->get_renderable_mesh().edge_line_vertex_buffer_range;
    ASSERT_GT(base_range.count, 0u);
    const std::map<Edge_key, std::array<Canonical_face, 2>> base_edges =
        collect_canonical_edges(unpack_endpoints(read_bytes(build.vertex_buffer, base_range), 0, base_range.count));

    const GEO::Mesh& mesh = geometry->get_mesh();
    Lod_vertex_data  lod_vertices;
    lod_vertices.vertex_count = mesh.vertices.nb();
    for (GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
        lod_vertices.positions.insert(lod_vertices.positions.end(), {p.x, p.y, p.z});
    }
    Lod_chain_indices chain;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t corner = 1; (corner + 1) < corner_count; ++corner) {
            chain.indices.insert(
                chain.indices.end(),
                {mesh.facets.vertex(facet, 0), mesh.facets.vertex(facet, corner), mesh.facets.vertex(facet, corner + 1)}
            );
        }
    }
    chain.levels.push_back(Lod_chain_indices::Level{.first_index = 0, .index_count = chain.indices.size()});
    erhe::primitive::build_lod_edge_lines(lod_vertices.get_input(), chain);
    const std::map<Edge_key, std::array<Canonical_face, 2>> lod_edges =
        collect_canonical_edges(unpack_endpoints(chain.edge_line_vertex_data, 0, chain.get_edge_line_vertex_count()));

    // Every polygon edge (the fan diagonals are extra in the LOD build).
    ASSERT_EQ(base_edges.size(), mesh.edges.nb());
    std::size_t compared = 0;
    for (const auto& [key, base_faces] : base_edges) {
        const auto it = lod_edges.find(key);
        ASSERT_NE(it, lod_edges.end());
        // A closed, consistently wound mesh walks each edge once each way,
        // which pairs the faces of the two builds.
        ASSERT_NE(base_faces[0].walks_lo_to_hi, base_faces[1].walks_lo_to_hi);
        for (const Canonical_face& base_face : base_faces) {
            const std::array<Canonical_face, 2>& lod_faces = it->second;
            const Canonical_face& lod_face = (lod_faces[0].walks_lo_to_hi == base_face.walks_lo_to_hi) ? lod_faces[0] : lod_faces[1];
            ASSERT_EQ(lod_face.walks_lo_to_hi, base_face.walks_lo_to_hi);
            EXPECT_EQ(lod_face.interior_sign, base_face.interior_sign);
            EXPECT_GT(glm::dot(lod_face.normal, base_face.normal), 0.9999f);
            ++compared;
        }
    }
    EXPECT_EQ(compared, 2 * mesh.edges.nb());
}
