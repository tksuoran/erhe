#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace editor {

// Which redo-stack entry free_undone_loads() should release: the highest index
// holding a droppable payload.
//
// Index order is what makes this non-obvious. Operation_stack::undo() pushes
// the MOST RECENTLY recorded entry first, so in the redo stack index 0 is the
// newest and back() is the one that would be redone first. Entries recorded
// after index i therefore sit at indices < i. Releasing the highest index frees
// the most, because discarding [0, i) destroys those entries and their payloads
// too, while entries at indices > i were recorded before the load and cannot
// reference its content.
//
// Deliberately kept in its own dependency-free header so the rule can be unit
// tested without building the editor (doc/reloadable-asset-loads.md).
[[nodiscard]] auto select_free_undone_loads_target(const std::vector<bool>& has_payload) -> std::optional<std::size_t>;

}
