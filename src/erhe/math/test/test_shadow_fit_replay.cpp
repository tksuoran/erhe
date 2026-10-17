// Replays captured shadow fit inputs (camera frustum, receiver and caster
// world AABBs, light direction) through the same erhe::math stages the
// directional fit runs, to time each stage on realistic data and to compare
// the interior box pruning experiment against the baseline stage.
//
// Skipped unless ERHE_FIT_CAPTURE_DIR points at a directory with capture
// files (heavy_<scenario>.txt, written by the capture script described in
// doc/erhe/shadow_tight_fit.md). Floats in the files are hex floats.

#include "erhe_math/aabb.hpp"
#include "erhe_math/math_util.hpp"
#include "shadow_fit_prune.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace fit_experiment;

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto micro(const Clock::time_point a, const Clock::time_point b) -> double
{
    return std::chrono::duration<double, std::micro>(b - a).count();
}

void log_line(const char* format, auto... args)
{
    if constexpr (sizeof...(args) == 0) {
        GTEST_LOG_(INFO) << format;
    } else {
        char buffer[640];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        GTEST_LOG_(INFO) << buffer;
    }
}

volatile float g_sink = 0.0f;

class Replay_light
{
public:
    glm::vec3   direction{0.0f, 1.0f, 0.0f};
    std::string flags; // recorded verdict per caster: '1' survives both culls
};

class Replay_frame
{
public:
    float                       shadow_range{0.0f};
    std::array<glm::vec3, 8>    corners{};
    std::array<glm::vec4, 6>    planes{};
    std::vector<erhe::math::Aabb> receivers;
    std::vector<uint8_t>        receiver_in_frustum;
    std::vector<erhe::math::Aabb> casters;
    std::vector<Replay_light>   lights;
};

// Parses n whitespace separated floats (hex float syntax accepted by strtof).
auto parse_floats(const char*& cursor, float* out, const std::size_t n) -> bool
{
    for (std::size_t i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(cursor, &end);
        if (end == cursor) {
            return false;
        }
        cursor = end;
    }
    return true;
}

[[nodiscard]] auto load_frames(const std::string& path) -> std::vector<Replay_frame>
{
    std::vector<Replay_frame> frames;
    std::ifstream stream{path};
    std::string line;
    Replay_frame* frame = nullptr;
    while (std::getline(stream, line)) {
        const char* cursor = line.c_str();
        if (line.rfind("frame ", 0) == 0) {
            frames.emplace_back();
            frame = &frames.back();
        } else if (line.rfind("shadow_range ", 0) == 0) {
            cursor += 13;
            parse_floats(cursor, &frame->shadow_range, 1);
        } else if (line.rfind("corners ", 0) == 0) {
            cursor += 8;
            float v[24];
            parse_floats(cursor, v, 24);
            for (std::size_t i = 0; i < 8; ++i) {
                frame->corners[i] = glm::vec3{v[3 * i], v[3 * i + 1], v[3 * i + 2]};
            }
        } else if (line.rfind("planes ", 0) == 0) {
            cursor += 7;
            float v[24];
            parse_floats(cursor, v, 24);
            for (std::size_t i = 0; i < 6; ++i) {
                frame->planes[i] = glm::vec4{v[4 * i], v[4 * i + 1], v[4 * i + 2], v[4 * i + 3]};
            }
        } else if (line.rfind("receivers ", 0) == 0) {
            const std::size_t count = static_cast<std::size_t>(std::atoi(line.c_str() + 10));
            for (std::size_t i = 0; i < count; ++i) {
                std::getline(stream, line);
                const char* c = line.c_str();
                float v[7];
                parse_floats(c, v, 7);
                frame->receivers.push_back(erhe::math::Aabb{glm::vec3{v[0], v[1], v[2]}, glm::vec3{v[3], v[4], v[5]}});
                frame->receiver_in_frustum.push_back(v[6] != 0.0f ? 1 : 0);
            }
        } else if (line.rfind("casters ", 0) == 0) {
            const std::size_t count = static_cast<std::size_t>(std::atoi(line.c_str() + 8));
            for (std::size_t i = 0; i < count; ++i) {
                std::getline(stream, line);
                const char* c = line.c_str();
                float v[6];
                parse_floats(c, v, 6);
                frame->casters.push_back(erhe::math::Aabb{glm::vec3{v[0], v[1], v[2]}, glm::vec3{v[3], v[4], v[5]}});
            }
        } else if (line.rfind("light ", 0) == 0) {
            cursor += 6;
            float v[3];
            parse_floats(cursor, v, 3);
            Replay_light light{};
            light.direction = glm::vec3{v[0], v[1], v[2]};
            while ((*cursor == ' ') || (*cursor == '\t')) {
                ++cursor;
            }
            light.flags = cursor;
            frame->lights.push_back(light);
        }
    }
    return frames;
}

[[nodiscard]] auto box_corner(const erhe::math::Aabb& b, const std::size_t i) -> glm::vec3
{
    return glm::vec3{
        ((i & 4u) != 0u) ? b.max.x : b.min.x,
        ((i & 2u) != 0u) ? b.max.y : b.min.y,
        ((i & 1u) != 0u) ? b.max.z : b.min.z
    };
}

class Stage_sums
{
public:
    // shared per frame
    double rcv_gather{0}, rcv_hull{0}, rcv_clip{0};
    std::size_t rcv_hull_runs{0}, rcv_clip_runs{0}, frames{0};
    // per light fit
    double rcv_planes{0}, volume_planes{0}, filter{0}, clip{0};
    double cal_base{0}, fit_box_base{0};
    double var_k16{0}, var_seed{0};          // pruned stage totals incl. calipers
    double var_k16_extents{0};               // cheap per box extents replacing fit_box
    std::size_t fits{0}, survivors{0}, interior{0}, boundary{0}, recorded_agree{0}, recorded_total{0};
    double prune_k16{0}, prune_seed{0}, points_k16{0}, points_seed{0};
    std::size_t hull_size{0}, mismatches_k16{0}, mismatches_seed{0}, extent_mismatch{0};
    double base_points{0};
    // persistent receiver hull classification experiment
    std::size_t cs_changed{0}, cs_reusable{0}, cs_hull_same{0}, cs_reuse_wrong{0};
    std::size_t cs_added{0}, cs_removed{0}, cs_removed_vertex{0}, cs_added_outside{0};
};

} // anonymous namespace

TEST(Shadow_fit_replay, stages)
{
    const char* dir_env = std::getenv("ERHE_FIT_CAPTURE_DIR");
    if (dir_env == nullptr) {
        GTEST_SKIP() << "set ERHE_FIT_CAPTURE_DIR to a directory with heavy_<scenario>.txt capture files";
    }
    const std::string replay_dir{dir_env};
    log_line("replay of captured fit inputs; microseconds per stage, mean over frames (receiver stage per frame, others per light fit)");

    for (const char* scenario : {"static", "orbit", "pan", "drag", "dragmid", "sun"}) {
        const std::vector<Replay_frame> frames = load_frames(replay_dir + "/heavy_" + scenario + ".txt");
        if (frames.empty()) {
            log_line("%s: no frames", scenario);
            continue;
        }
        Stage_sums sums;

        // receiver cache state across frames
        std::vector<glm::vec3> prev_points, gathered;
        std::array<glm::vec3, 8> prev_corners{};
        bool has_result = false;
        erhe::math::Convex_hull receiver_hull, clipped_hull;
        std::vector<glm::vec3> clipped;
        bool hull_valid = false;

        std::vector<std::vector<uint32_t>> prev_participants(8);
        std::vector<std::size_t> prev_caster_count(8, 0);

        erhe::math::Convex_hull box_hull;
        static constexpr std::array<std::array<std::size_t, 3>, 12> box_triangles{{
            {0, 1, 3}, {0, 3, 2}, {4, 5, 7}, {4, 7, 6}, {0, 1, 5}, {0, 5, 4},
            {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 3, 7}, {1, 7, 5}
        }};
        box_hull.triangle_indices.assign(box_triangles.begin(), box_triangles.end());

        Footprints footprints;
        std::vector<uint8_t> inside;

        for (const Replay_frame& frame : frames) {
            ++sums.frames;

            // ---- receiver cache stage (shared by all lights of the pass) ----
            {
                const Clock::time_point t0 = Clock::now();
                gathered.clear();
                for (const erhe::math::Aabb& aabb : frame.receivers) {
                    if (!aabb.is_valid_3d() || !erhe::math::aabb_in_frustum(frame.planes, frame.corners, aabb)) {
                        continue;
                    }
                    for (std::size_t i = 0; i < 8; ++i) {
                        gathered.push_back(box_corner(aabb, i));
                    }
                }
                const Clock::time_point t1 = Clock::now();
                sums.rcv_gather += micro(t0, t1);
                const bool same_corner_set = has_result && (gathered == prev_points);
                if (!(same_corner_set && (frame.corners == prev_corners))) {
                    // ---- experiment: could the previous hull be reused? ----
                    // Sufficient condition: every corner that left the set was not a vertex of the
                    // previous hull and every corner that entered lies inside it.
                    bool condition_valid = false;
                    bool reusable = false;
                    std::vector<glm::vec3> old_hull_vertices;
                    if (has_result && !same_corner_set && !receiver_hull.triangle_indices.empty()) {
                        condition_valid = true;
                        ++sums.cs_changed;
                        old_hull_vertices = receiver_hull.points;
                        const auto lex = [](const glm::vec3& a, const glm::vec3& b) { return (a.x != b.x) ? (a.x < b.x) : ((a.y != b.y) ? (a.y < b.y) : (a.z < b.z)); };
                        std::vector<glm::vec3> old_sorted = prev_points, new_sorted = gathered, hull_sorted = old_hull_vertices;
                        std::sort(old_sorted.begin(), old_sorted.end(), lex);
                        std::sort(new_sorted.begin(), new_sorted.end(), lex);
                        std::sort(hull_sorted.begin(), hull_sorted.end(), lex);
                        std::vector<glm::vec3> removed, added;
                        std::set_difference(old_sorted.begin(), old_sorted.end(), new_sorted.begin(), new_sorted.end(), std::back_inserter(removed), lex);
                        std::set_difference(new_sorted.begin(), new_sorted.end(), old_sorted.begin(), old_sorted.end(), std::back_inserter(added), lex);
                        sums.cs_added += added.size();
                        sums.cs_removed += removed.size();
                        reusable = true;
                        for (const glm::vec3& r : removed) {
                            if (std::binary_search(hull_sorted.begin(), hull_sorted.end(), r, lex)) {
                                reusable = false;
                                ++sums.cs_removed_vertex;
                                break;
                            }
                        }
                        if (reusable) {
                            for (const glm::vec3& a : added) {
                                if (!erhe::math::point_in_convex_hull(receiver_hull, a, 1e-5f)) {
                                    reusable = false;
                                    ++sums.cs_added_outside;
                                    break;
                                }
                            }
                        }
                        if (reusable) {
                            ++sums.cs_reusable;
                        }
                    }
                    prev_points.swap(gathered);
                    prev_corners = frame.corners;
                    has_result = true;
                    hull_valid = false;
                    clipped_hull.clear();
                    const Clock::time_point t2 = Clock::now();
                    if (prev_points.size() >= 4) {
                        if (!same_corner_set) {
                            erhe::math::calculate_bounding_convex_hull(prev_points, receiver_hull);
                            ++sums.rcv_hull_runs;
                            if (condition_valid) {
                                const auto lex = [](const glm::vec3& a, const glm::vec3& b) { return (a.x != b.x) ? (a.x < b.x) : ((a.y != b.y) ? (a.y < b.y) : (a.z < b.z)); };
                                std::vector<glm::vec3> a = old_hull_vertices, b = receiver_hull.points;
                                std::sort(a.begin(), a.end(), lex);
                                std::sort(b.begin(), b.end(), lex);
                                const bool same = (a == b);
                                if (same) { ++sums.cs_hull_same; }
                                if (reusable && !same) { ++sums.cs_reuse_wrong; }
                            }
                        }
                        const Clock::time_point t3 = Clock::now();
                        sums.rcv_hull += micro(t2, t3);
                        if (!receiver_hull.triangle_indices.empty()) {
                            erhe::math::clip_convex_hull_points_to_frustum(receiver_hull, frame.planes, frame.corners, clipped);
                            if (clipped.size() >= 4) {
                                erhe::math::calculate_bounding_convex_hull(clipped, clipped_hull);
                                hull_valid = !clipped_hull.triangle_indices.empty();
                            }
                        }
                        ++sums.rcv_clip_runs;
                        sums.rcv_clip += micro(t3, Clock::now());
                    }
                }
            }

            // ---- per light ----
            for (std::size_t light_index = 0; light_index < frame.lights.size(); ++light_index) {
                const Replay_light& light = frame.lights[light_index];
                const glm::vec3 dir = glm::normalize(light.direction);
                ++sums.fits;

                std::vector<glm::vec4> receiver_planes;
                {
                    const Clock::time_point t0 = Clock::now();
                    if (hull_valid) {
                        erhe::math::build_shadow_caster_cull_planes_from_hull(clipped_hull, dir, receiver_planes, nullptr);
                    }
                    sums.rcv_planes += micro(t0, Clock::now());
                }

                const Clock::time_point tv0 = Clock::now();
                const erhe::math::Shadow_volume_planes     volume     = erhe::math::build_shadow_caster_volume_planes(frame.planes, dir);
                const erhe::math::Shadow_caster_silhouette silhouette = erhe::math::build_shadow_caster_silhouette(frame.planes, frame.corners, dir);
                std::vector<glm::vec4> filter_planes;
                filter_planes.assign(volume.planes_span().begin(), volume.planes_span().end());
                filter_planes.insert(filter_planes.end(), silhouette.planes_span().begin(), silhouette.planes_span().end());
                sums.volume_planes += micro(tv0, Clock::now());

                std::vector<erhe::math::Aabb> survivors;
                std::vector<uint32_t>         survivor_ids;
                {
                    const Clock::time_point t0 = Clock::now();
                    for (std::size_t c = 0; c < frame.casters.size(); ++c) {
                        const erhe::math::Aabb& aabb = frame.casters[c];
                        if (!aabb.is_valid_3d()) {
                            continue;
                        }
                        const bool in_shadow = erhe::math::aabb_in_convex_volume(filter_planes, aabb);
                        bool in_receiver = true;
                        if (!receiver_planes.empty() && in_shadow) {
                            in_receiver = erhe::math::aabb_in_convex_volume(receiver_planes, aabb);
                        }
                        const bool keep = in_shadow && in_receiver;
                        if (keep) {
                            survivors.push_back(aabb);
                            survivor_ids.push_back(static_cast<uint32_t>(c));
                        }
                        if (c < light.flags.size()) {
                            ++sums.recorded_total;
                            if ((light.flags[c] == '1') == keep) {
                                ++sums.recorded_agree;
                            }
                        }
                    }
                    sums.filter += micro(t0, Clock::now());
                }
                sums.survivors += survivors.size();

                // ---- per box clip: interior boxes keep 8 corners, boundary boxes are clipped ----
                std::vector<glm::vec3>    caster_points;                 // baseline fit point set, in baseline order
                std::vector<std::size_t>  interior_boxes;                // indices into survivors
                std::vector<glm::vec3>    boundary_points;               // clipped points of boundary boxes (+ frustum corners)
                std::vector<glm::vec3>    box_clip_points;
                {
                    const Clock::time_point t0 = Clock::now();
                    unsigned int frustum_corners_added = 0u;
                    for (std::size_t s = 0; s < survivors.size(); ++s) {
                        const erhe::math::Aabb& aabb = survivors[s];
                        box_hull.points.clear();
                        for (std::size_t i = 0; i < 8; ++i) {
                            box_hull.points.push_back(box_corner(aabb, i));
                        }
                        const glm::vec3 center = aabb.center();
                        const glm::vec3 extent = 0.5f * aabb.diagonal();
                        bool fully_inside = true;
                        for (const glm::vec4& plane : volume.planes_span()) {
                            const glm::vec3 normal{plane};
                            if ((glm::dot(normal, center) + plane.w) < glm::dot(glm::abs(normal), extent)) {
                                fully_inside = false;
                                break;
                            }
                        }
                        if (fully_inside) {
                            caster_points.insert(caster_points.end(), box_hull.points.begin(), box_hull.points.end());
                            interior_boxes.push_back(s);
                            continue;
                        }
                        erhe::math::clip_convex_hull_points_by_planes(box_hull, volume.planes_span(), box_clip_points);
                        caster_points.insert(caster_points.end(), box_clip_points.begin(), box_clip_points.end());
                        boundary_points.insert(boundary_points.end(), box_clip_points.begin(), box_clip_points.end());
                        for (std::size_t corner_index = 0; corner_index < 8; ++corner_index) {
                            if ((frustum_corners_added & (1u << corner_index)) != 0u) {
                                continue;
                            }
                            const glm::vec3& corner = frame.corners[corner_index];
                            if ((corner.x >= aabb.min.x) && (corner.x <= aabb.max.x) && (corner.y >= aabb.min.y) && (corner.y <= aabb.max.y) && (corner.z >= aabb.min.z) && (corner.z <= aabb.max.z)) {
                                caster_points.push_back(corner);
                                boundary_points.push_back(corner);
                                frustum_corners_added |= (1u << corner_index);
                            }
                        }
                    }
                    sums.clip += micro(t0, Clock::now());
                }
                sums.interior += interior_boxes.size();
                sums.boundary += survivors.size() - interior_boxes.size();
                sums.base_points += static_cast<double>(caster_points.size());
                if (caster_points.size() < 3) {
                    continue;
                }

                const Light_basis basis{dir};

                // ---- baseline: project, 2D hull, calipers, then fit_box ----
                std::vector<glm::vec2> projected, base_hull;
                glm::mat2 roll{1.0f};
                {
                    const Clock::time_point t0 = Clock::now();
                    projected.reserve(caster_points.size());
                    for (const glm::vec3& p : caster_points) {
                        projected.push_back(glm::vec2{basis.light_from_world * p});
                    }
                    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{projected}, base_hull);
                    if (base_hull.size() >= 3) {
                        roll = erhe::math::calculate_min_area_obb_2d(base_hull).edge_from_original;
                    }
                    sums.cal_base += micro(t0, Clock::now());
                }
                sums.hull_size += base_hull.size();
                glm::vec2 base_min{std::numeric_limits<float>::max()}, base_max{std::numeric_limits<float>::lowest()};
                float base_smin = std::numeric_limits<float>::max(), base_smax = std::numeric_limits<float>::lowest();
                {
                    const Clock::time_point t0 = Clock::now();
                    for (const glm::vec3& p : caster_points) {
                        const glm::vec3 pl = basis.light_from_world * p;
                        const glm::vec2 pr = roll * glm::vec2{pl};
                        const float     s  = pl.z;
                        base_min = glm::min(base_min, pr); base_max = glm::max(base_max, pr);
                        base_smin = std::min(base_smin, s); base_smax = std::max(base_smax, s);
                    }
                    sums.fit_box_base += micro(t0, Clock::now());
                }

                // ---- pruned variants (interior boxes pruned; boundary points always kept) ----
                const auto run_variant = [&](const std::size_t direction_count, const std::size_t seed_vertices, double& total_us, double& prune_ratio, double& point_ratio, std::size_t& mismatches, std::vector<uint32_t>* participants_io, const bool measure_extents) {
                    const Clock::time_point t0 = Clock::now();
                    std::vector<Bench_box> interior;
                    interior.reserve(interior_boxes.size());
                    for (const std::size_t s : interior_boxes) {
                        interior.push_back(Bench_box{survivors[s].min, survivors[s].max});
                    }
                    footprints.build(interior, basis);

                    std::vector<glm::vec2> inner_points;
                    if (direction_count > 0) {
                        extreme_corners_vec(footprints, direction_count, inner_points);
                    }
                    if ((seed_vertices > 0) && (participants_io != nullptr)) {
                        for (const uint32_t id : *participants_io) {
                            // seed ids are caster indices; seeds are valid only while the caster list is the same size
                            for (std::size_t k = 0; k < interior.size(); ++k) {
                                if (survivor_ids[interior_boxes[k]] == id) {
                                    for (std::size_t c = 0; c < 8; ++c) {
                                        inner_points.push_back(glm::vec2{basis.light_from_world * box_corner(survivors[interior_boxes[k]], c)});
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    for (const glm::vec3& p : boundary_points) {
                        inner_points.push_back(glm::vec2{basis.light_from_world * p}); // real set points too
                    }
                    Inner_polygon poly;
                    std::vector<glm::vec2> inner_hull;
                    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{inner_points}, inner_hull);
                    poly.build(inner_hull, (seed_vertices > 0) ? seed_vertices : 32u);

                    std::size_t pruned = 0;
                    if (poly.valid()) {
                        pruned = prune_edge_outer(footprints, poly, inside);
                    } else {
                        inside.assign(interior.size(), 0);
                    }
                    std::vector<glm::vec2> pts;
                    std::vector<uint32_t>  owners;
                    for (std::size_t k = 0; k < interior.size(); ++k) {
                        if (inside[k] != 0) { continue; }
                        for (std::size_t c = 0; c < 8; ++c) {
                            const glm::vec2 q{basis.light_from_world * box_corner(survivors[interior_boxes[k]], c)};
                            bool in = false;
                            if (poly.valid()) {
                                in = true;
                                for (std::size_t e = 0; e < poly.nx.size(); ++e) {
                                    if ((poly.nx[e] * q.x + poly.ny[e] * q.y) < poly.d[e]) { in = false; break; }
                                }
                            }
                            if (!in) {
                                pts.push_back(q);
                                owners.push_back(survivor_ids[interior_boxes[k]]);
                            }
                        }
                    }
                    const std::size_t interior_points = pts.size();
                    for (const glm::vec3& p : boundary_points) {
                        pts.push_back(glm::vec2{basis.light_from_world * p});
                        owners.push_back(0xffffffffu);
                    }
                    std::vector<glm::vec2> hull;
                    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{pts}, hull);
                    glm::mat2 var_roll{1.0f};
                    if (hull.size() >= 3) {
                        var_roll = erhe::math::calculate_min_area_obb_2d(hull).edge_from_original;
                    }
                    if (measure_extents) {
                        // cheap per box extents in the rolled frame for interior boxes, plus the boundary points
                        glm::vec2 lo{std::numeric_limits<float>::max()}, hi{std::numeric_limits<float>::lowest()};
                        float slo = std::numeric_limits<float>::max(), shi = std::numeric_limits<float>::lowest();
                        const glm::vec3 rzv = basis.rz;
                        for (const std::size_t s : interior_boxes) {
                            const glm::vec3 c = survivors[s].center();
                            const glm::vec3 e = 0.5f * survivors[s].diagonal();
                            const glm::vec2 c2{glm::dot(basis.rx, c), glm::dot(basis.ry, c)};
                            const glm::vec2 cr = var_roll * c2;
                            const glm::vec2 ax = var_roll * glm::vec2{basis.rx.x, basis.ry.x} * e.x;
                            const glm::vec2 ay = var_roll * glm::vec2{basis.rx.y, basis.ry.y} * e.y;
                            const glm::vec2 az = var_roll * glm::vec2{basis.rx.z, basis.ry.z} * e.z;
                            const glm::vec2 h = glm::abs(ax) + glm::abs(ay) + glm::abs(az);
                            lo = glm::min(lo, cr - h); hi = glm::max(hi, cr + h);
                            const float cs = glm::dot(rzv, c);
                            const float hs = std::abs(rzv.x) * e.x + std::abs(rzv.y) * e.y + std::abs(rzv.z) * e.z;
                            slo = std::min(slo, cs - hs); shi = std::max(shi, cs + hs);
                        }
                        for (const glm::vec3& p : boundary_points) {
                            const glm::vec3 pl = basis.light_from_world * p;
                            const glm::vec2 pr = var_roll * glm::vec2{pl};
                            lo = glm::min(lo, pr); hi = glm::max(hi, pr);
                            slo = std::min(slo, pl.z); shi = std::max(shi, pl.z);
                        }
                        const float tol = 1e-3f * std::max(1.0f, glm::length(base_max - base_min));
                        const bool same_roll = glm::all(glm::lessThan(glm::abs(var_roll[0] - roll[0]), glm::vec2{1e-4f})) && glm::all(glm::lessThan(glm::abs(var_roll[1] - roll[1]), glm::vec2{1e-4f}));
                        if (same_roll && ((glm::length(lo - base_min) > tol) || (glm::length(hi - base_max) > tol) || (std::abs(slo - base_smin) > tol) || (std::abs(shi - base_smax) > tol))) {
                            ++sums.extent_mismatch;
                        }
                    }
                    total_us += micro(t0, Clock::now());
                    prune_ratio += interior.empty() ? 0.0 : static_cast<double>(pruned) / static_cast<double>(interior.size());
                    point_ratio += static_cast<double>(pts.size()) / static_cast<double>(std::max<std::size_t>(1, caster_points.size()));
                    (void)interior_points;
                    const bool equal = (hull.size() == base_hull.size()) &&
                        std::equal(hull.begin(), hull.end(), base_hull.begin(), [](const glm::vec2& a, const glm::vec2& b) { return (a.x == b.x) && (a.y == b.y); });
                    if (!equal) { ++mismatches; }
                    if (participants_io != nullptr) {
                        std::vector<uint32_t> next;
                        for (const glm::vec2& hv : hull) {
                            for (std::size_t p = 0; p < pts.size(); ++p) {
                                if ((pts[p].x == hv.x) && (pts[p].y == hv.y)) {
                                    if (owners[p] != 0xffffffffu) { next.push_back(owners[p]); }
                                    break;
                                }
                            }
                        }
                        std::sort(next.begin(), next.end());
                        next.erase(std::unique(next.begin(), next.end()), next.end());
                        *participants_io = next;
                    }
                };
                run_variant(16, 0, sums.var_k16, sums.prune_k16, sums.points_k16, sums.mismatches_k16, nullptr, true);
                if (prev_caster_count[light_index] != frame.casters.size()) {
                    prev_participants[light_index].clear();
                }
                prev_caster_count[light_index] = frame.casters.size();
                run_variant(4, 32, sums.var_seed, sums.prune_seed, sums.points_seed, sums.mismatches_seed, &prev_participants[light_index], false);
            }
        }

        const double frames_n = static_cast<double>(sums.frames);
        const double fits_n   = static_cast<double>(std::max<std::size_t>(1, sums.fits));
        const double per_frame_receiver = (sums.rcv_gather + sums.rcv_hull + sums.rcv_clip) / frames_n;
        const double per_fit_other = (sums.rcv_planes + sums.volume_planes + sums.filter + sums.clip) / fits_n;
        const double cal_base = sums.cal_base / fits_n;
        const double fit_box  = sums.fit_box_base / fits_n;
        const double k16      = sums.var_k16 / fits_n;
        const double seed     = sums.var_seed / fits_n;
        log_line("== %s: %zu frames, %zu light fits, casters/fit surviving %.0f (interior %.0f, boundary %.0f), baseline points %.0f, hull h %.0f, recorded-verdict agreement %.2f%%",
            scenario, sums.frames, sums.fits, sums.survivors / fits_n, sums.interior / fits_n, sums.boundary / fits_n, sums.base_points / fits_n, sums.hull_size / fits_n,
            100.0 * static_cast<double>(sums.recorded_agree) / static_cast<double>(std::max<std::size_t>(1, sums.recorded_total)));
        log_line("   receiver stage per frame: gather %.0f  hull3d %.0f (ran %zu/%zu frames)  clip+rehull %.0f (ran %zu/%zu)  total %.0f",
            sums.rcv_gather / frames_n, sums.rcv_hull / frames_n, sums.rcv_hull_runs, sums.frames, sums.rcv_clip / frames_n, sums.rcv_clip_runs, sums.frames, per_frame_receiver);
        log_line("   receiver hull reuse test: corner set changed on %zu frames; hull vertex set actually unchanged on %zu; sufficient condition holds on %zu (wrong verdicts %zu); avg corners added %.1f removed %.1f per changed frame; blocked by removed hull vertex %zu, by added corner outside %zu",
            sums.cs_changed, sums.cs_hull_same, sums.cs_reusable, sums.cs_reuse_wrong,
            sums.cs_changed ? static_cast<double>(sums.cs_added) / static_cast<double>(sums.cs_changed) : 0.0,
            sums.cs_changed ? static_cast<double>(sums.cs_removed) / static_cast<double>(sums.cs_changed) : 0.0,
            sums.cs_removed_vertex, sums.cs_added_outside);
        EXPECT_EQ(sums.cs_reuse_wrong, 0u);
        log_line("   per light fit: receiver planes %.0f  volume planes %.0f  filter %.0f  clip %.0f | calipers-zone %.0f  fit_box %.0f",
            sums.rcv_planes / fits_n, sums.volume_planes / fits_n, sums.filter / fits_n, sums.clip / fits_n, cal_base, fit_box);
        log_line("   pruned K16: stage %.0f (+extents folded in)  pruned %.1f%% of interior boxes  points left %.1f%%  hull mismatches %zu  extent mismatches %zu",
            k16, 100.0 * sums.prune_k16 / fits_n, 100.0 * sums.points_k16 / fits_n, sums.mismatches_k16, sums.extent_mismatch);
        log_line("   pruned seed32+K4: stage %.0f  pruned %.1f%%  points left %.1f%%  hull mismatches %zu",
            seed, 100.0 * sums.prune_seed / fits_n, 100.0 * sums.points_seed / fits_n, sums.mismatches_seed);
        const double base_fit = per_frame_receiver / 4.0 + per_fit_other + cal_base + fit_box;
        const double new_fit  = per_frame_receiver / 4.0 + per_fit_other + k16;
        log_line("   estimated fit per light: baseline %.0f us  with K16 prune+extents %.0f us (receiver stage shared by 4 lights)", base_fit, new_fit);
        EXPECT_EQ(sums.mismatches_k16, 0u);
        EXPECT_EQ(sums.mismatches_seed, 0u);
    }
}
