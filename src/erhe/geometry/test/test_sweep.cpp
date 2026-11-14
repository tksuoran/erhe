#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/sweep.hpp"

#include <geogram/mesh/mesh.h>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

using erhe::geometry::shapes::Sweep_parameters;

[[nodiscard]] auto make_circle_profile(const int count, const float radius) -> std::vector<glm::vec2>
{
    std::vector<glm::vec2> profile;
    for (int i = 0; i < count; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
        profile.emplace_back(radius * std::cos(a), radius * std::sin(a));
    }
    return profile;
}

// Parameter sets covering the sweep's branches: cubic spine with taper, twist and morph,
// a vertical start (initial up falls back to +X), repeated control points (tangent
// finite difference), a straight spine (no transport rotation) and a degree 5 spine.
[[nodiscard]] auto make_cases() -> std::vector<Sweep_parameters>
{
    std::vector<Sweep_parameters> cases;
    {
        Sweep_parameters p;
        p.profile     = make_circle_profile(8, 0.25f);
        p.profile_end = make_circle_profile(8, 0.125f);
        p.spine       = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f}, {2.0f, -0.5f, 1.0f}, {3.0f, 1.0f, 2.0f}};
        p.spine_steps = 24;
        p.taper       = {{0.0f, 1.0f}, {0.6f, 0.8f}, {1.0f, 0.0f}};
        p.twist       = 1.3f;
        cases.push_back(p);
    }
    {
        Sweep_parameters p;
        p.profile     = {{-0.1f, -0.05f}, {0.1f, -0.05f}, {0.1f, 0.05f}, {-0.1f, 0.05f}};
        p.spine       = {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 2.0f, 0.2f}, {1.5f, 2.5f, -0.3f}, {2.0f, 1.0f, 0.0f}, {2.5f, 0.0f, 1.0f}};
        p.spine_steps = 31;
        p.twist       = -0.7f;
        cases.push_back(p);
    }
    {
        Sweep_parameters p;
        p.profile     = make_circle_profile(5, 0.1f);
        p.spine       = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
        p.spine_steps = 16;
        p.end_cap     = false;
        cases.push_back(p);
    }
    {
        Sweep_parameters p;
        p.profile     = make_circle_profile(6, 0.2f);
        p.spine       = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 4.0f}};
        p.spine_steps = 7;
        cases.push_back(p);
    }
    return cases;
}

TEST(Sweep, RingsFollowTheSpine)
{
    const std::vector<Sweep_parameters> cases = make_cases();
    for (const Sweep_parameters& parameters : cases) {
        erhe::geometry::Geometry geometry{"sweep"};
        GEO::Mesh&               mesh = geometry.get_mesh();
        erhe::geometry::shapes::make_sweep(mesh, parameters);
        const bool         tip   = !parameters.taper.empty() && (parameters.taper.back().y < 1.0e-4f);
        const GEO::index_t rings = static_cast<GEO::index_t>(parameters.spine_steps + (tip ? 0 : 1));
        EXPECT_EQ(mesh.vertices.nb(), (rings * static_cast<GEO::index_t>(parameters.profile.size())) + (tip ? 1u : 0u));
        // The first ring's centroid is the first spine point (profiles are centered).
        glm::vec3 centroid{0.0f};
        for (GEO::index_t j = 0; j < static_cast<GEO::index_t>(parameters.profile.size()); ++j) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, j);
            centroid += glm::vec3{p.x, p.y, p.z};
        }
        centroid /= static_cast<float>(parameters.profile.size());
        EXPECT_NEAR(glm::length(centroid - parameters.spine.front()), 0.0f, 1.0e-5f);
    }
}

// ERHE_SWEEP_DUMP=<path> writes every case's vertex positions as hexadecimal floats,
// for bit-exact before / after comparisons of the sweep.
TEST(Sweep, DumpVertexPositions)
{
    const char* const path = std::getenv("ERHE_SWEEP_DUMP");
    if (path == nullptr) {
        GTEST_SKIP() << "ERHE_SWEEP_DUMP not set";
    }
    std::ofstream file{path};
    const std::vector<Sweep_parameters> cases = make_cases();
    for (std::size_t c = 0; c < cases.size(); ++c) {
        erhe::geometry::Geometry geometry{"sweep"};
        GEO::Mesh&               mesh = geometry.get_mesh();
        erhe::geometry::shapes::make_sweep(mesh, cases[c]);
        file << fmt::format("case {} vertices {} facets {}\n", c, mesh.vertices.nb(), mesh.facets.nb());
        for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
            file << fmt::format("{:a} {:a} {:a}\n", p.x, p.y, p.z);
        }
    }
}

} // namespace
