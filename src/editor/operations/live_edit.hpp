#pragma once

#include "operations/operation_group.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

namespace erhe { class Item_base; }

namespace editor {

class App_context;
class Operation;

// An edit that spans frames and records one entry at its end (doc/editor/operations.md "Live edits"); held by
// Operation_stack, which runs its operations' first executes and is the only one to record or undo them.
// The owner's handle of an open live edit: move-only; the stack refuses a call with a handle of an edit that ended.
class Live_edit_token
{
public:
    Live_edit_token() = default;
    Live_edit_token(const Live_edit_token&) = delete;
    auto operator=(const Live_edit_token&) -> Live_edit_token& = delete;
    Live_edit_token(Live_edit_token&& other) noexcept;
    auto operator=(Live_edit_token&& other) noexcept -> Live_edit_token&;

    [[nodiscard]] auto get_serial() const -> std::uint64_t; // 0: no edit

private:
    friend class Live_edit;
    explicit Live_edit_token(std::uint64_t serial);
    std::uint64_t m_serial{0};
};

class Live_edit
{
public:
    // on_abandoned is called, with no operation executing, when the stack ends the edit on its own (a scene close,
    // the shutdown); it may call the stack as any main-thread code does.
    [[nodiscard]] auto begin(std::string reason, std::function<void()> on_abandoned) -> Live_edit_token;
    // Whether the token names the open edit.
    [[nodiscard]] auto is_owner(const Live_edit_token& token) const -> bool;
    [[nodiscard]] auto is_open   () const -> bool;
    [[nodiscard]] auto get_reason() const -> const char*; // null when closed
    [[nodiscard]] auto get_size  () const -> std::size_t;

    // An operation of the edit that has executed.
    void add(const std::shared_ptr<Operation>& operation);
    // Closes the edit: the one entry to record, null when it holds no operation.
    [[nodiscard]] auto end() -> std::shared_ptr<Operation>;
    // Undoes the operations in reverse order and closes the edit; nothing is recorded.
    void cancel(App_context& context);
    // cancel(); returns on_abandoned for the caller to call once nothing executes.
    [[nodiscard]] auto abandon(App_context& context) -> std::function<void()>;

    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const;

private:
    void close();

    std::uint64_t         m_serial{0};      // of the open edit, 0 when closed
    std::uint64_t         m_next_serial{1};
    std::string           m_reason;
    std::function<void()> m_on_abandoned;
    Operation_group       m_operations;
};

// What Operation_stack::end_live_edit() did.
enum class Live_edit_end : unsigned int {
    recorded = 0, // one entry
    unchanged,    // no operation: nothing recorded, the redo history kept
    refused       // the token names no open live edit
};

// Whether a scene save holds the stack (Operation_stack::is_held()).
enum class Save_hold : unsigned int {
    none = 0,
    held
};

// How another party hands the stack an operation.
enum class Operation_request : unsigned int {
    execute_now = 0, // the caller reads the outcome
    queue            // queue(), queue_from_thread(): the caller reads nothing
};

enum class Operation_admission : unsigned int {
    execute = 0, // runs now
    hold,        // waits in the queue until the save or the live edit ends
    refuse       // refused with the live edit's reason
};

// The rule for an operation another party hands the stack (doc/editor/operations.md "Live edits"): during a save it
// waits; during a live edit execute_now() is refused and a queued one waits.
[[nodiscard]] auto get_operation_admission(Operation_request request, Save_hold save_hold, const Live_edit& live_edit) -> Operation_admission;

} // namespace editor
