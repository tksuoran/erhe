#pragma once

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Gpu_timer;
    class Reloadable_shader_stages;
}

namespace erhe::scene_renderer {

class Light_cluster_binner;
class Light_cluster_gpu_binner;

// Where a view's light cluster lists are binned (doc/plans/clustered_lights.md
// C4): cpu, Light_cluster_binner::bin() writing compact lists into a CPU_write
// range; gpu, Light_cluster_gpu_binner's compute pass writing fixed-offset
// lists into a GPU_access range, with the CPU binner kept as the reference of
// get_light_cluster_check.
enum class Light_cluster_binning : uint8_t
{
    cpu   = 0,
    gpu   = 1,
    count = 2
};

[[nodiscard]] auto c_str(Light_cluster_binning binning) -> const char*;

// What one view's binning of one frame reported (C4): the grid, the lights
// it binned, the clusters whose list exceeded the capacity and the longest
// list the grid asked for (before the cut). For gpu binning the values of
// the retired frame frame_index, read back from the dispatch's counters.
class Light_cluster_stats
{
public:
    bool                  valid                 {false};
    uint64_t              frame_index           {0};
    Light_cluster_binning binning               {Light_cluster_binning::cpu};
    glm::uvec4            counts                {0u}; // tiles x, tiles y, slices, 0
    std::size_t           cluster_count         {0};
    std::size_t           binned_light_count    {0};
    std::size_t           capacity              {0};
    std::size_t           overflow_cluster_count{0};
    std::size_t           max_list_length       {0};
};

// The binning state of one binned view (C4), owned by the view's renderer
// and used by its render phase: a persistent host-readable buffer with one
// slot per frame in flight for the GPU binner's overflow counters (the
// dispatch of frame F copies into slot F % frames in flight, which only the
// view's own frames reuse, so the counters stay until retire() reads them
// however many frames the view skips), the dispatch's GPU timer, and the
// latest stats. Steady state allocates nothing.
class Light_cluster_view_state
{
public:
    Light_cluster_view_state();
    ~Light_cluster_view_state() noexcept;
    Light_cluster_view_state(const Light_cluster_view_state&)            = delete;
    Light_cluster_view_state& operator=(const Light_cluster_view_state&) = delete;

    // The latest stats: this frame's for cpu binning, the newest retired
    // frame's for gpu binning (invalid until one retired).
    [[nodiscard]] auto get_stats      () const -> const Light_cluster_stats& { return m_stats; }
    // The binning dispatch's GPU time of the latest completed measurement
    // (0 before the first, and with cpu binning).
    [[nodiscard]] auto get_gpu_time_ns() const -> uint64_t;

    // Render phase, at the start of the view's binning in frame_index:
    // decodes the readback of the frame that used this frame's slot (it
    // retired) into the stats.
    void retire(uint64_t frame_index);
    // The stats of a frame binned on the CPU (or with the single-cluster
    // grid, binner null).
    void set_cpu_stats(uint64_t frame_index, const Light_cluster_binner* binner);

private:
    friend class Light_cluster_gpu_binner;

    // Byte stride of the readback slots: a multiple of every
    // nonCoherentAtomSize, so a slot can be invalidated on its own.
    static constexpr std::size_t readback_slot_stride{256};

    class Readback_slot
    {
    public:
        Light_cluster_stats stats{};
        bool                pending{false};
    };

    std::unique_ptr<erhe::graphics::Buffer>    m_readback_buffer;
    std::vector<Readback_slot>                 m_readback_slots;
    // "Light cluster binning <view label>"; the timer keeps a pointer to it.
    std::string                                m_gpu_timer_label;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_gpu_timer;
    Light_cluster_stats                        m_stats{};
};

// How Light_buffer::write_view() bins one view. gpu needs all of gpu_binner
// (supported), command_buffer (the frame's, outside any render pass) and
// view_state; otherwise the view is binned on the CPU. view_state, when
// given, receives the stats in both modes; label names the view in the
// check.
class Light_view_binning
{
public:
    Light_cluster_binning           binning       {Light_cluster_binning::cpu};
    Light_cluster_gpu_binner*       gpu_binner    {nullptr};
    erhe::graphics::Command_buffer* command_buffer{nullptr};
    Light_cluster_view_state*       view_state    {nullptr};
    std::string_view                label         {};
};

// One cluster whose GPU list differs from the CPU reference.
class Light_cluster_mismatch
{
public:
    uint32_t                cluster{0};
    std::array<uint32_t, 4> cpu_counts{};
    std::array<uint32_t, 4> gpu_counts{};
    std::vector<uint32_t>   cpu_slots;
    std::vector<uint32_t>   gpu_slots;
    bool                    gpu_offset_wrong{false};
};

// One binned view of the checked frame.
class Light_cluster_view_check
{
public:
    std::string                         label;
    glm::uvec4                          counts{0u};
    std::size_t                         cluster_count         {0};
    std::size_t                         binned_light_count    {0};
    std::size_t                         listed_count          {0}; // list entries over all clusters (CPU)
    std::size_t                         mismatch_count        {0}; // clusters
    std::size_t                         cpu_overflow_count    {0};
    std::size_t                         gpu_overflow_count    {0};
    std::size_t                         cpu_max_list_length   {0};
    std::size_t                         gpu_max_list_length   {0};
    std::vector<Light_cluster_mismatch> mismatches; // the first few
};

// get_light_cluster_check (V2): the GPU lists of every view binned on the GPU
// in one frame against the CPU binner's lists of the same inputs.
class Light_cluster_check
{
public:
    uint64_t                              frame_index   {0};
    std::size_t                           mismatch_count{0}; // clusters, all views, plus counter disagreements
    std::vector<Light_cluster_view_check> views;
};

enum class Light_cluster_check_state : unsigned int
{
    idle     = 0,
    pending  = 1,
    complete = 2
};

class Light_cluster_check_data;

// The GPU light binner (doc/plans/clustered_lights.md C4): the compute
// pipeline of res/shaders/light_cluster_bin.comp, its interface blocks and the
// ring buffer clients of its inputs, overflow counters and readbacks. One per
// device, shared by every binned view.
class Light_cluster_gpu_binner
{
public:
    static constexpr unsigned int input_binding_point   {0};
    static constexpr unsigned int output_binding_point  {1};
    static constexpr unsigned int counters_binding_point{2};
    static constexpr uint32_t     workgroup_size        {64};
    // Overflowing cluster count, longest list, two reserved uints.
    static constexpr std::size_t  counter_bytes         {4 * sizeof(uint32_t)};

    Light_cluster_gpu_binner(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path);
    ~Light_cluster_gpu_binner() noexcept;
    Light_cluster_gpu_binner(const Light_cluster_gpu_binner&)            = delete;
    Light_cluster_gpu_binner& operator=(const Light_cluster_gpu_binner&) = delete;

    // The pipeline built (every backend with compute storage blocks).
    [[nodiscard]] auto is_supported() const -> bool;

    // Render phase, outside any render pass: records the binning of
    // binner's last prepare() into clusters (byte_count >= binner.
    // get_fixed_byte_count(), at byte_offset of the buffer), the barrier
    // before the frame's fragment reads, and the copy of the overflow
    // counters into view_state's readback of frame_index. A requested check
    // also bins the same inputs on the CPU (binner.bin_prepared()) and reads
    // the lists back.
    void dispatch(
        erhe::graphics::Command_buffer& command_buffer,
        Light_cluster_binner&           binner,
        const erhe::graphics::Buffer&   clusters,
        std::size_t                     byte_offset,
        std::size_t                     byte_count,
        Light_cluster_view_state&       view_state,
        uint64_t                        frame_index,
        std::string_view                label
    );

    // Main thread: requests the check of the first frame binned on the GPU
    // whose render phase starts after the request (a new request replaces an
    // earlier one), and polls it. Every view binned on the GPU in that frame
    // is checked. The render phase of the frame the main thread prepares, or
    // of an earlier one, may be running already: the checked frame is at
    // least the main thread's frame index + 1.
    void request_check();
    [[nodiscard]] auto poll_check(Light_cluster_check& out) -> Light_cluster_check_state;

private:
    erhe::graphics::Device&                                   m_graphics_device;
    erhe::graphics::Shader_resource                           m_input_block;
    erhe::graphics::Shader_resource                           m_output_block;
    erhe::graphics::Shader_resource                           m_counters_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_pipeline;
    erhe::graphics::Ring_buffer_client                        m_input_buffer;
    erhe::graphics::Ring_buffer_client                        m_counter_buffer;
    erhe::graphics::Ring_buffer_client                        m_readback_buffer; // the check's readbacks
    std::shared_ptr<Light_cluster_check_data>                 m_check;
};

} // namespace erhe::scene_renderer
