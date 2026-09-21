#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <memory>

namespace erhe::scene {

class Xformable;

// A callback invoked from Xformable::handle_transform_update, with the prim
// whose world transform changed.
using Transform_observer_callback = std::function<void(Xformable&)>;

// The subscriber list of one prim. Held by shared_ptr so a token that outlives
// the prim is a no-op rather than a dangling pointer.
class Transform_observer_list
{
public:
    void add   (uint64_t id, Transform_observer_callback callback);
    void remove(uint64_t id);
    void notify(Xformable& node);

    [[nodiscard]] auto next_id() -> uint64_t;

private:
    class Entry
    {
    public:
        uint64_t                    id;
        Transform_observer_callback callback;
    };

    // A list, not a vector: a callback may subscribe to the same prim, and a
    // list keeps the references the running notification holds valid. It also
    // allocates only when an observer is added, never per transform update.
    std::list<Entry> m_entries;
    uint64_t         m_next_id{1};
    // Removals made while notify() runs are deferred to the end of the
    // outermost notify(), so a callback is never destroyed while it runs.
    int              m_notify_depth{0};
    bool             m_has_released{false};
};

// Subscription to one prim's transform updates. Move-only; unsubscribes on
// destruction or release(). Safe when the prim dies first: the prim's
// destruction turns the token into a no-op.
//
// Rules for a callback:
//  - It may release its own token or add an observer to the same prim. An
//    observer added during a notification is first called by the NEXT
//    notification, and a token released during one is not called again.
//  - It must not write the transform of the prim it observes; that would
//    re-enter notify() (the observer that wants a write-back guards it, as
//    Frame_controller and Four_view do).
class Transform_observer_token
{
public:
    Transform_observer_token() = default;
    Transform_observer_token(const Transform_observer_token&) = delete;
    Transform_observer_token& operator=(const Transform_observer_token&) = delete;
    Transform_observer_token(Transform_observer_token&& other) noexcept;
    Transform_observer_token& operator=(Transform_observer_token&& other) noexcept;
    ~Transform_observer_token() noexcept;

    Transform_observer_token(std::weak_ptr<Transform_observer_list> list, uint64_t id);

    [[nodiscard]] auto is_active() const -> bool;
    void release();

private:
    std::weak_ptr<Transform_observer_list> m_list{};
    uint64_t                               m_id  {0};
};

} // namespace erhe::scene
