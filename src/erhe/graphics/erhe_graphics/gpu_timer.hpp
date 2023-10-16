#if 0
#pragma once

#include "erhe_graphics/gl_objects.hpp"

#include <array>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace erhe::graphics
{

class Gpu_timer
{
public:
    explicit Gpu_timer(const char* label);
    ~Gpu_timer() noexcept;

    Gpu_timer     (const Gpu_timer&) = delete;
    auto operator=(const Gpu_timer&) = delete;
    Gpu_timer     (Gpu_timer&&)      = delete;
    auto operator=(Gpu_timer&&)      = delete;

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
    static auto all_gpu_timers () -> std::vector<Gpu_timer*>;

private:
    class Query
    {
    public:
        std::optional<Gl_query> query_object{};
        bool                    begin       {false};
        bool                    end         {false};
        bool                    pending     {false};
    };

    static constexpr std::size_t   s_count = 4;
    static std::mutex              s_mutex;
    static std::vector<Gpu_timer*> s_all_gpu_timers;
    static Gpu_timer*              s_active_timer;
    static std::size_t             s_index;

    std::array<Query, s_count> m_queries;
    std::thread::id            m_owner_thread;
    uint64_t                   m_last_result{0};
    const char*                m_label{nullptr};
};

class Scoped_gpu_timer
{
public:
    explicit Scoped_gpu_timer(Gpu_timer& timer);
    ~Scoped_gpu_timer() noexcept;

private:
    Gpu_timer& m_timer;
};

} // namespace erhe::graphics
#endif
