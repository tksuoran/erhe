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

// The GPU binner's input block (Light_cluster_gpu_binner, res/shaders/
// light_cluster_bin.comp): c_light_cluster_bin_header_uvec4_count uvec4
// headers, then vec4 data. Light_cluster_binner::write_inputs() documents the
// contents.
inline constexpr std::size_t c_light_cluster_bin_header_uvec4_count = 3;

// The light binner (C4): tests every spot and point light's range sphere
// against every cluster's view-space bounds, and every spot light's widened
// cone against the bounding sphere of every closed cluster that passes, and
// lists the passing slots per cluster in ascending order. prepare() builds the grid and the fp32 inputs
// of the tests (cluster bounds from tile corner rays and slice depths, light
// spheres in the cluster camera's node space); bin_prepared() runs the tests
// on the CPU. The GPU binner (Light_cluster_gpu_binner) runs the same tests,
// in the same fp32 operations, on the inputs write_inputs() writes, so both
// produce the same lists bit for bit. Keeps its buffers across calls
// (cleared, capacity kept), so a steady-state frame allocates nothing.
class Light_cluster_binner
{
public:
    // Builds the grid for view and the fp32 test inputs from the lights of
    // light_projections (Light_projections::apply() of this frame).
    void prepare(const Light_projections& light_projections, const Light_cluster_view& view, const Light_cluster_config& config);
    // Runs the tests of the last prepare() on the CPU.
    void bin_prepared();
    // prepare() and bin_prepared().
    void bin(const Light_projections& light_projections, const Light_cluster_view& view, const Light_cluster_config& config);

    [[nodiscard]] auto get_grid                  () const -> const Light_cluster_grid& { return m_grid; }
    // Most lights one cluster lists (the configured capacity, clamped to
    // [1, 65535]), from the last prepare().
    [[nodiscard]] auto get_capacity              () const -> std::size_t               { return m_capacity; }
    // Spot and point slots the last prepare() bins.
    [[nodiscard]] auto get_binned_light_count    () const -> std::size_t               { return m_lights.size(); }

    // The GPU binner's input block for the last prepare():
    //   header[0] = tiles x, tiles y, slices, capacity
    //   header[1] = the first slot of spot shadow-mapped, spot unshadowed,
    //               point shadow-mapped, point unshadowed lights
    //   header[2] = one past the last binned slot, open-side flags (bit 0:
    //               the first tile column faces node-space -x, bit 1: the
    //               first tile row faces -y), the first list uint, list
    //               stride in uints (get_fixed_list_stride())
    //   data[slice]                            = slice near depth, far depth, widening, 0
    //   data[slices + 2 corner + 0 / 1]        = corner ray origin (view depth 0) / direction (per unit depth)
    //   data[slices + 2 corners + binned slot] = light centre (node space), radius (< 0: unbounded)
    //   data[slices + 2 corners + binned count + binned slot] = spot cone:
    //       emission axis (node space) times the sine of the widened outer
    //       half angle, its cotangent; zero for point lights and for a cone
    //       widened to the whole sphere
    [[nodiscard]] auto get_input_byte_count      () const -> std::size_t;
    void write_inputs(std::span<std::byte> out) const;
    // The fixed-offset layout the GPU binner writes (C3): cluster headers,
    // then get_fixed_list_stride() uints per cluster at cluster order.
    [[nodiscard]] auto get_fixed_list_stride     () const -> std::size_t;
    [[nodiscard]] auto get_fixed_byte_count      () const -> std::size_t;

    // From the last bin_prepared():
    [[nodiscard]] auto get_byte_count            () const -> std::size_t;
    // Clusters whose list was cut at the capacity.
    [[nodiscard]] auto get_overflow_cluster_count() const -> std::size_t               { return m_overflow_cluster_count; }
    // Longest list the grid asked for, before the capacity cut.
    [[nodiscard]] auto get_max_list_length       () const -> std::size_t               { return m_max_list_length; }
    // The cluster data, compact lists (light cluster block layout).
    void write(std::span<std::byte> out) const;

    // The lights of one cluster: ascending slots. Tests and diagnostics.
    [[nodiscard]] auto get_cluster_slots          (std::size_t cluster) const -> std::span<const uint32_t>;
    // Spot shadow-mapped, spot unshadowed, point shadow-mapped, point
    // unshadowed counts of one cluster's list.
    [[nodiscard]] auto get_cluster_category_counts(std::size_t cluster) const -> std::span<const uint32_t>;

private:
    class Hit
    {
    public:
        uint32_t cluster;
        uint32_t slot;
    };

    void build_grid(const Light_projections& light_projections, const Light_cluster_view& view, const Light_cluster_config& config);

    Light_cluster_grid      m_grid{};
    std::size_t             m_capacity{1};
    uint32_t                m_category_begin[5]{};
    uint32_t                m_open_flags{0};
    // The fp32 test inputs (write_inputs()).
    std::vector<glm::vec4>  m_slices;          // per slice: near depth, far depth, widening, 0
    std::vector<glm::vec4>  m_corners;         // per corner: origin, direction
    std::vector<glm::vec4>  m_lights;          // per binned slot: centre, radius
    std::vector<glm::vec4>  m_cones;           // per binned slot: axis * sin, cot (0 for point lights)
    // Per cluster: the widened bounds and the open-side flags.
    std::vector<glm::vec3>  m_cluster_min;
    std::vector<glm::vec3>  m_cluster_max;
    std::vector<uint32_t>   m_cluster_open;
    std::vector<Hit>        m_hits;
    std::vector<uint32_t>   m_cluster_counts;   // per cluster, before the capacity cut
    std::vector<uint32_t>   m_cluster_offsets;  // per cluster + 1, into m_sorted_slots
    std::vector<uint32_t>   m_sorted_slots;     // after the capacity cut
    std::vector<uint32_t>   m_category_counts;  // 4 per cluster, after the capacity cut
    std::size_t             m_overflow_cluster_count{0};
    std::size_t             m_max_list_length{0};
};

} // namespace erhe::scene_renderer
