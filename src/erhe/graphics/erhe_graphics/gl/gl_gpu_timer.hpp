#pragma once

#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_profile/profile.hpp"

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
    class Query
    {
    public:
        std::optional<Gl_query> query_object{};
        bool                    begin       {false};
        bool                    end         {false};
        bool                    pending     {false};
    };

    static constexpr std::size_t                      s_count = 4;
    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Gpu_timer_impl*>               s_all_gpu_timers;
    static Gpu_timer_impl*                            s_active_timer;
    static std::size_t                                s_index;

    Device&                    m_device;
    std::array<Query, s_count> m_queries;
    std::thread::id            m_owner_thread;
    uint64_t                   m_last_result{0};
    const char*                m_label{nullptr};
};


} // namespace erhe::graphics
