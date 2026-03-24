#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
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
    static std::mutex                     s_mutex;
    static std::vector<Gpu_timer_impl*>   s_all_gpu_timers;
    static Gpu_timer_impl*                s_active_timer;
    static std::size_t                    s_index;

    Device&     m_device;
    uint64_t    m_last_result{0};
    const char* m_label{nullptr};
};

} // namespace erhe::graphics
