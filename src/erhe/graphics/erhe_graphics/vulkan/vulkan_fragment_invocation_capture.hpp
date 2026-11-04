#pragma once

#include "erhe_graphics/fragment_invocation_capture.hpp"

#include <volk.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe::graphics {

class Device_impl;
class Shader_stages;

// Fragment invocations per pipeline bind range of one frame, with pipeline
// statistics queries (doc/plans/shader_cost.md A1). request() runs on the
// main thread; the bind and pass-end hooks on the thread recording the frame;
// the readback in the frame's completion handler.
class Fragment_invocation_capture_impl final
{
public:
    Fragment_invocation_capture_impl(Device_impl& device_impl, VkDevice vk_device);
    ~Fragment_invocation_capture_impl() noexcept;
    Fragment_invocation_capture_impl(const Fragment_invocation_capture_impl&) = delete;
    void operator=                  (const Fragment_invocation_capture_impl&) = delete;

    // Arms the first frame with id >= first_frame_id that binds a pipeline.
    [[nodiscard]] auto request(uint64_t first_frame_id) -> bool;
    [[nodiscard]] auto take   () -> std::optional<Fragment_invocation_capture>;

    // view_count: the views of the active render pass (1 without multiview).
    void on_pipeline_bound  (uint64_t frame_id, VkCommandBuffer command_buffer, uint32_t view_count, const Shader_stages* shader_stages, std::string_view pipeline_label);
    void on_render_pass_end (VkCommandBuffer command_buffer);

private:
    static constexpr uint32_t s_query_capacity = 16384;

    enum class State : unsigned int { idle, armed, recording, complete };

    class Range
    {
    public:
        uint32_t                  first_query;
        Fragment_invocation_range info;
    };

    void end_open_query(VkCommandBuffer command_buffer); // m_mutex held
    void read_results  ();

    Device_impl&                                         m_device_impl;
    VkDevice                                             m_vk_device;
    VkQueryPool                                          m_query_pool{VK_NULL_HANDLE};
    std::mutex                                           m_mutex;
    State                                                m_state         {State::idle};
    uint64_t                                             m_first_frame_id{0};
    uint64_t                                             m_frame_id      {0};
    uint32_t                                             m_next_query    {0};
    uint32_t                                             m_dropped_ranges{0};
    std::vector<Range>                                   m_ranges;
    std::vector<std::pair<VkCommandBuffer, std::size_t>> m_open_ranges; // index into m_ranges
    Fragment_invocation_capture                          m_result;
};

} // namespace erhe::graphics
