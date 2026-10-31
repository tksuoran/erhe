#pragma once

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {
    class Bind_group_layout;
    class Buffer;
    class Command_buffer;
    class Compute_pipeline;
    class Device;
    class Reloadable_shader_stages;
}

namespace erhe::scene_renderer {

// The overdraw statistics of one view and frame
// (doc/plans/overdraw_visualization.md O5): a histogram of the per-pixel
// fragment shader invocation counts of the overdraw variants
// (res/shaders/erhe_overdraw.glsl) and their totals.
class Overdraw_statistics
{
public:
    // Counts 0 .. histogram_last_bin - 1 have their own bin; the last bin
    // holds every larger count.
    static constexpr uint32_t    histogram_last_bin {64};
    static constexpr std::size_t histogram_bin_count{histogram_last_bin + 1};

    bool                                          valid              {false};
    uint64_t                                      frame_index        {0};
    // How the frame counted (shaded or rasterized).
    Overdraw_count                                overdraw           {Overdraw_count::off};
    uint32_t                                      width              {0};
    uint32_t                                      height             {0};
    uint64_t                                      invocation_total   {0};
    uint32_t                                      covered_pixel_count{0};
    uint32_t                                      max_count          {0};
    std::array<uint32_t, histogram_bin_count>     histogram          {};
};

// The counting state of one view, owned by the view and used by its render
// phase: the per-pixel count buffer (one serves every frame in flight: the
// frames execute in order on one queue, and each frame clears it before its
// render pass), a persistent host-readable buffer with one slot per frame in
// flight for the reduced statistics (the frame F reduction copies into slot
// F % frames in flight, decoded by the next frame of the view that uses the
// slot, after F retired), and the latest decoded statistics.
class Overdraw_view_state
{
public:
    Overdraw_view_state();
    ~Overdraw_view_state() noexcept;
    Overdraw_view_state(const Overdraw_view_state&)            = delete;
    Overdraw_view_state& operator=(const Overdraw_view_state&) = delete;

    // The newest retired frame's statistics (not valid before one retired).
    // Any thread.
    [[nodiscard]] auto get_statistics() const -> Overdraw_statistics;

    // Render phase: the count buffer of frame_index, which begin_frame()
    // prepared for it; null when the view did not count in that frame.
    [[nodiscard]] auto get_count_buffer(uint64_t frame_index) const -> const erhe::graphics::Buffer*;

    // Render phase, before the view's render pass of frame_index, outside
    // any render pass: decodes the readback of the frame that used this
    // frame's slot (it retired), sizes the count buffer to width x height
    // and clears it for the frame's counting draws.
    void begin_frame(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& command_buffer,
        uint64_t                        frame_index,
        Overdraw_count                  overdraw,
        uint32_t                        width,
        uint32_t                        height
    );

    // Render phase: drops the buffers and the statistics (the mode turned
    // off). Buffers in use by frames in flight are destroyed when those
    // complete.
    void release();

private:
    friend class Overdraw_counter;

    // Byte stride of the readback slots: a multiple of every
    // nonCoherentAtomSize, so a slot can be invalidated on its own, and room
    // for the result (4 + histogram_bin_count uints).
    static constexpr std::size_t readback_slot_stride{512};

    class Readback_slot
    {
    public:
        uint64_t       frame_index{0};
        Overdraw_count overdraw   {Overdraw_count::off};
        uint32_t width      {0};
        uint32_t height     {0};
        bool     pending    {false};
    };

    void retire(uint64_t frame_index);

    std::unique_ptr<erhe::graphics::Buffer> m_count_buffer;
    uint64_t                                m_count_frame_index{0};
    Overdraw_count                          m_overdraw{Overdraw_count::off};
    uint32_t                                m_width {0};
    uint32_t                                m_height{0};
    std::unique_ptr<erhe::graphics::Buffer> m_readback_buffer;
    std::vector<Readback_slot>              m_readback_slots;
    mutable std::mutex                      m_statistics_mutex;
    Overdraw_statistics                     m_statistics{};
};

// The reduction of the views' overdraw counts into Overdraw_statistics
// (res/shaders/overdraw_reduce.comp), shared by the views. Supported on
// devices with fragment stage buffer atomics
// (Device_info::fragment_stores_and_atomics), which the counting variants
// need too, and compute shaders.
class Overdraw_counter
{
public:
    Overdraw_counter(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path);
    ~Overdraw_counter() noexcept;
    Overdraw_counter(const Overdraw_counter&)            = delete;
    Overdraw_counter& operator=(const Overdraw_counter&) = delete;

    [[nodiscard]] auto is_supported() const -> bool;

    // Render phase, after the view's render pass of frame_index (whose
    // counting draws wrote view_state's count buffer, prepared by
    // view_state.begin_frame() of the same frame), outside any render pass:
    // reduces the counts and copies the result to the frame's readback
    // slot.
    void reduce(erhe::graphics::Command_buffer& command_buffer, Overdraw_view_state& view_state, uint64_t frame_index);

private:
    static constexpr unsigned int input_binding_point {0};
    static constexpr unsigned int counts_binding_point{1};
    static constexpr unsigned int result_binding_point{2};
    static constexpr unsigned int workgroup_size      {256};
    // Workgroups of one reduction: each walks a grid-strided share of the
    // pixels, so the result's global atomics stay per workgroup.
    static constexpr std::size_t  max_group_count     {256};

    erhe::graphics::Device&                                   m_graphics_device;
    erhe::graphics::Shader_resource                           m_input_block;
    erhe::graphics::Shader_resource                           m_counts_block;
    erhe::graphics::Shader_resource                           m_result_block;
    erhe::graphics::Ring_buffer_client                        m_input_buffer;
    erhe::graphics::Ring_buffer_client                        m_result_buffer;
    std::unique_ptr<erhe::graphics::Bind_group_layout>        m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Reloadable_shader_stages> m_shader_stages;
    std::unique_ptr<erhe::graphics::Compute_pipeline>         m_pipeline;
};

} // namespace erhe::scene_renderer
