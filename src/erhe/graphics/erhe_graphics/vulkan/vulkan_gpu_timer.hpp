#pragma once

#include "erhe_profile/profile.hpp"

#include "volk.h"

#include <array>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace erhe::graphics {

class Device;

class Gpu_timer_impl
{
public:
    Gpu_timer_impl(Device& device, const char* label);
    ~Gpu_timer_impl() noexcept;

    Gpu_timer_impl(const Gpu_timer_impl&) = delete;
    auto operator=(const Gpu_timer_impl&) = delete;
    Gpu_timer_impl(Gpu_timer_impl&&)      = delete;
    auto operator=(Gpu_timer_impl&&)      = delete;

    [[nodiscard]] auto last_result() -> uint64_t;
    [[nodiscard]] auto label      () const -> const char*;
    void begin ();
    void end   ();
    void read  ();
    void create();
    void reset ();

    static void on_thread_enter();
    static void on_thread_exit ();
    static void end_frame      ();
    static auto all_gpu_timers () -> std::vector<Gpu_timer_impl*>;

private:
    Device*       m_device        {nullptr};
    const char*   m_label         {""};
    uint64_t      m_last_result   {0};
    VkQueryPool   m_query_pool    {VK_NULL_HANDLE};
    bool          m_query_started {false};
    bool          m_query_ended   {false};
};

} // namespace erhe::graphics
