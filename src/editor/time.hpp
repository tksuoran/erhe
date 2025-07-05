#pragma once

#include "erhe_profile/profile.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace editor {

class Time;

class Time_context
{
public:
    float    simulation_dt_s    {0.0};
    int64_t  simulation_dt_ns   {0};
    int64_t  simulation_time_ns {0};
    float    host_system_dt_s   {0.0};
    int64_t  host_system_dt_ns  {0};
    int64_t  host_system_time_ns{0};
    uint64_t frame_number       {0};
    uint64_t subframe           {0};
};

class Time
{
public:
    [[nodiscard]] auto get_simulation_time_ns () const -> int64_t;
    [[nodiscard]] auto get_host_system_time_ns() const -> int64_t;
    [[nodiscard]] auto get_frame_number       () const -> uint64_t;
    void prepare_update     ();
    void for_each_fixed_step(std::function<void(const Time_context&)> callback);

private:
    int64_t  m_host_system_last_frame_start_time{0};
    int64_t  m_host_system_time_ns              {0};
    int64_t  m_simulation_time_accumulator      {0};
    int64_t  m_simulation_dt_ns                 {0};
    int64_t  m_simulation_time_ns               {0};
    uint64_t m_frame_number                     {0};

    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Time_context>      m_this_frame_fixed_steps;
};

}
