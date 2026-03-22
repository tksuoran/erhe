# erhe_message_bus -- Code Review

## Summary
A generic publish-subscribe message bus using `std::shared_ptr`/`std::weak_ptr` for automatic subscription lifetime management. The design is template-based with configurable dispatch policies (sync, queue, or both). It is well-structured for its purpose but has a thread safety issue in the `update()` method.

## Strengths
- Clean template design with `Dispatch_policy` using C++20 `requires` clauses
- Automatic subscription cleanup via `weak_ptr` -- subscribers are pruned when their `Subscription` goes out of scope
- Lazy pruning of expired subscriptions (only when detected during dispatch)
- Type-safe: each `Message_bus` is parameterized on message type
- Good separation between synchronous and queued dispatch

## Issues
- **[notable]** Potential deadlock in `update()` (message_bus.hpp:84-109): acquires `m_receivers_mutex` first, then `m_messages_mutex`, then `m_queued_messages_mutex`. Meanwhile, `queue_message()` acquires only `m_queued_messages_mutex` and `send_message()` acquires only `m_receivers_mutex`. If `send_message` is called from within a receiver callback during `update()`, it will attempt to re-lock `m_receivers_mutex` (non-recursive), causing undefined behavior/deadlock.
- **[moderate]** `send_message()` holds `m_receivers_mutex` while invoking callbacks (message_bus.hpp:61-76). If a callback modifies the message bus (e.g., subscribes a new listener), this will deadlock since the mutex is not recursive.
- **[minor]** `prune_expired()` could use C++20 `std::erase_if` instead of the erase-remove idiom.
- **[minor]** Message is passed by value to both `send_message` and `queue_message`. For large message types, this could be expensive. Consider accepting by `const&` or providing a perfect-forwarding overload.

## Suggestions
- Make `m_receivers_mutex` a `std::recursive_mutex` to allow re-entrant calls from callbacks, or document that callbacks must not call `send_message`/`subscribe` on the same bus.
- Replace the erase-remove pattern in `prune_expired()` with `std::erase_if(m_receivers, [](const auto& w) { return w.expired(); })`.
- Consider whether the double-buffered queue (`m_messages` + `m_queued_messages`) is necessary. The current approach means queued messages are always delivered one `update()` cycle late. If that is intentional, document it.
- The TODO comment (line 45) about replacing with a signals library suggests known limitations -- either act on it or remove the TODO.
