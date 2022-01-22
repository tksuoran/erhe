#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <array>
#include <mutex>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Scoped_debug_group final
{
public:
    explicit Scoped_debug_group(const std::string_view debug_label);
    ~Scoped_debug_group();
};

class Gpu_timer
{
public:
    Gpu_timer();
    ~Gpu_timer();

    Gpu_timer     (const Gpu_timer&) = delete;
    auto operator=(const Gpu_timer&) = delete;
    Gpu_timer     (Gpu_timer&&)      = delete;
    auto operator=(Gpu_timer&&)      = delete;

    [[nodiscard]] auto last_result() const -> uint64_t;
    void begin ();
    void end   ();
    void read  ();
    void create();
    void reset ();

    static void on_thread_enter();
    static void on_thread_exit();
    static void end_frame();

private:
    class Query
    {
    public:
        std::optional<Gl_query> query_object{};
        bool                    begin       {false};
        bool                    end         {false};
        bool                    pending     {false};
    };

    static constexpr size_t        s_count = 4;
    static std::mutex              s_mutex;
    static std::vector<Gpu_timer*> s_all_gpu_timers;
    static Gpu_timer*              s_active_timer;
    static size_t                  s_index;

    std::array<Query, s_count> m_queries;
    std::thread::id            m_owner_thread;
    uint64_t                   m_last_result{0};
};

class Scoped_gpu_timer
{
public:
    Scoped_gpu_timer(Gpu_timer& timer);
    ~Scoped_gpu_timer();

private:
    Gpu_timer& m_timer;
};

} // namespace erhe::graphics
