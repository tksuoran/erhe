#include "animation/animation_player.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"

#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <algorithm>

namespace editor {

Animation_player::Animation_player(App_context& context)
    : m_context{context}
{
}

void Animation_player::update(const float dt_s)
{
    if (!m_animation) {
        return;
    }

    if (m_playing) {
        const float length = m_end_time - m_start_time;
        if (length <= 0.0f) {
            m_time = m_start_time;
            m_playing = false;
        } else {
            m_time += dt_s * m_speed;
            if (m_looping) {
                while (m_time < m_start_time) {
                    m_time += length;
                }
                while (m_time > m_end_time) {
                    m_time -= length;
                }
            } else {
                if (m_time <= m_start_time) {
                    m_time = m_start_time;
                    m_playing = false;
                }
                if (m_time >= m_end_time) {
                    m_time = m_end_time;
                    m_playing = false;
                }
            }
        }
        m_apply_requested = true;
    }

    if (m_apply_requested) {
        m_apply_requested = false;
        apply();
    }
}

void Animation_player::set_animation(const std::shared_ptr<erhe::scene::Animation>& animation)
{
    if (m_animation == animation) {
        return;
    }
    m_animation = animation;
    m_playing   = false;
    refresh_time_range();
    // Keep the playhead where it is (the timeline range is independent of the
    // animation): Create Key on a fresh animation must not jump time back to
    // the start.
    m_time = std::clamp(m_time, m_start_time, m_end_time);
    if (m_animation) {
        m_apply_requested = true;
    }
}

auto Animation_player::get_animation() const -> const std::shared_ptr<erhe::scene::Animation>&
{
    return m_animation;
}

void Animation_player::play()
{
    if (!m_animation) {
        return;
    }
    if (!m_looping && (m_time >= m_end_time)) {
        m_time = m_start_time;
    }
    m_playing = true;
}

void Animation_player::pause()
{
    m_playing = false;
}

void Animation_player::stop()
{
    m_playing = false;
    m_time    = m_start_time;
    m_apply_requested = true;
}

void Animation_player::seek(const float time)
{
    m_time = std::clamp(time, m_start_time, m_end_time);
    m_apply_requested = true;
}

void Animation_player::set_start_time(const float time)
{
    m_start_time = time;
    if (m_end_time < m_start_time) {
        m_end_time = m_start_time;
    }
    m_time = std::clamp(m_time, m_start_time, m_end_time);
    m_apply_requested = true;
}

void Animation_player::set_end_time(const float time)
{
    m_end_time = time;
    if (m_start_time > m_end_time) {
        m_start_time = m_end_time;
    }
    m_time = std::clamp(m_time, m_start_time, m_end_time);
    m_apply_requested = true;
}

void Animation_player::set_speed(const float speed)
{
    m_speed = speed;
}

void Animation_player::set_looping(const bool looping)
{
    m_looping = looping;
}

auto Animation_player::is_playing() const -> bool
{
    return m_playing;
}

auto Animation_player::is_looping() const -> bool
{
    return m_looping;
}

auto Animation_player::get_speed() const -> float
{
    return m_speed;
}

auto Animation_player::get_time() const -> float
{
    return m_time;
}

auto Animation_player::get_start_time() const -> float
{
    return m_start_time;
}

auto Animation_player::get_end_time() const -> float
{
    return m_end_time;
}

void Animation_player::set_autokey_mode(const Autokey_mode mode)
{
    m_autokey_mode = mode;
}

auto Animation_player::get_autokey_mode() const -> Autokey_mode
{
    return m_autokey_mode;
}

void Animation_player::on_animation_edited(const std::shared_ptr<erhe::scene::Animation>& animation)
{
    if (!m_animation || (m_animation != animation)) {
        return;
    }
    refresh_time_range();
    m_time = std::clamp(m_time, m_start_time, m_end_time);
    m_apply_requested = true;
}

void Animation_player::refresh_time_range()
{
    // The timeline range is user-editable; animation keys only ever extend it
    // so that every key stays visible and reachable. Never shrink it here -
    // shrinking is a user action (set_start_time() / set_end_time()).
    if (!m_animation || m_animation->channels.empty()) {
        return;
    }
    const float first_time = m_animation->get_first_time();
    const float last_time  = m_animation->get_last_time();
    if (last_time < first_time) {
        return;
    }
    m_start_time = std::min(m_start_time, first_time);
    m_end_time   = std::max(m_end_time, last_time);
}

void Animation_player::apply()
{
    if (!m_animation || m_animation->channels.empty()) {
        return;
    }

    m_animation->apply(m_time);

    m_context.app_message_bus->animation_update.send_message(Animation_update_message{});

    // All channel targets are expected to live in the same scene; update the
    // node transforms of every distinct scene encountered to be safe.
    erhe::scene::Scene* last_scene = nullptr;
    for (const erhe::scene::Animation_channel& channel : m_animation->channels) {
        if (!channel.target) {
            continue;
        }
        erhe::scene::Scene* scene = channel.target->get_scene();
        if ((scene != nullptr) && (scene != last_scene)) {
            scene->update_node_transforms();
            last_scene = scene;
        }
    }
}

}
