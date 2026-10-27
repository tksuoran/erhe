#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

namespace tf {
    class Executor;
}
namespace erhe::graphics {
    class Command_buffer;
    class Device;
}
namespace erhe::utility {
    class Memory_budget;
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
// The load memory budget is deliberately NOT here: it is a standing cap on
// the CPU bytes waiting for upload across frames, so it is manager state,
// not a per-frame allowance. It is carried on the tick context.
class Frame_load_budget
{
public:
    // gpu_upload_bytes: this frame's upload allowance
    // (Asset_manager::get_gpu_upload_bytes_per_frame()); the other budgets
    // come from load_config.
    Frame_load_budget(const Load_config& load_config, std::size_t gpu_upload_bytes);

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

    // load.max_in_flight_bytes as set (0: the platform default); the
    // Asset_manager sizes its load memory budget from it each tick.
    std::size_t max_in_flight_bytes{0};

    // The load memory budget (doc/editor/async_asset_loading.md "Load memory
    // budget"), shared by every live load: a mesh build job or an image
    // decode starts only with a lease of it, held until its bytes are in
    // upload staging. Set by Asset_manager::tick before the tasks tick.
    std::shared_ptr<erhe::utility::Memory_budget> load_memory_budget{};

    // Hard cap on the host-visible staging the budgeted uploads use
    // (Device::allocate_upload_staging): passed with every staged upload.
    std::size_t max_upload_staging_bytes{0};
};

} // namespace editor
