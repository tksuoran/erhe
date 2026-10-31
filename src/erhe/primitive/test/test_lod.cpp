#include "erhe_primitive/lod.hpp"

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
#include <set>
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
