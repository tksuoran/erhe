#pragma once

#include "erhe_profile/profile.hpp"
#include "erhe_scene/transform.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::scene { class Node; }

namespace editor {

class Time;
class App_message_bus;

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

class Transform_animation_entry
{
public:
    std::shared_ptr<erhe::scene::Node> node;
    erhe::scene::Transform             parent_from_node_before;
    erhe::scene::Transform             parent_from_node_after;
    int64_t                            time_duration_ns;
    int64_t                            start_time_ns;
};

class Time
{
public:
    [[nodiscard]] auto get_simulation_time_ns                () const -> int64_t;
    [[nodiscard]] auto get_host_system_time_ns               () const -> int64_t;
    [[nodiscard]] auto get_host_system_last_frame_duration_ns() const -> int64_t;
    [[nodiscard]] auto get_frame_number                      () const -> uint64_t;
    void prepare_update     ();
    void for_each_fixed_step(std::function<void(const Time_context&)> callback);

    void update_transform_animations(App_message_bus& app_message_bus);
    void finish_all_transform_animations(App_message_bus& app_message_bus);
    void begin_transform_animation(
        std::shared_ptr<erhe::scene::Node> node,
        erhe::scene::Transform             parent_from_node_before,
        erhe::scene::Transform             parent_from_node_after,
        float                              time_duration
    );

private:
    int64_t  m_host_system_last_frame_start_time {0};
    int64_t  m_host_system_time_ns               {0};
    int64_t  m_host_system_last_frame_duration_ns{0};
    int64_t  m_simulation_time_accumulator       {0};
    int64_t  m_simulation_dt_ns                  {0};
    int64_t  m_simulation_time_ns                {0};
    uint64_t m_frame_number                      {0};

    ERHE_PROFILE_MUTEX(std::mutex,         m_mutex);
    std::vector<Time_context>              m_this_frame_fixed_steps;
    std::vector<Transform_animation_entry> m_transform_animations;
};

}
