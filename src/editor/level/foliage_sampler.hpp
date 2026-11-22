#pragma once

#include "level/foliage_index.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/level_repeat.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Scene-free sampling of the foliage paint tool (doc/plans/foliage_paint_tool_design.md FD8, FD10).
namespace editor {

inline constexpr std::string_view c_foliage_paint_stream {"foliage paint"};
inline constexpr std::string_view c_foliage_remove_stream{"foliage remove"};

inline constexpr std::size_t   c_foliage_min_attempts            = 1;
inline constexpr std::size_t   c_foliage_max_attempts            = 1024;
inline constexpr std::size_t   c_foliage_candidates_per_attempt  = 8;
inline constexpr std::uint32_t c_foliage_draw_24_range           = 1u << 24;
// The index query's relative widening over the spacing rule's reach (2^-16).
inline constexpr float         c_foliage_spacing_query_margin    = 1.0f / 65536.0f;

// FD8.3: the stream of a paint stroke and of a remove stroke.
[[nodiscard]] auto make_foliage_paint_random (int seed) -> Kit_random;
[[nodiscard]] auto make_foliage_remove_random(int seed) -> Kit_random;

// FD8.1: round(density * pi * radius^2), clamped to 1 - 1024; a count that is not a number is 1.
[[nodiscard]] auto get_attempt_count(float density, float radius) -> std::size_t;

class Foliage_brush_frame
{
public:
    glm::vec3 center  {0.0f};
    glm::vec3 normal  {0.0f, 1.0f, 0.0f};
    glm::vec3 tangent0{1.0f, 0.0f, 0.0f};
    glm::vec3 tangent1{0.0f, 0.0f, 1.0f};
};

// FD8.2 in single float operations; the least-along axis ties pick x, then y; the normal is finite and not zero.
// tangent0 = normalize(cross(n, that axis)), tangent1 = cross(n, tangent0).
[[nodiscard]] auto make_brush_frame(glm::vec3 center, glm::vec3 normal) -> Foliage_brush_frame;

class Foliage_candidate_ray
{
public:
    glm::vec3 origin   {0.0f};
    glm::vec3 direction{0.0f}; // minus the brush normal
    float     length   {0.0f}; // 2 h
};

// FD8.4 step 2 for the two 24-bit draws of one candidate; false (a spent candidate) when the pair is not inside the
// unit circle. The radius is finite and above zero.
[[nodiscard]] auto make_candidate_ray(
    const Foliage_brush_frame& frame,
    float                      radius,
    std::uint32_t              draw_x,
    std::uint32_t              draw_y,
    Foliage_candidate_ray&     out_ray
) -> bool;

class Foliage_ray_hit
{
public:
    glm::vec3     point  {0.0f};
    glm::vec3     normal {0.0f, 1.0f, 0.0f};
    std::uint64_t support{0}; // the caller's token of the hit mesh
};

enum class Foliage_ray_outcome : unsigned int {
    surface = 0,    // out_hit holds the nearest surface hit
    no_surface,
    sealed_support  // the nearest hit is a mesh nothing may be added under (inside a reference instance)
};

// The surface ray a stamp casts per candidate (find_ray_surface() in the editor, a plane in the tests).
class IFoliage_ray
{
public:
    virtual ~IFoliage_ray() noexcept;

    // The nearest surface hit along the ray within its length.
    [[nodiscard]] virtual auto find_surface(const Foliage_candidate_ray& ray, Foliage_ray_hit& out_hit) -> Foliage_ray_outcome = 0;
};

// FD8.5 on squared distances; the index is queried a little wider than the rule, so it can only over-include.
[[nodiscard]] auto is_spacing_free(
    const Foliage_index&                    index,
    glm::vec3                               point,
    float                                   radius,
    std::vector<Foliage_index::Handle>&     scratch_handles
) -> bool;
[[nodiscard]] auto is_spacing_free(std::span<const Foliage_index_entry> stamp_points, glm::vec3 point, float radius) -> bool;

// One palette entry as the sampler reads it: pick weight, spacing radius, yaw in degrees, variant count (at least 1).
class Foliage_sampler_entry
{
public:
    std::uint32_t weight         {1};
    float         radius         {0.0f};
    float         yaw_min_degrees{0.0f};
    float         yaw_max_degrees{360.0f};
    std::uint32_t variants       {1};
};

// The palette as the sampler reads it, built once per stroke; scatter item i is entry i.
class Foliage_sampler_palette
{
public:
    Foliage_sampler_palette() = default;
    explicit Foliage_sampler_palette(std::span<const Foliage_sampler_entry> entries);

    // Replaces the entries; the buffers keep their capacity.
    void assign(std::span<const Foliage_sampler_entry> entries);

    [[nodiscard]] auto get_entries      () const -> std::span<const Foliage_sampler_entry>;
    [[nodiscard]] auto get_scatter_items() const -> std::span<const Repeat_scatter_item>;
    [[nodiscard]] auto get_total_weight () const -> std::uint64_t;

private:
    std::vector<Foliage_sampler_entry> m_entries;
    std::vector<Repeat_scatter_item>   m_scatter_items;
    std::uint64_t                      m_total_weight{0};
};

class Foliage_placement
{
public:
    glm::vec3     point      {0.0f};
    glm::vec3     normal     {0.0f, 1.0f, 0.0f};
    std::uint64_t support    {0};
    std::size_t   entry      {0};
    float         yaw_degrees{0.0f};
    std::uint32_t variant    {0};
};

class Foliage_stamp_counts
{
public:
    std::size_t attempts          {0};
    std::size_t placed            {0};
    std::size_t refused_by_spacing{0}; // candidates
    std::size_t without_surface   {0}; // candidates
    std::size_t on_sealed_support {0}; // candidates whose hit mesh is inside a reference instance
};

class Foliage_sampler_scratch
{
public:
    std::vector<Foliage_index::Handle> handles;
    std::vector<Foliage_index_entry>   stamp_points; // FD8.6
};

// One paint stamp (FD8.4 - FD8.6): get_attempt_count() attempts about the frame; clears and fills out_placements.
// A palette whose weights sum to 0 makes no attempt. The index is not changed.
[[nodiscard]] auto run_foliage_paint_stamp(
    Kit_random&                     random,
    const Foliage_brush_frame&      frame,
    float                           radius,
    float                           density,
    const Foliage_sampler_palette&  palette,
    const Foliage_index&            index,
    IFoliage_ray&                   ray,
    Foliage_sampler_scratch&        scratch,
    std::vector<Foliage_placement>& out_placements
) -> Foliage_stamp_counts;

// FD10.2: the count of 24-bit draws below remove_density * 2^24 (density clamped to 0 - 1), computed once per stroke.
[[nodiscard]] auto get_remove_threshold(float remove_density) -> std::uint32_t;

// FD10.2: whether a 24-bit draw removes its item: the draw is below the threshold.
[[nodiscard]] auto is_remove_draw(std::uint32_t draw_24, std::uint32_t threshold) -> bool;

// FD10.2: one 24-bit draw; true when the item is removed.
[[nodiscard]] auto draw_remove(Kit_random& random, std::uint32_t threshold) -> bool;

// FD10.1: the order a remove stamp visits its items: x, then y, then z, then the key.
[[nodiscard]] auto is_before_in_remove_order(const Foliage_index_entry& lhs, const Foliage_index_entry& rhs) -> bool;
void sort_in_remove_order(std::span<Foliage_index_entry> entries);

} // namespace editor
