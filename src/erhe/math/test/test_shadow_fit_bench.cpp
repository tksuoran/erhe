// Experiment harness for the directional shadow fit's light plane hull and
// rotating calipers stages (Light::tight_directional_light_projection_transforms,
// "fit: optimize rotation (calipers)"). Not a regression test: every test is
// skipped unless the environment variable ERHE_FIT_BENCH is set, because the
// numbers are only meaningful in a Release build and the runs take seconds.
//
// What it measures, on synthetic caster boxes:
//  - the baseline stage (corner generation + projection + sort based 2D hull +
//    O(h^2) calipers) split per sub-stage,
//  - interior-box pruning before the 2D hull (fixed direction polygon versus a
//    polygon seeded from the previous frame's hull participants), checking that
//    the resulting hull is bit-identical to the baseline hull,
//  - calipers variants (linear rotating calipers, SoA quadratic).

#include "erhe_math/math_util.hpp"
#include "shadow_fit_prune.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace fit_experiment;

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto bench_enabled() -> bool
{
    return std::getenv("ERHE_FIT_BENCH") != nullptr;
}

void log_line(const char* format, auto... args)
{
    if constexpr (sizeof...(args) == 0) {
        GTEST_LOG_(INFO) << format;
    } else {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        GTEST_LOG_(INFO) << buffer;
    }
}

enum class Layout : int { disc, clustered, flat_grid };

[[nodiscard]] auto layout_name(const Layout layout) -> const char*
{
    switch (layout) {
        case Layout::disc:      return "disc";
        case Layout::clustered: return "clustered";
        case Layout::flat_grid: return "flat_grid";
    }
    return "?";
}

[[nodiscard]] auto make_boxes(const Layout layout, const std::size_t count, const uint32_t seed) -> std::vector<Bench_box>
{
    std::mt19937 rng{seed};
    std::uniform_real_distribution<float> u01{0.0f, 1.0f};
    std::normal_distribution<float>       gauss{0.0f, 1.0f};
    std::vector<Bench_box> boxes;
    boxes.reserve(count);
    std::vector<glm::vec2> clusters;
    for (int i = 0; i < 20; ++i) {
        clusters.push_back(glm::vec2{(u01(rng) - 0.5f) * 200.0f, (u01(rng) - 0.5f) * 200.0f});
    }
    const std::size_t grid = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<float>(count))));
    for (std::size_t i = 0; i < count; ++i) {
        glm::vec3 center{0.0f};
        glm::vec3 extent{0.2f + 1.5f * u01(rng), 0.2f + 1.5f * u01(rng), 0.2f + 1.5f * u01(rng)};
        switch (layout) {
            case Layout::disc: {
                const float r = 100.0f * std::sqrt(u01(rng));
                const float a = 2.0f * std::numbers::pi_v<float> * u01(rng);
                center = glm::vec3{r * std::cos(a), 10.0f * u01(rng), r * std::sin(a)};
                break;
            }
            case Layout::clustered: {
                const glm::vec2 c = clusters[i % clusters.size()];
                center = glm::vec3{c.x + 8.0f * gauss(rng), 5.0f * u01(rng), c.y + 8.0f * gauss(rng)};
                break;
            }
            case Layout::flat_grid: {
                const float gx = static_cast<float>(i % grid) - 0.5f * static_cast<float>(grid);
                const float gz = static_cast<float>(i / grid) - 0.5f * static_cast<float>(grid);
                center = glm::vec3{gx * 3.0f, extent.y, gz * 3.0f};
                break;
            }
        }
        boxes.push_back(Bench_box{center - extent, center + extent});
    }
    return boxes;
}

// Median of several timed repetitions, microseconds.
template <typename F>
[[nodiscard]] auto time_us(F&& f, const int repetitions = 15) -> double
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    for (int i = 0; i < repetitions; ++i) {
        const Clock::time_point t0 = Clock::now();
        f();
        const Clock::time_point t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// Keeps results alive so the optimizer cannot drop the measured work.
volatile float g_sink = 0.0f;

// ---- Baseline stage, as in light_frustum_fit.cpp ---------------------------

void baseline_corners(const std::vector<Bench_box>& boxes, std::vector<glm::vec3>& world)
{
    world.clear();
    for (const Bench_box& b : boxes) {
        for (std::size_t i = 0; i < 8; ++i) {
            world.push_back(
                glm::vec3{
                    ((i & 4u) != 0u) ? b.max.x : b.min.x,
                    ((i & 2u) != 0u) ? b.max.y : b.min.y,
                    ((i & 1u) != 0u) ? b.max.z : b.min.z
                }
            );
        }
    }
}

void baseline_project(const std::vector<glm::vec3>& world, const glm::mat3& light_from_world, std::vector<glm::vec2>& projected)
{
    projected.clear();
    projected.reserve(world.size());
    for (const glm::vec3& p : world) {
        projected.push_back(glm::vec2{light_from_world * p});
    }
}

// ---- Calipers variants ------------------------------------------------------

class Obb_result
{
public:
    std::size_t edge{0};
    float       area{std::numeric_limits<float>::max()};
    glm::vec2   min{0.0f};
    glm::vec2   max{0.0f};
};

// Structure-of-arrays quadratic, no matrix, no per-edge sqrt.
[[nodiscard]] auto calipers_soa_quadratic(const std::vector<float>& x, const std::vector<float>& y) -> Obb_result
{
    Obb_result result{};
    const std::size_t n = x.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j  = (i + 1) % n;
        const float       dx = x[j] - x[i];
        const float       dy = y[j] - y[i];
        const float       l2 = dx * dx + dy * dy;
        if (l2 < 1e-12f) {
            continue;
        }
        float umin = std::numeric_limits<float>::max();
        float umax = std::numeric_limits<float>::lowest();
        float vmin = std::numeric_limits<float>::max();
        float vmax = std::numeric_limits<float>::lowest();
        for (std::size_t k = 0; k < n; ++k) {
            const float u = dx * x[k] + dy * y[k];
            const float v = dx * y[k] - dy * x[k];
            umin = std::min(umin, u);
            umax = std::max(umax, u);
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        const float area = ((umax - umin) * (vmax - vmin)) / l2;
        if (area < result.area) {
            const float inv = 1.0f / std::sqrt(l2);
            result.edge = i;
            result.area = area;
            result.min  = glm::vec2{umin * inv, vmin * inv};
            result.max  = glm::vec2{umax * inv, vmax * inv};
        }
    }
    return result;
}

// Linear time rotating calipers over a CCW strictly convex polygon.
[[nodiscard]] auto calipers_linear(const std::vector<float>& x, const std::vector<float>& y) -> Obb_result
{
    Obb_result result{};
    const std::size_t n = x.size();
    if (n < 3) {
        return result;
    }
    const auto X = [&](const std::size_t i) -> float { return x[i % n]; };
    const auto Y = [&](const std::size_t i) -> float { return y[i % n]; };

    std::size_t j = 1; // max along the edge direction
    std::size_t k = 1; // max along the edge normal (farthest from the edge)
    std::size_t l = 1; // min along the edge direction
    for (std::size_t i = 0; i < n; ++i) {
        const float dx = X(i + 1) - X(i);
        const float dy = Y(i + 1) - Y(i);
        const float l2 = dx * dx + dy * dy;
        if (l2 < 1e-12f) {
            continue;
        }
        // u = (dx, dy), v = (-dy, dx) (left normal; interior side of a CCW polygon)
        const auto du = [&](const std::size_t a) -> float { return dx * (X(a + 1) - X(a)) + dy * (Y(a + 1) - Y(a)); };
        const auto dv = [&](const std::size_t a) -> float { return -dy * (X(a + 1) - X(a)) + dx * (Y(a + 1) - Y(a)); };
        j = std::max(j, i + 1);
        for (std::size_t guard = 0; (guard < n) && (du(j) > 0.0f); ++guard) {
            ++j;
        }
        k = std::max(k, j);
        for (std::size_t guard = 0; (guard < n) && (dv(k) > 0.0f); ++guard) {
            ++k;
        }
        l = std::max(l, k);
        for (std::size_t guard = 0; (guard < n) && (du(l) < 0.0f); ++guard) {
            ++l;
        }
        const float umax = dx * X(j) + dy * Y(j);
        const float umin = dx * X(l) + dy * Y(l);
        const float vmax = -dy * X(k) + dx * Y(k);
        const float vmin = -dy * X(i) + dx * Y(i);
        const float area = ((umax - umin) * (vmax - vmin)) / l2;
        if (area < result.area) {
            const float inv = 1.0f / std::sqrt(l2);
            result.edge = i;
            result.area = area;
            result.min  = glm::vec2{umin * inv, vmin * inv};
            result.max  = glm::vec2{umax * inv, vmax * inv};
        }
    }
    return result;
}

[[nodiscard]] auto regular_polygon(const std::size_t count, const float radius, const uint32_t seed) -> std::vector<glm::vec2>
{
    std::mt19937 rng{seed};
    std::uniform_real_distribution<float> jitter{0.9f, 1.0f};
    std::vector<glm::vec2> raw;
    for (std::size_t i = 0; i < count * 3; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(count * 3);
        raw.push_back(radius * jitter(rng) * glm::vec2{std::cos(a), std::sin(a)});
    }
    std::vector<glm::vec2> hull;
    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{raw}, hull);
    return hull;
}

} // anonymous namespace

TEST(Shadow_fit_bench, calipers_variants)
{
    if (!bench_enabled()) {
        GTEST_SKIP() << "set ERHE_FIT_BENCH=1 to run the shadow fit experiments";
    }
    log_line("calipers: hull size | baseline us | soa quadratic us | linear us | edge mismatches (area-rel >1e-4) over 200 hulls");
    for (const std::size_t target : {8u, 16u, 32u, 64u, 128u, 256u, 512u}) {
        std::vector<std::vector<glm::vec2>> hulls;
        for (uint32_t seed = 0; seed < 200; ++seed) {
            hulls.push_back(regular_polygon(target, 100.0f, seed));
        }
        std::size_t mismatches = 0;
        std::size_t total_vertices = 0;
        for (const std::vector<glm::vec2>& hull : hulls) {
            total_vertices += hull.size();
            std::vector<float> x, y;
            for (const glm::vec2& p : hull) { x.push_back(p.x); y.push_back(p.y); }
            const erhe::math::Min_area_obb_2d base = erhe::math::calculate_min_area_obb_2d(hull);
            const Obb_result lin  = calipers_linear(x, y);
            const Obb_result soa  = calipers_soa_quadratic(x, y);
            const float rel_lin = std::abs(lin.area - base.area) / std::max(base.area, 1e-6f);
            const float rel_soa = std::abs(soa.area - base.area) / std::max(base.area, 1e-6f);
            if ((rel_lin > 1e-4f) || (rel_soa > 1e-4f)) {
                ++mismatches;
            }
        }
        const double base_us = time_us([&]() {
            float s = 0.0f;
            for (const std::vector<glm::vec2>& hull : hulls) { s += erhe::math::calculate_min_area_obb_2d(hull).area; }
            g_sink = s;
        });
        std::vector<std::vector<float>> xs(hulls.size()), ys(hulls.size());
        for (std::size_t i = 0; i < hulls.size(); ++i) {
            for (const glm::vec2& p : hulls[i]) { xs[i].push_back(p.x); ys[i].push_back(p.y); }
        }
        const double soa_us = time_us([&]() {
            float s = 0.0f;
            for (std::size_t i = 0; i < hulls.size(); ++i) { s += calipers_soa_quadratic(xs[i], ys[i]).area; }
            g_sink = s;
        });
        const double lin_us = time_us([&]() {
            float s = 0.0f;
            for (std::size_t i = 0; i < hulls.size(); ++i) { s += calipers_linear(xs[i], ys[i]).area; }
            g_sink = s;
        });
        const double per = 1.0 / static_cast<double>(hulls.size());
        log_line("  h~%4zu (avg %6.1f) | %9.3f | %9.3f | %9.3f | %zu", target, static_cast<double>(total_vertices) * per, base_us * per, soa_us * per, lin_us * per, mismatches);
        EXPECT_EQ(mismatches, 0u);
    }
}


namespace {

class Variant
{
public:
    const char* name;
    std::size_t directions;   // fixed extreme directions (0 = none)
    std::size_t seed_vertices;// seed polygon vertices from previous participants (0 = none)
    bool        point_prune;  // drop survivor corners inside the inner polygon before the sort
    bool        vec_extremes; // lane-wise vectorizable extreme search
};

class Frame_stats
{
public:
    double gen_us{0}, seed_us{0}, prune_us{0}, survivors_us{0}, hull_us{0}, calipers_us{0};
    double prune_ratio{0}, point_ratio{0}, overlap{0};
    std::size_t mismatches{0}, hull_size{0};
};

[[nodiscard]] auto corner(const Bench_box& b, const std::size_t c) -> glm::vec3
{
    return glm::vec3{
        ((c & 4u) != 0u) ? b.max.x : b.min.x,
        ((c & 2u) != 0u) ? b.max.y : b.min.y,
        ((c & 1u) != 0u) ? b.max.z : b.min.z
    };
}

[[nodiscard]] auto micro(const Clock::time_point a, const Clock::time_point b) -> double
{
    return std::chrono::duration<double, std::micro>(b - a).count();
}

} // anonymous namespace

TEST(Shadow_fit_bench, hull_stage)
{
    if (!bench_enabled()) {
        GTEST_SKIP() << "set ERHE_FIT_BENCH=1 to run the shadow fit experiments";
    }
    class Scenario { public: const char* name; float churn; float light_step; };
    const Scenario scenarios[] = {
        {"static",         0.00f, 0.0f},
        {"camera_pan",     0.03f, 0.0f},
        {"sun_moving",     0.00f, 0.004f},
    };
    const Variant variants[] = {
        {"baseline",                 0,  0, false, false},
        {"K8 scalar",                8,  0, false, false},
        {"K8 vec",                   8,  0, false, true },
        {"K8 vec +points",           8,  0, true,  true },
        {"K16 vec +points",          16, 0, true,  true },
        {"seed16",                   0,  16, false, true },
        {"seed32 +points",           0,  32, true,  true },
        {"seed32+K4 +points",        4,  32, true,  true },
    };
    constexpr int variant_count = sizeof(variants) / sizeof(variants[0]);
    log_line("hull stage per frame (microseconds). pruned=boxes dropped before the sort, pts=corner points left for the sort, h=hull size");
    log_line("layout       boxes scenario   | variant            |   gen  seed prune  surv  hull  cali  total | pruned   pts    h mism");

    for (const Layout layout : {Layout::disc, Layout::clustered, Layout::flat_grid}) {
        for (const std::size_t count : {2000u, 8000u}) {
            for (const Scenario& scenario : scenarios) {
                const std::vector<Bench_box> all = make_boxes(layout, count, 1234u);
                std::mt19937 rng{99u};
                std::vector<uint8_t> alive(all.size(), 1);
                for (std::size_t i = 0; i < all.size(); ++i) {
                    alive[i] = ((rng() % 10u) < 7u) ? 1 : 0;
                }
                constexpr int frame_count = 40;
                Frame_stats totals[variant_count];
                std::vector<uint32_t> previous[variant_count]; // participant ids per variant
                float light_angle = 0.6f;
                std::vector<Bench_box> boxes;
                std::vector<uint32_t>  ids;
                std::vector<uint32_t>  id_to_index(all.size(), 0xffffffffu);
                std::vector<glm::vec3> world;
                std::vector<glm::vec2> projected, base_hull, hull, survivor_points;
                std::vector<uint8_t>   inside;
                Footprints footprints;

                for (int frame = 0; frame < frame_count; ++frame) {
                    const std::size_t swaps = static_cast<std::size_t>(scenario.churn * static_cast<float>(all.size()));
                    for (std::size_t sw = 0; sw < swaps; ++sw) {
                        for (int attempt = 0; attempt < 50; ++attempt) {
                            const std::size_t a = rng() % all.size();
                            const std::size_t b = rng() % all.size();
                            if ((alive[a] != 0) && (alive[b] == 0)) { alive[a] = 0; alive[b] = 1; break; }
                        }
                    }
                    light_angle += scenario.light_step;
                    const Light_basis basis{glm::vec3{0.3f * std::cos(light_angle), 0.8f, 0.5f * std::sin(light_angle) + 0.3f}};
                    boxes.clear(); ids.clear();
                    std::fill(id_to_index.begin(), id_to_index.end(), 0xffffffffu);
                    for (std::size_t i = 0; i < all.size(); ++i) {
                        if (alive[i] != 0) {
                            id_to_index[i] = static_cast<uint32_t>(boxes.size());
                            boxes.push_back(all[i]);
                            ids.push_back(static_cast<uint32_t>(i));
                        }
                    }

                    // reference hull (variant 0) and its participant ids
                    std::vector<uint32_t> base_owner_ids;
                    {
                        Frame_stats& st = totals[0];
                        const Clock::time_point t0 = Clock::now();
                        baseline_corners(boxes, world);
                        baseline_project(world, basis.light_from_world, projected);
                        const Clock::time_point t1 = Clock::now();
                        erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{projected}, base_hull);
                        const Clock::time_point t2 = Clock::now();
                        const erhe::math::Min_area_obb_2d obb = erhe::math::calculate_min_area_obb_2d(base_hull);
                        const Clock::time_point t3 = Clock::now();
                        g_sink = obb.area;
                        st.gen_us += micro(t0, t1); st.hull_us += micro(t1, t2); st.calipers_us += micro(t2, t3);
                        st.hull_size += base_hull.size();
                        st.point_ratio += 1.0;
                        for (const glm::vec2& hv : base_hull) {
                            for (std::size_t p = 0; p < projected.size(); ++p) {
                                if ((projected[p].x == hv.x) && (projected[p].y == hv.y)) { base_owner_ids.push_back(ids[p / 8]); break; }
                            }
                        }
                        std::sort(base_owner_ids.begin(), base_owner_ids.end());
                        base_owner_ids.erase(std::unique(base_owner_ids.begin(), base_owner_ids.end()), base_owner_ids.end());
                        previous[0] = base_owner_ids;
                    }

                    for (int v = 1; v < variant_count; ++v) {
                        const Variant& var = variants[v];
                        Frame_stats& st = totals[v];
                        const Clock::time_point t0 = Clock::now();
                        footprints.build(boxes, basis);
                        const Clock::time_point t1 = Clock::now();

                        // inner polygon from real points: fixed direction extremes + previous participants
                        std::vector<glm::vec2> inner_points;
                        if (var.directions > 0) {
                            if (var.vec_extremes) {
                                extreme_corners_vec(footprints, var.directions, inner_points);
                            } else {
                                Inner_polygon tmp;
                                extreme_polygon(footprints, var.directions, tmp); // scalar path; polygon rebuilt below from the same extremes
                                // recompute extremes via the vector routine's result for the union (same points)
                                extreme_corners_vec(footprints, var.directions, inner_points);
                            }
                        }
                        if (var.seed_vertices > 0) {
                            for (const uint32_t id : previous[v]) {
                                const uint32_t index = id_to_index[id];
                                if (index == 0xffffffffu) { continue; }
                                for (std::size_t c = 0; c < 8; ++c) {
                                    inner_points.push_back(glm::vec2{basis.light_from_world * corner(boxes[index], c)});
                                }
                            }
                        }
                        Inner_polygon poly;
                        std::vector<glm::vec2> inner_hull;
                        erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{inner_points}, inner_hull);
                        poly.build(inner_hull, (var.seed_vertices > 0) ? var.seed_vertices : 64u);
                        const Clock::time_point t2 = Clock::now();

                        std::size_t pruned = 0;
                        if (poly.valid()) {
                            pruned = prune_edge_outer(footprints, poly, inside);
                        } else {
                            inside.assign(boxes.size(), 0);
                        }
                        const Clock::time_point t3 = Clock::now();

                        survivor_points.clear();
                        std::vector<uint32_t> survivor_ids;
                        std::vector<uint32_t> point_owner;
                        for (std::size_t i = 0; i < boxes.size(); ++i) {
                            if (inside[i] != 0) { continue; }
                            for (std::size_t c = 0; c < 8; ++c) {
                                const glm::vec2 q{basis.light_from_world * corner(boxes[i], c)};
                                if (var.point_prune && poly.valid()) {
                                    bool in = true;
                                    for (std::size_t e = 0; e < poly.nx.size(); ++e) {
                                        if ((poly.nx[e] * q.x + poly.ny[e] * q.y) < poly.d[e]) { in = false; break; }
                                    }
                                    if (in) { continue; }
                                }
                                survivor_points.push_back(q);
                                point_owner.push_back(ids[i]);
                            }
                        }
                        const Clock::time_point t4 = Clock::now();
                        erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{survivor_points}, hull);
                        const Clock::time_point t5 = Clock::now();
                        const erhe::math::Min_area_obb_2d obb = erhe::math::calculate_min_area_obb_2d(hull);
                        const Clock::time_point t6 = Clock::now();
                        g_sink = obb.area;

                        st.gen_us += micro(t0, t1); st.seed_us += micro(t1, t2); st.prune_us += micro(t2, t3);
                        st.survivors_us += micro(t3, t4); st.hull_us += micro(t4, t5); st.calipers_us += micro(t5, t6);
                        st.prune_ratio += static_cast<double>(pruned) / static_cast<double>(boxes.size());
                        st.point_ratio += static_cast<double>(survivor_points.size()) / static_cast<double>(boxes.size() * 8);
                        st.hull_size += hull.size();
                        const bool equal = (hull.size() == base_hull.size()) &&
                            std::equal(hull.begin(), hull.end(), base_hull.begin(), [](const glm::vec2& a, const glm::vec2& b) { return (a.x == b.x) && (a.y == b.y); });
                        if (!equal) { ++st.mismatches; }

                        // participants for the next frame
                        std::vector<uint32_t> owners;
                        for (const glm::vec2& hv : hull) {
                            for (std::size_t p = 0; p < survivor_points.size(); ++p) {
                                if ((survivor_points[p].x == hv.x) && (survivor_points[p].y == hv.y)) { owners.push_back(point_owner[p]); break; }
                            }
                        }
                        std::sort(owners.begin(), owners.end());
                        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
                        if (var.seed_vertices > 0) {
                            std::size_t shared = 0;
                            for (const uint32_t id : base_owner_ids) {
                                if (std::binary_search(previous[v].begin(), previous[v].end(), id)) { ++shared; }
                            }
                            st.overlap += base_owner_ids.empty() ? 1.0 : static_cast<double>(shared) / static_cast<double>(base_owner_ids.size());
                        }
                        previous[v] = owners;
                    }
                }

                const double inv = 1.0 / static_cast<double>(frame_count);
                for (int v = 0; v < variant_count; ++v) {
                    const Frame_stats& s = totals[v];
                    const double total = (s.gen_us + s.seed_us + s.prune_us + s.survivors_us + s.hull_us + s.calipers_us) * inv;
                    log_line(
                        "%-10s %6zu %-11s | %-18s | %5.0f %5.0f %5.0f %5.0f %5.0f %5.0f %6.0f | %5.1f%% %5.1f%% %4.0f %zu",
                        layout_name(layout), count, scenario.name, variants[v].name,
                        s.gen_us * inv, s.seed_us * inv, s.prune_us * inv, s.survivors_us * inv, s.hull_us * inv, s.calipers_us * inv, total,
                        s.prune_ratio * inv * 100.0, s.point_ratio * inv * 100.0, static_cast<double>(s.hull_size) * inv, s.mismatches
                    );
                    EXPECT_EQ(s.mismatches, 0u);
                }
            }
        }
    }
}
