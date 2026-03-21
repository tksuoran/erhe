#pragma once

#include "erhe_profile/profile.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace erhe::message_bus {

enum class Dispatch_policy {
    sync_only,
    queue_only,
    both
};

// Forward declaration
template <typename Message_type, Dispatch_policy policy>
class Message_bus;

template <typename Message_type>
class Subscription
{
public:
    Subscription() = default;

    struct Detail
    {
        std::function<void(Message_type&)> callback;
        void invoke(Message_type& message) { callback(message); }
    };

private:
    template <typename M, Dispatch_policy P>
    friend class Message_bus;

    explicit Subscription(std::shared_ptr<Detail> detail) : m_detail{std::move(detail)} {}

    std::shared_ptr<Detail> m_detail;
};

//// TODO use something like https://github.com/TheWisp/signals instead
template <typename Message_type, Dispatch_policy policy = Dispatch_policy::both>
class Message_bus
{
public:
    template <typename Callback>
    auto subscribe(Callback&& callback) -> Subscription<Message_type>
    {
        auto detail = std::make_shared<typename Subscription<Message_type>::Detail>(
            typename Subscription<Message_type>::Detail{std::function<void(Message_type&)>{std::forward<Callback>(callback)}}
        );
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_receivers_mutex};
        m_receivers.push_back(detail);
        return Subscription<Message_type>{std::move(detail)};
    }

    void send_message(Message_type message) requires (policy == Dispatch_policy::sync_only || policy == Dispatch_policy::both)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_receivers_mutex};
        bool has_expired = false;
        for (auto& weak_detail : m_receivers) {
            auto detail = weak_detail.lock();
            if (!detail) {
                has_expired = true;
                continue;
            }
            detail->invoke(message);
        }
        if (has_expired) {
            prune_expired();
        }
    }

    void queue_message(Message_type message) requires (policy == Dispatch_policy::queue_only || policy == Dispatch_policy::both)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_queued_messages_mutex};
        m_queued_messages.push(message);
    }

    void update() requires (policy == Dispatch_policy::queue_only || policy == Dispatch_policy::both)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_1{m_receivers_mutex};
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_2{m_messages_mutex};
            bool has_expired = false;
            while (!m_messages.empty()) {
                for (auto& weak_detail : m_receivers) {
                    auto detail = weak_detail.lock();
                    if (!detail) {
                        has_expired = true;
                        continue;
                    }
                    detail->invoke(m_messages.front());
                }
                m_messages.pop();
            }
            if (has_expired) {
                prune_expired();
            }
            {
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_3{m_queued_messages_mutex};
                m_messages = std::move(m_queued_messages);
            }
        }
    }

private:
    void prune_expired()
    {
        m_receivers.erase(
            std::remove_if(
                m_receivers.begin(),
                m_receivers.end(),
                [](const auto& w) { return w.expired(); }
            ),
            m_receivers.end()
        );
    }

    ERHE_PROFILE_MUTEX(std::mutex, m_receivers_mutex);
    std::vector<std::weak_ptr<typename Subscription<Message_type>::Detail>> m_receivers;

    ERHE_PROFILE_MUTEX(std::mutex, m_messages_mutex);
    std::queue<Message_type>       m_messages;

    ERHE_PROFILE_MUTEX(std::mutex, m_queued_messages_mutex);
    std::queue<Message_type>       m_queued_messages;
};


} // namespace erhe::message_bus
