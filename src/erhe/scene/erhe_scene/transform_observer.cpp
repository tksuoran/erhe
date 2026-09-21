#include "erhe_scene/transform_observer.hpp"

#include <algorithm>

namespace erhe::scene {

auto Transform_observer_list::next_id() -> uint64_t
{
    return m_next_id++;
}

void Transform_observer_list::add(const uint64_t id, Transform_observer_callback callback)
{
    m_entries.push_back(Entry{id, std::move(callback)});
}

void Transform_observer_list::remove(const uint64_t id)
{
    if (m_notify_depth > 0) {
        // Iterating: mark the entry released and compact once notify() is done.
        for (Entry& entry : m_entries) {
            if (entry.id == id) {
                entry.id       = 0;
                entry.callback = nullptr;
                m_has_released = true;
                return;
            }
        }
        return;
    }
    m_entries.remove_if([id](const Entry& entry) { return entry.id == id; });
}

void Transform_observer_list::notify(Xformable& node)
{
    // Over the entries present when the notification started, so an observer
    // a callback adds is first called by the next one. No allocation happens
    // here: a transform update of a prim with observers costs the calls alone.
    std::size_t remaining = m_entries.size();
    ++m_notify_depth;
    for (const Entry& entry : m_entries) {
        if (remaining == 0) {
            break;
        }
        --remaining;
        if (entry.id != 0) {
            entry.callback(node);
        }
    }
    --m_notify_depth;
    if ((m_notify_depth == 0) && m_has_released) {
        m_entries.remove_if([](const Entry& entry) { return entry.id == 0; });
        m_has_released = false;
    }
}

Transform_observer_token::Transform_observer_token(std::weak_ptr<Transform_observer_list> list, const uint64_t id)
    : m_list{std::move(list)}
    , m_id  {id}
{
}

Transform_observer_token::Transform_observer_token(Transform_observer_token&& other) noexcept
    : m_list{std::move(other.m_list)}
    , m_id  {other.m_id}
{
    other.m_id = 0;
}

Transform_observer_token& Transform_observer_token::operator=(Transform_observer_token&& other) noexcept
{
    if (this != &other) {
        release();
        m_list     = std::move(other.m_list);
        m_id       = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

Transform_observer_token::~Transform_observer_token() noexcept
{
    release();
}

auto Transform_observer_token::is_active() const -> bool
{
    return (m_id != 0) && !m_list.expired();
}

void Transform_observer_token::release()
{
    if (m_id != 0) {
        const std::shared_ptr<Transform_observer_list> list = m_list.lock();
        if (list) {
            list->remove(m_id);
        }
    }
    m_list.reset();
    m_id = 0;
}

} // namespace erhe::scene
