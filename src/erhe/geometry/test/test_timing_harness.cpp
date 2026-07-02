// Permanent per-phase timing harness for the Catmull-Clark / geometry
// operation performance work (doc/catmull_clark.md). DISABLED_ so the
// regular test run stays fast; run it explicitly with:
//
//   erhe_geometry_tests --gtest_also_run_disabled_tests --gtest_filter=*TimingHarness*
//
// Build it in both configurations from the multi-config test tree produced
// by scripts\configure_tests.bat (no ASAN, no profiler):
//
//   cmake --build build_tests --target erhe_geometry_tests --config Debug
//   cmake --build build_tests --target erhe_geometry_tests --config Release
//
// Phase columns come from the Scoped_phase_timer markers in
// catmull_clark_subdivision.cpp (cc_*) and Geometry_operation::post_processing
// (interpolate / sanitize / process); "other" is the remainder of the
// iteration total not covered by any marker.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/operation_timing.hpp"
#include "erhe_geometry/operation/post_processing.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

namespace {

auto make_processed_cube() -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geometry = std::make_unique<erhe::geometry::Geometry>("cube");
    erhe::geometry::shapes::make_cube(geometry->get_mesh(), 1.0f);
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geometry->process({.flags = flags});
    return geometry;
}

// 7 iterations from a 6-facet cube; the last iteration subdivides a
// 24576-facet input into 98304 facets, matching the editor's
// "subdivide x6 on the default 24-facet box" stress case.
void run_catmull_clark_chain(const bool structural_intermediates)
{
    constexpr int iteration_count = 7;

    const char* const phase_names[] = {
        "cc_classify",
        "cc_initial_points",
        "cc_edge_midpoints",
        "cc_facet_centroids",
        "cc_quads",
        "interpolate",
        "sanitize",
        "process",
    };

    fmt::print("\n{:>10} {:>10}", "src_facets", "src_verts");
    for (const char* phase_name : phase_names) {
        fmt::print(" {:>18}", phase_name);
    }
    fmt::print(" {:>10} {:>10}\n", "other", "total");

    double chain_total_ms = 0.0;
    std::unique_ptr<erhe::geometry::Geometry> current = make_processed_cube();
    for (int i = 0; i < iteration_count; ++i) {
        const GEO::index_t src_facet_count  = current->get_mesh().facets.nb();
        const GEO::index_t src_vertex_count = current->get_mesh().vertices.nb();

        const erhe::geometry::operation::Post_processing post_processing_level =
            (structural_intermediates && ((i + 1) < iteration_count))
                ? erhe::geometry::operation::Post_processing::structural_only
                : erhe::geometry::operation::Post_processing::full_default;

        erhe::geometry::operation::Operation_timing timing;
        erhe::geometry::operation::Operation_timing* const previous = erhe::geometry::operation::Operation_timing::install(&timing);

        std::unique_ptr<erhe::geometry::Geometry> next = std::make_unique<erhe::geometry::Geometry>("subdivided");
        const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
        erhe::geometry::operation::catmull_clark_subdivision(*current.get(), *next.get(), nullptr, nullptr, post_processing_level);
        const std::chrono::steady_clock::time_point end_time = std::chrono::steady_clock::now();

        erhe::geometry::operation::Operation_timing::install(previous);

        const double total_ms = std::chrono::duration<double, std::milli>{end_time - start_time}.count();
        chain_total_ms += total_ms;
        double accounted_ms = 0.0;
        fmt::print("{:>10} {:>10}", src_facet_count, src_vertex_count);
        for (const char* phase_name : phase_names) {
            const double phase_ms = timing.get_milliseconds(phase_name);
            accounted_ms += phase_ms;
            fmt::print(" {:>18.1f}", phase_ms);
        }
        fmt::print(" {:>10.1f} {:>10.1f}\n", total_ms - accounted_ms, total_ms);

        // Whole-mesh Catmull-Clark turns every corner of every facet into one
        // quad; the source is all-quads at every level here, so 4x growth.
        const GEO::index_t expected_facet_count = 4 * src_facet_count;
        EXPECT_EQ(next->get_mesh().facets.nb(), expected_facet_count);

        current = std::move(next);
    }
    fmt::print("chain total: {:.1f} ms\n", chain_total_ms);
}

} // anonymous namespace

// Every iteration runs the full default post-processing (the pre-item-11
// behavior; kept for comparison).
TEST(TimingHarness, DISABLED_CatmullClarkChain)
{
    run_catmull_clark_chain(false);
}

// Intermediate iterations run structural-only post-processing, the way the
// editor's subdivide node runs the chain (doc/catmull_clark.md item 11).
TEST(TimingHarness, DISABLED_CatmullClarkChainStructuralIntermediates)
{
    run_catmull_clark_chain(true);
}
