#pragma once

#include "erhe_math/coordinate_conventions.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::scene {
    class Camera_snapshot;
}

namespace erhe::scene_renderer {

class Light_layer_partition;
class Light_projections;

// Clustered light shading (doc/plans/clustered_lights.md C2-C4). The light
// cluster block (light_cluster.data[], binding 10) holds three uints per
// cluster - the offset of the cluster's light list in data[], the spot
// shadow-mapped | spot unshadowed counts and the point shadow-mapped | point
// unshadowed counts (16 bits each, low half first) - followed by the lists:
// light array slots, two 16-bit slots per uint (low half first), ascending,
// so a list's four categories are consecutive runs in slot order.
inline constexpr std::size_t c_light_cluster_header_uint_count = 3;

// The renderer configuration of the cluster grid (Program_interface_config).
class Light_cluster_config
{
public:
    // Screen tile edge, in pixels of the grid's nominal resolution.
    int tile_size  {64};
    // View depth slices, exponential between the cluster camera's near plane
    // and the farthest depth a binned light reaches.
    int slice_count{24};
    // Most lights one cluster lists; at most 65535 (a category count is 16
    // bits). A cluster with more keeps its lowest slots and counts as
    // overflowing.
    int capacity   {64};
};

// The grid parameters the light block header carries (C2): a fragment at
// view-relative position p (relative to its pass's view origin) is at
// p_c = p + (pass view origin - view_origin) relative to the cluster camera;
// clip_from_cluster_relative * p_c gives its tile, -dot(p_c, view_axis.xyz)
// its view depth d, and floor(log2(max(d, slices.z)) * slices.x + slices.y)
// its slice, each clamped into counts. The default is the single-cluster
// grid: one tile, one slice.
class Light_cluster_grid
{
public:
    [[nodiscard]] auto get_cluster_count() const -> std::size_t
    {
        return static_cast<std::size_t>(counts.x) * static_cast<std::size_t>(counts.y) * static_cast<std::size_t>(counts.z);
    }

    glm::mat4  clip_from_cluster_relative{1.0f};
    glm::vec4  view_origin               {0.0f};               // xyz cluster camera position, w 0
    glm::vec4  view_axis                 {0.0f, 0.0f, 1.0f, 0.0f}; // xyz node-space z row of the camera's node_from_world
    glm::vec4  slices                    {0.0f, 0.0f, 1.0f, 0.0f}; // x scale, y bias, z near, w 0
    glm::uvec4 counts                    {1u, 1u, 1u, 0u};      // tiles x, tiles y, slices, 0
};

// The camera a view's grid is built for: the camera and viewport its color
// passes are rendered with (for the headset the root camera covering both
// eyes, C7) and the projection conventions of those passes.
class Light_cluster_view
{
public:
    const erhe::scene::Camera_snapshot* camera       {nullptr};
    erhe::math::Viewport                viewport     {};
    bool                                reverse_depth{true};
    erhe::math::Depth_range             depth_range  {erhe::math::Depth_range::zero_to_one};
    erhe::math::Coordinate_conventions  conventions  {};
};

// One past the last light array slot a cluster list of the partition can
// name (the end of the point unshadowed slots). The light array a list is
// bound with holds at least this many lights.
[[nodiscard]] auto get_cluster_slot_end(const Light_layer_partition& partition) -> std::size_t;

// Writes the single-cluster grid's light cluster data for a partition: one
// cluster listing every shaded spot and point slot. Returns the byte count
// (get_single_cluster_byte_count()).
[[nodiscard]] auto get_single_cluster_byte_count(const Light_layer_partition& partition) -> std::size_t;
void write_single_cluster(const Light_layer_partition& partition, std::span<std::byte> out);

// The CPU light binner (C4): tests every spot and point light's range sphere
// against every cluster's view-space bounds and lists the passing slots per
// cluster in ascending order. Keeps its buffers across calls (cleared,
// capacity kept), so a steady-state frame allocates nothing.
class Light_cluster_binner
{
public:
    // Builds the grid for view and bins the lights of light_projections
    // (Light_projections::apply() of this frame) into it.
    void bin(const Light_projections& light_projections, const Light_cluster_view& view, const Light_cluster_config& config);

    [[nodiscard]] auto get_grid                  () const -> const Light_cluster_grid& { return m_grid; }
    [[nodiscard]] auto get_byte_count            () const -> std::size_t;
    // Clusters whose list was cut at the capacity by the last bin().
    [[nodiscard]] auto get_overflow_cluster_count() const -> std::size_t               { return m_overflow_cluster_count; }
    // Longest list the last bin() asked for, before the capacity cut.
    [[nodiscard]] auto get_max_list_length       () const -> std::size_t               { return m_max_list_length; }
    // The cluster data of the last bin() (light cluster block layout).
    void write(std::span<std::byte> out) const;

    // The lights of one cluster, from the last bin(): ascending slots.
    // Tests and diagnostics.
    [[nodiscard]] auto get_cluster_slots(std::size_t cluster) const -> std::span<const uint32_t>;

private:
    class Hit
    {
    public:
        uint32_t cluster;
        uint32_t slot;
    };

    void build_grid(const Light_projections& light_projections, const Light_cluster_view& view, const Light_cluster_config& config);

    Light_cluster_grid     m_grid{};
    glm::dmat4             m_node_from_world{1.0};
    glm::dvec3             m_view_origin{0.0};
    double                 m_near{1.0};
    std::vector<double>    m_slice_depths;     // slice_count + 1 boundaries
    std::vector<glm::dvec3> m_corner_origins;  // (tiles_x + 1) * (tiles_y + 1): corner ray at view depth 0
    std::vector<glm::dvec3> m_corner_directions; // corner ray per unit of view depth
    bool                   m_x_min_at_left{true};
    bool                   m_y_min_at_bottom{true};
    std::vector<Hit>       m_hits;
    std::vector<uint32_t>  m_cluster_counts;   // per cluster, before the capacity cut
    std::vector<uint32_t>  m_cluster_offsets;  // per cluster + 1, into m_sorted_slots
    std::vector<uint32_t>  m_sorted_slots;     // after the capacity cut
    std::vector<uint32_t>  m_category_counts;  // 4 per cluster, after the capacity cut
    std::size_t            m_overflow_cluster_count{0};
    std::size_t            m_max_list_length{0};
};

} // namespace erhe::scene_renderer
