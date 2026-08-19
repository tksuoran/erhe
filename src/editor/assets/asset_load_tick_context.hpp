#pragma once

#include <chrono>
#include <cstddef>

namespace tf {
    class Executor;
}
namespace erhe::graphics {
    class Command_buffer;
    class Device;
}

// Codegen emits the config structs at global scope
// (build/src/editor/config/generated/load_config.hpp).
struct Load_config;

namespace editor {

class App_context;

// The per-frame bound on asynchronous asset loading (async-asset-loading
// plan 2.4). One budget is constructed per frame in Editor::tick and shared
// by every live Asset_load_task, handed out round-robin, so that N
// concurrent loads degrade gracefully instead of multiplying per-frame cost.
//
// The two kinds of cap interact, and the rule is explicit: the time slice is
// checked BETWEEN items and wins; the byte and item budgets are the upper
// bound within that slice. So a task loop is
//
//     while (!budget.is_exhausted() && budget.take_residency_item()) { ... }
//
// and every take_* call reports how much it actually granted, which may be
// less than requested (or zero) - the task then resumes next frame.
//
// max_decoded_bytes_in_flight is deliberately NOT here: it is a standing cap
// on the decoded bytes waiting for upload across frames, so it is manager
// state, not a per-frame allowance. It is carried on the tick context.
class Frame_load_budget
{
public:
    explicit Frame_load_budget(const Load_config& load_config);

    // True once the time slice has elapsed. Checked between items; wins over
    // every other budget below.
    [[nodiscard]] auto is_exhausted() const -> bool;

    // Granted amount, 0 .. requested. Zero means "no budget left this frame".
    [[nodiscard]] auto take_gpu_upload_bytes(std::size_t requested) -> std::size_t;
    [[nodiscard]] auto take_io_read_bytes   (std::size_t requested) -> std::size_t;

    // Return unspent bytes that take_gpu_upload_bytes granted. Used when a
    // consumer is handed the whole remaining allowance up front (the
    // budgeted transfer-queue drain) and records less than it took.
    void give_back_gpu_upload_bytes(std::size_t byte_count);

    // One item of the residency / publish slice, false when the slice is spent.
    [[nodiscard]] auto take_residency_item() -> bool;
    [[nodiscard]] auto take_publish_item  () -> bool;

    [[nodiscard]] auto get_remaining_gpu_upload_bytes() const -> std::size_t { return m_gpu_upload_bytes; }
    [[nodiscard]] auto get_remaining_io_read_bytes   () const -> std::size_t { return m_io_read_bytes; }
    [[nodiscard]] auto get_remaining_residency_items () const -> std::size_t { return m_residency_items; }
    [[nodiscard]] auto get_remaining_publish_items   () const -> std::size_t { return m_publish_items; }

private:
    std::chrono::steady_clock::time_point m_deadline;
    std::size_t                           m_gpu_upload_bytes{0};
    std::size_t                           m_io_read_bytes   {0};
    std::size_t                           m_residency_items {0};
    std::size_t                           m_publish_items   {0};
};

// Everything a task may touch from the main thread during
// Asset_manager::tick. Holding the command buffer here is what makes threading
// invariant 1 of the plan (2.3) checkable: GPU objects are created and commands
// recorded only from a tick, and only against this command buffer.
class Asset_load_tick_context
{
public:
    App_context&                    app_context;
    erhe::graphics::Device&         graphics_device;
    erhe::graphics::Command_buffer& command_buffer;
    tf::Executor&                   executor;
    Frame_load_budget&              budget;

    // Standing backpressure cap (plan 2.4): a task must not schedule further
    // decode / build work while this many decoded bytes are already waiting
    // for upload budget.
    std::size_t max_decoded_bytes_in_flight{0};
};

} // namespace editor
