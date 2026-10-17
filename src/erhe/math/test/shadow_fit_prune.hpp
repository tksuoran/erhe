#pragma once

// Interior box pruning helpers for the shadow fit experiments (see
// test_shadow_fit_bench.cpp). Boxes are described by their light plane
// footprint (center and the images of the three half axes); a convex inner
// polygon whose vertices are real set points proves a box cannot contribute a
// vertex to the 2D hull of the whole set.

#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace fit_experiment {

class Bench_box
{
public:
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

class Light_basis
{
public:
    explicit Light_basis(const glm::vec3& direction)
    {
        rz = glm::normalize(direction);
        const glm::vec3 up = (std::abs(rz.y) > 0.999f) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
        rx = glm::normalize(glm::cross(up, rz));
        ry = glm::cross(rz, rx);
        light_from_world = glm::transpose(glm::mat3{rx, ry, rz});
    }
    glm::vec3 rx{1.0f, 0.0f, 0.0f};
    glm::vec3 ry{0.0f, 1.0f, 0.0f};
    glm::vec3 rz{0.0f, 0.0f, 1.0f};
    glm::mat3 light_from_world{1.0f};
};

// ---- Footprints: per box light plane center and half axes (SoA) -----------

class Footprints
{
public:
    std::vector<float> cx, cy;     // light plane center
    std::vector<float> axx, axy;   // image of the box x half axis
    std::vector<float> ayx, ayy;   // image of the box y half axis
    std::vector<float> azx, azy;   // image of the box z half axis

    void build(const std::vector<Bench_box>& boxes, const Light_basis& basis)
    {
        const std::size_t n = boxes.size();
        cx.resize(n); cy.resize(n);
        axx.resize(n); axy.resize(n);
        ayx.resize(n); ayy.resize(n);
        azx.resize(n); azy.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec3 c = 0.5f * (boxes[i].min + boxes[i].max);
            const glm::vec3 e = 0.5f * (boxes[i].max - boxes[i].min);
            cx[i]  = glm::dot(basis.rx, c);
            cy[i]  = glm::dot(basis.ry, c);
            axx[i] = e.x * basis.rx.x; axy[i] = e.x * basis.ry.x;
            ayx[i] = e.y * basis.rx.y; ayy[i] = e.y * basis.ry.y;
            azx[i] = e.z * basis.rx.z; azy[i] = e.z * basis.ry.z;
        }
    }
};

class Inner_polygon
{
public:
    // Edge i keeps points with nx * x + ny * y >= d (inward normal).
    std::vector<float> nx, ny, d;
    std::size_t        vertex_count{0};
    [[nodiscard]] auto valid() const -> bool { return vertex_count >= 3; }

    void build(const std::vector<glm::vec2>& hull, const std::size_t max_vertices)
    {
        nx.clear(); ny.clear(); d.clear();
        vertex_count = 0;
        if (hull.size() < 3) {
            return;
        }
        // Thinning keeps a subset of the hull vertices, which is still a convex
        // polygon with real set points as vertices.
        std::vector<glm::vec2> v;
        if (hull.size() > max_vertices) {
            for (std::size_t i = 0; i < max_vertices; ++i) {
                v.push_back(hull[(i * hull.size()) / max_vertices]);
            }
        } else {
            v = hull;
        }
        glm::vec2 lo{std::numeric_limits<float>::max()};
        glm::vec2 hi{std::numeric_limits<float>::lowest()};
        for (const glm::vec2& p : v) {
            lo = glm::min(lo, p);
            hi = glm::max(hi, p);
        }
        const float slack = 1e-5f * std::max(1.0f, std::max(hi.x - lo.x, hi.y - lo.y));
        const std::size_t count = v.size();
        for (std::size_t i = 0; i < count; ++i) {
            const glm::vec2 a   = v[i];
            const glm::vec2 b   = v[(i + 1) % count];
            const glm::vec2 dir = b - a;
            const float     len = glm::length(dir);
            if (len < 1e-6f) {
                continue;
            }
            const glm::vec2 n{-dir.y / len, dir.x / len};
            nx.push_back(n.x);
            ny.push_back(n.y);
            d.push_back(glm::dot(n, a) + slack);
        }
        vertex_count = nx.size();
    }
};

// Box-outer, early out per box (scalar).
[[nodiscard]] inline auto prune_box_outer(const Footprints& f, const Inner_polygon& poly, std::vector<uint8_t>& inside) -> std::size_t
{
    const std::size_t n = f.cx.size();
    inside.assign(n, 0);
    std::size_t pruned = 0;
    const std::size_t edges = poly.nx.size();
    for (std::size_t i = 0; i < n; ++i) {
        bool all = true;
        for (std::size_t e = 0; e < edges; ++e) {
            const float nx = poly.nx[e];
            const float ny = poly.ny[e];
            const float support =
                std::abs(nx * f.axx[i] + ny * f.axy[i]) +
                std::abs(nx * f.ayx[i] + ny * f.ayy[i]) +
                std::abs(nx * f.azx[i] + ny * f.azy[i]);
            if (((nx * f.cx[i]) + (ny * f.cy[i]) - support) < poly.d[e]) {
                all = false;
                break;
            }
        }
        inside[i] = all ? 1 : 0;
        pruned += all ? 1 : 0;
    }
    return pruned;
}

// Edge-outer, branchless inner loop over the SoA arrays (auto-vectorizable).
[[nodiscard]] inline auto prune_edge_outer(const Footprints& f, const Inner_polygon& poly, std::vector<uint8_t>& inside) -> std::size_t
{
    const std::size_t n = f.cx.size();
    inside.assign(n, 1);
    const std::size_t edges = poly.nx.size();
    uint8_t* const out = inside.data();
    for (std::size_t e = 0; e < edges; ++e) {
        const float nx = poly.nx[e];
        const float ny = poly.ny[e];
        const float d  = poly.d[e];
        const float* cx = f.cx.data();  const float* cy = f.cy.data();
        const float* axx = f.axx.data(); const float* axy = f.axy.data();
        const float* ayx = f.ayx.data(); const float* ayy = f.ayy.data();
        const float* azx = f.azx.data(); const float* azy = f.azy.data();
        for (std::size_t i = 0; i < n; ++i) {
            const float support =
                std::abs(nx * axx[i] + ny * axy[i]) +
                std::abs(nx * ayx[i] + ny * ayy[i]) +
                std::abs(nx * azx[i] + ny * azy[i]);
            const bool ok = ((nx * cx[i]) + (ny * cy[i]) - support) >= d;
            out[i] = static_cast<uint8_t>(out[i] & (ok ? 1u : 0u));
        }
    }
    std::size_t pruned = 0;
    for (std::size_t i = 0; i < n; ++i) {
        pruned += out[i];
    }
    return pruned;
}

// Extreme corner of every box in K fixed directions; the corners are real set
// points, their hull is the inner polygon.
inline void extreme_polygon(const Footprints& f, const std::size_t direction_count, Inner_polygon& poly)
{
    const std::size_t n = f.cx.size();
    std::vector<glm::vec2> extremes;
    for (std::size_t k = 0; k < direction_count; ++k) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(direction_count);
        const float ux = std::cos(angle);
        const float uy = std::sin(angle);
        float     best = std::numeric_limits<float>::lowest();
        glm::vec2 best_point{0.0f};
        for (std::size_t i = 0; i < n; ++i) {
            const float pa = ux * f.axx[i] + uy * f.axy[i];
            const float pb = ux * f.ayx[i] + uy * f.ayy[i];
            const float pc = ux * f.azx[i] + uy * f.azy[i];
            const float s  = ux * f.cx[i] + uy * f.cy[i] + std::abs(pa) + std::abs(pb) + std::abs(pc);
            if (s > best) {
                best = s;
                const float sa = (pa >= 0.0f) ? 1.0f : -1.0f;
                const float sb = (pb >= 0.0f) ? 1.0f : -1.0f;
                const float sc = (pc >= 0.0f) ? 1.0f : -1.0f;
                best_point = glm::vec2{
                    f.cx[i] + sa * f.axx[i] + sb * f.ayx[i] + sc * f.azx[i],
                    f.cy[i] + sa * f.axy[i] + sb * f.ayy[i] + sc * f.azy[i]
                };
            }
        }
        extremes.push_back(best_point);
    }
    std::vector<glm::vec2> hull;
    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{extremes}, hull);
    poly.build(hull, 64);
}


// Extreme corners in K fixed directions with a lane-wise argmax that the
// compiler can vectorize (8 independent lanes over the SoA arrays).
inline void extreme_corners_vec(const Footprints& f, const std::size_t direction_count, std::vector<glm::vec2>& extremes)
{
    const std::size_t n = f.cx.size();
    constexpr std::size_t lanes = 8;
    for (std::size_t k = 0; k < direction_count; ++k) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(direction_count);
        const float ux = std::cos(angle);
        const float uy = std::sin(angle);
        float    best[lanes];
        uint32_t best_index[lanes];
        for (std::size_t l = 0; l < lanes; ++l) {
            best[l] = std::numeric_limits<float>::lowest();
            best_index[l] = 0;
        }
        const std::size_t blocks = n / lanes;
        for (std::size_t b = 0; b < blocks; ++b) {
            const std::size_t base = b * lanes;
            for (std::size_t l = 0; l < lanes; ++l) {
                const std::size_t i = base + l;
                const float s =
                    ux * f.cx[i] + uy * f.cy[i] +
                    std::abs(ux * f.axx[i] + uy * f.axy[i]) +
                    std::abs(ux * f.ayx[i] + uy * f.ayy[i]) +
                    std::abs(ux * f.azx[i] + uy * f.azy[i]);
                const bool better = s > best[l];
                best[l]       = better ? s : best[l];
                best_index[l] = better ? static_cast<uint32_t>(i) : best_index[l];
            }
        }
        float    top = std::numeric_limits<float>::lowest();
        uint32_t top_index = 0;
        for (std::size_t l = 0; l < lanes; ++l) {
            if (best[l] > top) { top = best[l]; top_index = best_index[l]; }
        }
        for (std::size_t i = blocks * lanes; i < n; ++i) {
            const float s =
                ux * f.cx[i] + uy * f.cy[i] +
                std::abs(ux * f.axx[i] + uy * f.axy[i]) +
                std::abs(ux * f.ayx[i] + uy * f.ayy[i]) +
                std::abs(ux * f.azx[i] + uy * f.azy[i]);
            if (s > top) { top = s; top_index = static_cast<uint32_t>(i); }
        }
        const std::size_t i = top_index;
        const float pa = ux * f.axx[i] + uy * f.axy[i];
        const float pb = ux * f.ayx[i] + uy * f.ayy[i];
        const float pc = ux * f.azx[i] + uy * f.azy[i];
        const float sa = (pa >= 0.0f) ? 1.0f : -1.0f;
        const float sb = (pb >= 0.0f) ? 1.0f : -1.0f;
        const float sc = (pc >= 0.0f) ? 1.0f : -1.0f;
        extremes.push_back(
            glm::vec2{
                f.cx[i] + sa * f.axx[i] + sb * f.ayx[i] + sc * f.azx[i],
                f.cy[i] + sa * f.axy[i] + sb * f.ayy[i] + sc * f.azy[i]
            }
        );
    }
}


} // namespace fit_experiment
