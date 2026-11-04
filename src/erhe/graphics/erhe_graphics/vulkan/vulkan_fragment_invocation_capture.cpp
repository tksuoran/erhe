#include "erhe_graphics/vulkan/vulkan_fragment_invocation_capture.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::graphics {

Fragment_invocation_capture_impl::Fragment_invocation_capture_impl(Device_impl& device_impl, const VkDevice vk_device)
    : m_device_impl{device_impl}
    , m_vk_device  {vk_device}
{
    const VkQueryPoolCreateInfo create_info{
        .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS,
        .queryCount         = s_query_capacity,
        .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
    };
    const VkResult result = vkCreateQueryPool(m_vk_device, &create_info, nullptr, &m_query_pool);
    if (result != VK_SUCCESS) {
        log_startup->error("vkCreateQueryPool() for fragment invocation capture failed with {} {}", static_cast<int32_t>(result), c_str(result));
        m_query_pool = VK_NULL_HANDLE;
    }
}

Fragment_invocation_capture_impl::~Fragment_invocation_capture_impl() noexcept
{
    if (m_query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_vk_device, m_query_pool, nullptr);
    }
}

auto Fragment_invocation_capture_impl::request(const uint64_t first_frame_id) -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if ((m_query_pool == VK_NULL_HANDLE) || (m_state == State::armed) || (m_state == State::recording)) {
        return false;
    }
    // No query of the pool is in use outside armed / recording.
    vkResetQueryPool(m_vk_device, m_query_pool, 0, s_query_capacity);
    m_state          = State::armed;
    m_first_frame_id = first_frame_id;
    m_frame_id       = 0;
    m_next_query     = 0;
    m_dropped_ranges = 0;
    m_ranges.clear();
    m_open_ranges.clear();
    m_result = Fragment_invocation_capture{};
    return true;
}

auto Fragment_invocation_capture_impl::take() -> std::optional<Fragment_invocation_capture>
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_state != State::complete) {
        return std::nullopt;
    }
    m_state = State::idle;
    return std::move(m_result);
}

void Fragment_invocation_capture_impl::end_open_query(const VkCommandBuffer command_buffer)
{
    const auto it = std::find_if(m_open_ranges.begin(), m_open_ranges.end(), [command_buffer](const auto& entry) { return entry.first == command_buffer; });
    if (it == m_open_ranges.end()) {
        return;
    }
    vkCmdEndQuery(command_buffer, m_query_pool, m_ranges[it->second].first_query);
    m_open_ranges.erase(it);
}

void Fragment_invocation_capture_impl::on_pipeline_bound(
    const uint64_t             frame_id,
    const VkCommandBuffer      command_buffer,
    const uint32_t             view_count,
    const Shader_stages* const shader_stages,
    const std::string_view     pipeline_label
)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if ((m_state == State::armed) && (frame_id >= m_first_frame_id)) {
        m_state    = State::recording;
        m_frame_id = frame_id;
        // The recording thread's frame is frame_id: the handler runs when it completes.
        m_device_impl.add_completion_handler([this](Device_impl&) { read_results(); });
    }
    if ((m_state != State::recording) || (frame_id != m_frame_id)) {
        return;
    }
    end_open_query(command_buffer);
    if (m_next_query + view_count > s_query_capacity) {
        ++m_dropped_ranges;
        return;
    }
    Range range{
        .first_query = m_next_query,
        .info = Fragment_invocation_range{
            .pipeline    = std::string{pipeline_label},
            .program     = (shader_stages != nullptr) ? shader_stages->name() : std::string{},
            .defines     = (shader_stages != nullptr) ? shader_stages->get_variant_defines() : std::vector<std::pair<std::string, std::string>>{},
            .view_count  = view_count,
            .invocations = 0
        }
    };
    vkCmdBeginQuery(command_buffer, m_query_pool, m_next_query, 0);
    m_next_query += view_count; // a multiview pass uses one query per view
    m_open_ranges.emplace_back(command_buffer, m_ranges.size());
    m_ranges.push_back(std::move(range));
}

void Fragment_invocation_capture_impl::on_render_pass_end(const VkCommandBuffer command_buffer)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_state != State::recording) {
        return;
    }
    end_open_query(command_buffer);
}

void Fragment_invocation_capture_impl::read_results()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_state != State::recording) {
        return;
    }
    ERHE_VERIFY(m_open_ranges.empty());
    std::vector<uint64_t> values(m_next_query, 0); // debug capture: allocation is fine
    if (m_next_query > 0) {
        const VkResult result = vkGetQueryPoolResults(
            m_vk_device, m_query_pool, 0, m_next_query,
            values.size() * sizeof(uint64_t), values.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );
        if (result != VK_SUCCESS) {
            log_vulkan->error("vkGetQueryPoolResults() for fragment invocation capture failed with {} {}", static_cast<int32_t>(result), c_str(result));
        }
    }
    m_result.frame_id       = m_frame_id;
    m_result.dropped_ranges = m_dropped_ranges;
    m_result.ranges.reserve(m_ranges.size());
    for (Range& range : m_ranges) {
        for (uint32_t view = 0; view < range.info.view_count; ++view) {
            range.info.invocations += values[range.first_query + view];
        }
        m_result.ranges.push_back(std::move(range.info));
    }
    m_ranges.clear();
    m_state = State::complete;
}

} // namespace erhe::graphics
