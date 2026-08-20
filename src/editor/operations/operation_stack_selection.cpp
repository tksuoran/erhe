#include "operations/operation_stack_selection.hpp"

namespace editor {

auto select_free_undone_loads_target(const std::vector<bool>& has_payload) -> std::optional<std::size_t>
{
    for (std::size_t index = has_payload.size(); index > 0; --index) {
        if (has_payload[index - 1]) {
            return index - 1;
        }
    }
    return std::nullopt;
}

}
