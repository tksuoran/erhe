# erhe_message_bus

## Purpose
Generic typed publish-subscribe message bus. Supports synchronous dispatch,
queued (deferred) dispatch, or both. Used for decoupled communication between
editor subsystems (e.g., selection changes, scene modifications).

## Key Types
- `Message_bus<Message_type, Dispatch_policy>` -- the bus itself; parameterized on message type and dispatch policy
- `Subscription<Message_type>` -- RAII subscription handle; message delivery stops when the subscription is dropped
- `Dispatch_policy` -- enum: `sync_only`, `queue_only`, `both`

## Public API
```cpp
Message_bus<MyMessage> bus;
auto sub = bus.subscribe([](MyMessage& msg){ /* handle */ });
bus.send_message(MyMessage{...});   // sync dispatch (immediate)
bus.queue_message(MyMessage{...});  // deferred dispatch
bus.update();                       // deliver queued messages
```

## Dependencies
- `erhe::profile` -- profiling mutex macros
- Standard library only (no other erhe dependencies)

## Notes
- Subscriptions use `shared_ptr`/`weak_ptr` so expired subscribers are automatically pruned.
- Thread-safe: separate mutexes protect the receiver list and message queues.
- Queued messages are double-buffered: `queue_message()` writes to a staging queue,
  `update()` swaps and dispatches from the active queue, preventing re-entrancy issues.
- The `Dispatch_policy` is enforced at compile time via C++20 `requires` clauses.
