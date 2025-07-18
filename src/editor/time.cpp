#include "time.hpp"

#include "app_message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

void Time::prepare_update()
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    int64_t host_system_frame_start_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t host_system_frame_duration_ns   = host_system_frame_start_time_ns - m_host_system_last_frame_start_time;
    int64_t simulation_frame_duration_ns    = host_system_frame_duration_ns;

    m_host_system_last_frame_duration_ns = host_system_frame_duration_ns;

    // Cap frame duration to 25ms. This causes time dilation
    if (simulation_frame_duration_ns > 25'000'000'000) {
        simulation_frame_duration_ns = 25'000'000'000;
    }

    //m_current_time = new_time;
    m_simulation_time_accumulator += simulation_frame_duration_ns;
    m_simulation_dt_ns = 1'000'000'000 / 240;
    const float simulation_dt_s = 1.0f / 240.0f;
    m_this_frame_fixed_steps.clear();
    uint64_t subframe = 0;
    while (m_simulation_time_accumulator >= m_simulation_dt_ns) {
        m_this_frame_fixed_steps.push_back(
            Time_context{
                .simulation_dt_s     = simulation_dt_s,
                .simulation_dt_ns    = m_simulation_dt_ns,
                .simulation_time_ns  = m_simulation_time_ns,
                .frame_number        = m_frame_number,
                .subframe            = subframe
            }
        );
        m_simulation_time_accumulator -= m_simulation_dt_ns;
        m_simulation_time_ns += m_simulation_dt_ns;
        ++subframe;
    }
    m_host_system_time_ns = m_host_system_last_frame_start_time;
    if (subframe > 0) {
        int64_t host_system_dt_ns = host_system_frame_duration_ns / subframe;
        double  host_system_dt_s_ = static_cast<double>(host_system_frame_duration_ns) / static_cast<double>(subframe * 1'000'000'000.0);
        float   host_system_dt_s  = static_cast<float>(host_system_dt_s_);
        for (Time_context& time_context : m_this_frame_fixed_steps) {
            time_context.host_system_dt_s    = host_system_dt_s;
            time_context.host_system_dt_ns   = host_system_dt_ns,
            time_context.host_system_time_ns = m_host_system_time_ns;
            m_host_system_time_ns += host_system_dt_ns;
        }
    }

    m_host_system_last_frame_start_time = host_system_frame_start_time_ns;
}

void Time::for_each_fixed_step(std::function<void(const Time_context&)> callback)
{
    for (const Time_context& time_context : m_this_frame_fixed_steps) {
        callback(time_context);
    }
}

auto Time::get_frame_number() const -> uint64_t
{
    return m_frame_number;
}

auto Time::get_simulation_time_ns() const -> int64_t
{
    return m_simulation_time_ns;
}

auto Time::get_host_system_time_ns() const -> int64_t
{
    return m_host_system_time_ns;
}

auto Time::get_host_system_last_frame_duration_ns() const -> int64_t
{
    return m_host_system_last_frame_duration_ns;
}

void Time::begin_transform_animation(
    std::shared_ptr<erhe::scene::Node> node,
    erhe::scene::Transform             parent_from_node_before,
    erhe::scene::Transform             parent_from_node_after,
    const float                        time_duration
)
{
    int64_t time_duration_ns = static_cast<int64_t>(std::round(static_cast<double>(time_duration) * 1e9));
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    m_transform_animations.emplace_back(
        node, parent_from_node_before, parent_from_node_after, time_duration_ns, m_host_system_last_frame_start_time
    );
}

void Time::finish_all_transform_animations(App_message_bus& app_message_bus)
{
    for (Transform_animation_entry& entry : m_transform_animations) {
        entry.node->set_parent_from_node(entry.parent_from_node_after);
        app_message_bus.send_message(
            App_message{
                .update_flags = Message_flag_bit::c_flag_bit_node_touched_operation_stack,
                .node         = entry.node.get()
            }
        );
    }
}

void Time::update_transform_animations(App_message_bus& app_message_bus)
{
    // Finish completed animations
    m_transform_animations.erase(
        std::remove_if(
            m_transform_animations.begin(),
            m_transform_animations.end(),
            [this, &app_message_bus](const Transform_animation_entry& entry) {
                const int64_t time_position = m_host_system_last_frame_start_time - entry.start_time_ns;
                if (time_position >= entry.time_duration_ns) {
                    entry.node->set_parent_from_node(entry.parent_from_node_after);
                    app_message_bus.send_message(
                        App_message{
                            .update_flags = Message_flag_bit::c_flag_bit_node_touched_operation_stack,
                            .node         = entry.node.get()
                        }
                    );
                    return true;
                }
                return false;
            }
        ),
        m_transform_animations.end()
    );

    for (Transform_animation_entry& entry : m_transform_animations) {
        const int64_t time_position = m_host_system_last_frame_start_time - entry.start_time_ns;
        ERHE_VERIFY(time_position < entry.time_duration_ns);
        const float t = static_cast<float>(
            static_cast<double>(time_position) / static_cast<double>(entry.time_duration_ns)
        );
        ERHE_VERIFY(t >= 0.0f);
        const float t_ = glm::smoothstep(0.0f, 1.0f, t);
        const erhe::scene::Trs_transform t0{entry.parent_from_node_before.get_matrix()};
        const erhe::scene::Trs_transform t1{entry.parent_from_node_after.get_matrix()};
        const erhe::scene::Trs_transform transform = erhe::scene::interpolate(t0, t1, t_);
        entry.node->set_parent_from_node(transform);
        app_message_bus.send_message(
            App_message{
                .update_flags = Message_flag_bit::c_flag_bit_node_touched_operation_stack,
                .node         = entry.node.get()
            }
        );
    }
}


}  // namespace editor

