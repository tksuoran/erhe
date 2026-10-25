#include "test_helpers.hpp"

#include "erhe_raytrace/prebuilt_bvh.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

class Mesh_data
{
public:
    std::string             name;
    std::vector<glm::vec3>  vertices;
    std::vector<glm::uvec3> triangles;
};

// Indexed height field grid, n x n quads, 2 * n * n triangles
auto make_wavy_grid(const uint32_t n) -> Mesh_data
{
    Mesh_data mesh{.name = "wavy_grid"};
    for (uint32_t y = 0; y <= n; ++y) {
        for (uint32_t x = 0; x <= n; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(n);
            const float fy = static_cast<float>(y) / static_cast<float>(n);
            const float h  = 0.1f * std::sin(fx * 12.0f) * std::cos(fy * 9.0f);
            mesh.vertices.emplace_back(fx - 0.5f, fy - 0.5f, h);
        }
    }
    for (uint32_t y = 0; y < n; ++y) {
        for (uint32_t x = 0; x < n; ++x) {
            const uint32_t i00 = (y * (n + 1)) + x;
            const uint32_t i10 = i00 + 1;
            const uint32_t i01 = i00 + (n + 1);
            const uint32_t i11 = i01 + 1;
            mesh.triangles.emplace_back(i00, i10, i11);
            mesh.triangles.emplace_back(i00, i11, i01);
        }
    }
    return mesh;
}

// Unindexed triangle soup of a UV sphere: three unique vertices per triangle
auto make_sphere_soup(const uint32_t stacks, const uint32_t slices) -> Mesh_data
{
    Mesh_data mesh{.name = "sphere_soup"};
    const float pi = 3.14159265358979f;
    auto point = [&](const uint32_t stack, const uint32_t slice) -> glm::vec3 {
        const float theta = pi * static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi   = 2.0f * pi * static_cast<float>(slice) / static_cast<float>(slices);
        return glm::vec3{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
    };
    auto add_triangle = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(a);
        mesh.vertices.push_back(b);
        mesh.vertices.push_back(c);
        mesh.triangles.emplace_back(base, base + 1, base + 2);
    };
    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            const glm::vec3 p00 = point(stack,     slice);
            const glm::vec3 p01 = point(stack,     slice + 1);
            const glm::vec3 p10 = point(stack + 1, slice);
            const glm::vec3 p11 = point(stack + 1, slice + 1);
            if (stack != 0) {
                add_triangle(p00, p01, p11);
            }
            if (stack != (stacks - 1)) {
                add_triangle(p00, p11, p10);
            }
        }
    }
    return mesh;
}

// Deterministic LCG; no std:: distributions (their output is implementation defined)
class Lcg
{
public:
    auto next_unit() -> float
    {
        m_state = (m_state * 1664525u) + 1013904223u;
        return static_cast<float>(m_state >> 8) * (1.0f / 16777216.0f);
    }
    auto next_signed() -> float { return (2.0f * next_unit()) - 1.0f; }

private:
    uint32_t m_state{12345u};
};

// Rays in world space, aimed from outside the mesh bounds at points inside them
auto make_rays(const Mesh_data& mesh, const glm::mat4& transform, const std::size_t count) -> std::vector<Ray>
{
    glm::vec3 min_corner{ std::numeric_limits<float>::max()};
    glm::vec3 max_corner{-std::numeric_limits<float>::max()};
    for (const glm::vec3& v : mesh.vertices) {
        min_corner = glm::min(min_corner, v);
        max_corner = glm::max(max_corner, v);
    }
    const glm::vec3 center      = 0.5f * (min_corner + max_corner);
    const glm::vec3 half_extent = 0.5f * (max_corner - min_corner);
    const float     radius      = (2.0f * glm::length(half_extent)) + 0.1f;

    Lcg lcg;
    std::vector<Ray> rays;
    while (rays.size() < count) {
        const glm::vec3 out{lcg.next_signed(), lcg.next_signed(), lcg.next_signed()};
        const float out_length = glm::length(out);
        if ((out_length < 0.1f) || (out_length > 1.0f)) {
            continue;
        }
        const glm::vec3 local_origin = center + ((radius / out_length) * out);
        const glm::vec3 local_target = center + (0.9f * half_extent * glm::vec3{lcg.next_signed(), lcg.next_signed(), lcg.next_signed()});
        const glm::vec3 origin = glm::vec3{transform * glm::vec4{local_origin, 1.0f}};
        const glm::vec3 target = glm::vec3{transform * glm::vec4{local_target, 1.0f}};
        rays.push_back(make_ray(origin, glm::normalize(target - origin), 1000.0f));
    }
    return rays;
}

// Geometry in a child scene, instanced into a root scene with a transform
class Instanced
{
public:
    Instanced(IGeometry* geometry, const glm::mat4& transform)
        : child_scene{IScene::create_unique("child")}
        , root_scene {IScene::create_unique("root")}
        , instance   {IInstance::create_unique("instance")}
    {
        child_scene->attach(geometry);
        child_scene->commit();
        instance->set_scene(child_scene.get());
        instance->set_transform(transform);
        instance->set_mask(0xffffffffu);
        instance->commit();
        root_scene->attach(instance.get());
        root_scene->commit();
    }

    std::unique_ptr<IScene>    child_scene;
    std::unique_ptr<IScene>    root_scene;
    std::unique_ptr<IInstance> instance;
};

auto get_test_transform() -> glm::mat4
{
    const glm::mat4 translation = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, -3.0f});
    const glm::mat4 rotation    = glm::rotate(glm::mat4{1.0f}, 0.7f, glm::normalize(glm::vec3{1.0f, 2.0f, 0.5f}));
    return translation * rotation;
}

// Casts the same rays through both geometries and requires bit-identical
// results. Returns the number of rays that hit.
auto expect_identical_hits(const Mesh_data& mesh, IGeometry* reference, IGeometry* candidate, const std::size_t ray_count) -> std::size_t
{
    const glm::mat4 transform = get_test_transform();
    Instanced a{reference, transform};
    Instanced b{candidate, transform};
    const std::vector<Ray> rays = make_rays(mesh, transform, ray_count);
    std::size_t hit_count = 0;
    for (std::size_t i = 0; i < rays.size(); ++i) {
        Ray ray_a = rays[i];
        Ray ray_b = rays[i];
        Hit hit_a{};
        Hit hit_b{};
        const bool is_hit_a = a.root_scene->intersect(ray_a, hit_a);
        const bool is_hit_b = b.root_scene->intersect(ray_b, hit_b);
        EXPECT_EQ(is_hit_a, is_hit_b) << "ray " << i;
        if (!is_hit_a || !is_hit_b) {
            continue;
        }
        ++hit_count;
        EXPECT_EQ(std::bit_cast<uint32_t>(ray_a.t_far), std::bit_cast<uint32_t>(ray_b.t_far)) << "ray " << i;
        EXPECT_EQ(hit_a.triangle_id, hit_b.triangle_id) << "ray " << i;
        EXPECT_EQ(std::bit_cast<uint32_t>(hit_a.uv.x), std::bit_cast<uint32_t>(hit_b.uv.x)) << "ray " << i;
        EXPECT_EQ(std::bit_cast<uint32_t>(hit_a.uv.y), std::bit_cast<uint32_t>(hit_b.uv.y)) << "ray " << i;
        EXPECT_EQ(hit_a.instance, a.instance.get()) << "ray " << i;
        EXPECT_EQ(hit_b.instance, b.instance.get()) << "ray " << i;
        EXPECT_EQ(hit_a.geometry, reference) << "ray " << i;
        EXPECT_EQ(hit_b.geometry, candidate) << "ray " << i;
    }
    return hit_count;
}

auto nodes_identical(const std::vector<Bvh_node_record>& lhs, const std::vector<Bvh_node_record>& rhs) -> bool
{
    return
        (lhs.size() == rhs.size()) &&
        (std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(Bvh_node_record)) == 0);
}

class Exported
{
public:
    Mesh_data     mesh;
    Test_geometry reference;
    Prebuilt_bvh  bvh;
};

// Commits the reference geometry and exports its BVH. Returns false when
// the backend cannot export.
auto make_exported(Mesh_data&& mesh, Exported& out) -> bool
{
    out.mesh      = std::move(mesh);
    out.reference = make_triangle_geometry(out.mesh.name, out.mesh.vertices, out.mesh.triangles);
    return out.reference.geometry->export_bvh(out.bvh);
}

constexpr std::size_t c_ray_count = 400;

void run_round_trip(Mesh_data&& mesh_data)
{
    Exported exported;
    if (!make_exported(std::move(mesh_data), exported)) {
        GTEST_SKIP() << "raytrace backend has no BVH export";
    }
    const Mesh_data& mesh      = exported.mesh;
    IGeometry*       reference = exported.reference.geometry.get();

    ASSERT_NE(reference->get_bvh_source(), Bvh_source::none);
    ASSERT_NE(reference->get_bvh_source(), Bvh_source::prebuilt);
    ASSERT_TRUE(reference->get_triangle_hash().has_value());
    EXPECT_EQ(exported.bvh.backend, get_prebuilt_bvh_backend());
    EXPECT_EQ(exported.bvh.backend, "bvh_v2");
    EXPECT_EQ(exported.bvh.library, get_prebuilt_bvh_library());
    EXPECT_EQ(exported.bvh.library.size(), 40u); // the pinned 40-digit commit id
    EXPECT_EQ(exported.bvh.triangle_hash, reference->get_triangle_hash().value());
    EXPECT_EQ(exported.bvh.triangle_count, mesh.triangles.size());
    EXPECT_EQ(reference->get_triangle_count(), mesh.triangles.size());
    EXPECT_EQ(exported.bvh.primitive_ids.size(), mesh.triangles.size());
    EXPECT_GT(exported.bvh.nodes.size(), 1u);
    EXPECT_EQ(
        validate_prebuilt_bvh(exported.bvh, get_prebuilt_bvh_backend(), get_prebuilt_bvh_library(), exported.bvh.triangle_hash, mesh.triangles.size(), 64),
        std::string{}
    );

    Test_geometry candidate = make_uncommitted_triangle_geometry("candidate", mesh.vertices, mesh.triangles);
    Prebuilt_bvh  supplied  = exported.bvh;
    ASSERT_TRUE(candidate.geometry->set_prebuilt_bvh(std::move(supplied)));
    candidate.geometry->commit();
    EXPECT_EQ(candidate.geometry->get_bvh_source(), Bvh_source::prebuilt);
    EXPECT_EQ(candidate.geometry->get_prebuilt_bvh_rejection(), std::string_view{});

    Prebuilt_bvh round_trip;
    ASSERT_TRUE(candidate.geometry->export_bvh(round_trip));
    EXPECT_EQ(round_trip.backend,        exported.bvh.backend);
    EXPECT_EQ(round_trip.library,        exported.bvh.library);
    EXPECT_EQ(round_trip.quality,        exported.bvh.quality);
    EXPECT_EQ(round_trip.triangle_hash,  exported.bvh.triangle_hash);
    EXPECT_EQ(round_trip.triangle_count, exported.bvh.triangle_count);
    EXPECT_TRUE(nodes_identical(round_trip.nodes, exported.bvh.nodes));
    EXPECT_EQ(round_trip.primitive_ids, exported.bvh.primitive_ids);

    const std::size_t hit_count = expect_identical_hits(mesh, reference, candidate.geometry.get(), c_ray_count);
    EXPECT_GT(hit_count, c_ray_count / 4);

    // Commit again without supplying a BVH: the supplied one applied to one
    // commit only.
    candidate.geometry->commit();
    EXPECT_NE(candidate.geometry->get_bvh_source(), Bvh_source::prebuilt);
}

TEST(Prebuilt_bvh, RoundTripWavyGrid)
{
    run_round_trip(make_wavy_grid(40)); // 3200 triangles
}

TEST(Prebuilt_bvh, RoundTripSphereSoup)
{
    run_round_trip(make_sphere_soup(40, 48)); // 3744 triangles
}

enum class Node_kind : unsigned int { inner, leaf };

auto find_node(const Prebuilt_bvh& bvh, const Node_kind kind, const std::size_t first) -> std::size_t
{
    for (std::size_t n = first; n < bvh.nodes.size(); ++n) {
        const bool is_leaf = (bvh.nodes[n].index & 0xfu) != 0;
        if (is_leaf == (kind == Node_kind::leaf)) {
            return n;
        }
    }
    return bvh.nodes.size();
}

// Supplies a corrupted copy of the exported BVH; commit() must reject it,
// say why, and fall back to the normal path with identical hits.
void expect_rejected(const std::function<void(Prebuilt_bvh&)>& corrupt, const std::string_view reason_part)
{
    Exported exported;
    if (!make_exported(make_wavy_grid(24), exported)) {
        GTEST_SKIP() << "raytrace backend has no BVH export";
    }
    Prebuilt_bvh corrupted = exported.bvh;
    corrupt(corrupted);

    Test_geometry candidate = make_uncommitted_triangle_geometry("rejected", exported.mesh.vertices, exported.mesh.triangles);
    ASSERT_TRUE(candidate.geometry->set_prebuilt_bvh(std::move(corrupted)));
    candidate.geometry->commit();

    const Bvh_source source = candidate.geometry->get_bvh_source();
    EXPECT_TRUE((source == Bvh_source::built) || (source == Bvh_source::cache)) << c_str(source);
    const std::string reason{candidate.geometry->get_prebuilt_bvh_rejection()};
    EXPECT_NE(reason.find(reason_part), std::string::npos) << reason;
    EXPECT_EQ(candidate.geometry->get_triangle_count(), exported.mesh.triangles.size());

    const std::size_t hit_count = expect_identical_hits(exported.mesh, exported.reference.geometry.get(), candidate.geometry.get(), 100);
    EXPECT_GT(hit_count, 0u);
}

TEST(Prebuilt_bvh, RejectsWrongLibraryTag)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.library = "0000000000000000000000000000000000000000"; }, "library");
}

TEST(Prebuilt_bvh, RejectsWrongBackend)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.backend = "tinybvh"; }, "backend");
}

TEST(Prebuilt_bvh, RejectsWrongTriangleCount)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.triangle_count += 1; }, "triangle count");
}

TEST(Prebuilt_bvh, RejectsWrongHash)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.triangle_hash ^= 1u; }, "triangle hash");
}

TEST(Prebuilt_bvh, RejectsNoNodes)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.nodes.clear(); }, "no nodes");
}

TEST(Prebuilt_bvh, RejectsTruncatedNodes)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.nodes.pop_back(); }, "references children");
}

TEST(Prebuilt_bvh, RejectsTruncatedPrimitiveIds)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.primitive_ids.pop_back(); }, "primitive ids for");
}

TEST(Prebuilt_bvh, RejectsChildOutOfRange)
{
    expect_rejected(
        [](Prebuilt_bvh& bvh) {
            const std::size_t n = find_node(bvh, Node_kind::inner, 0);
            ASSERT_LT(n, bvh.nodes.size());
            bvh.nodes[n].index = static_cast<uint32_t>((bvh.nodes.size() - 1) << 4);
        },
        "references children"
    );
}

TEST(Prebuilt_bvh, RejectsPrimitiveSlotOutOfRange)
{
    expect_rejected(
        [](Prebuilt_bvh& bvh) {
            const std::size_t n = find_node(bvh, Node_kind::leaf, 0);
            ASSERT_LT(n, bvh.nodes.size());
            bvh.nodes[n].index = static_cast<uint32_t>((bvh.triangle_count << 4) | 1u);
        },
        "references slots"
    );
}

TEST(Prebuilt_bvh, RejectsNodeReferencedTwice)
{
    expect_rejected(
        [](Prebuilt_bvh& bvh) {
            // A second inner node pointing at the root's children
            const std::size_t n = find_node(bvh, Node_kind::inner, 1);
            ASSERT_LT(n, bvh.nodes.size());
            bvh.nodes[n].index = bvh.nodes[0].index;
        },
        "more than one inner node"
    );
}

TEST(Prebuilt_bvh, RejectsCycleThroughRoot)
{
    expect_rejected(
        [](Prebuilt_bvh& bvh) {
            const std::size_t n = find_node(bvh, Node_kind::inner, 1);
            ASSERT_LT(n, bvh.nodes.size());
            bvh.nodes[n].index = 0u; // children 0 and 1: back to the root
        },
        "references the root"
    );
}

TEST(Prebuilt_bvh, RejectsDuplicatePrimitiveId)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.primitive_ids[1] = bvh.primitive_ids[0]; }, "more than once");
}

TEST(Prebuilt_bvh, RejectsPrimitiveIdOutOfRange)
{
    expect_rejected([](Prebuilt_bvh& bvh) { bvh.primitive_ids[0] = static_cast<uint32_t>(bvh.triangle_count); }, "out of range");
}

TEST(Prebuilt_bvh, ValidatorRejectsUnreachableNodes)
{
    // A root leaf, then an inner node 1 whose children are 1 and 2: every
    // non-root node has exactly one inner parent, but a self-referencing
    // pair is not reachable from the root.
    Prebuilt_bvh bvh;
    bvh.backend        = "bvh_v2";
    bvh.library        = "tag";
    bvh.triangle_hash  = 7;
    bvh.triangle_count = 1;
    bvh.primitive_ids  = {0u};
    bvh.nodes.resize(3);
    bvh.nodes[0].index = (0u << 4) | 1u; // leaf, slot 0
    bvh.nodes[1].index = (1u << 4);      // inner, children 1 and 2
    bvh.nodes[2].index = (0u << 4) | 1u; // leaf
    const std::string reason = validate_prebuilt_bvh(bvh, "bvh_v2", "tag", 7, 1, 64);
    EXPECT_NE(reason.find("reachable"), std::string::npos) << reason;
}

TEST(Prebuilt_bvh, ValidatorRejectsTooDeep)
{
    // A chain: each inner node has an inner child and a leaf child
    const std::size_t depth = 70;
    Prebuilt_bvh bvh;
    bvh.backend        = "bvh_v2";
    bvh.library        = "tag";
    bvh.triangle_hash  = 7;
    bvh.triangle_count = 1;
    bvh.primitive_ids  = {0u};
    std::size_t inner = 0;
    bvh.nodes.resize(1);
    for (std::size_t level = 0; level < depth; ++level) {
        const std::size_t first = bvh.nodes.size();
        bvh.nodes.resize(first + 2);
        bvh.nodes[inner].index     = static_cast<uint32_t>(first << 4);
        bvh.nodes[first + 1].index = (0u << 4) | 1u;
        inner = first;
    }
    bvh.nodes[inner].index = (0u << 4) | 1u;
    EXPECT_EQ(validate_prebuilt_bvh(bvh, "bvh_v2", "tag", 7, 1, depth), std::string{});
    const std::string reason = validate_prebuilt_bvh(bvh, "bvh_v2", "tag", 7, 1, depth - 1);
    EXPECT_NE(reason.find("depth"), std::string::npos) << reason;
}

TEST(Prebuilt_bvh, UnsupportedBackendReportsNotAvailable)
{
    if (!get_prebuilt_bvh_backend().empty()) {
        GTEST_SKIP() << "raytrace backend supports prebuilt BVHs";
    }
    Test_geometry geometry = make_uncommitted_triangle_geometry("unsupported", {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, {{0, 1, 2}});
    EXPECT_FALSE(geometry.geometry->set_prebuilt_bvh(Prebuilt_bvh{}));
    geometry.geometry->commit();
    Prebuilt_bvh out;
    EXPECT_FALSE(geometry.geometry->export_bvh(out));
}

} // namespace
