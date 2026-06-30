#pragma once

#include "config/generated/operation_params.hpp"
#include "erhe_commands/command.hpp"

namespace editor {

// ImGui drag-and-drop payload type string for an operation dragged out of the
// Operations window and dropped into an inventory slot.
constexpr const char* c_operation_payload_type = "Operation_Preset";

// POD payload, safe for ImGui's memcpy of the drag-drop blob. Carries the
// long-lived registered command that identifies the operation, plus a frozen
// snapshot of its parameters taken at drag time. Every field is a scalar or a
// raw pointer (Operation_params is all-scalar), so a byte copy is well defined.
class Operation_drag_payload
{
public:
    erhe::commands::Command* command{nullptr};
    Operation_params         params {};
};

// Short display label for an operation slot button: the last dotted segment of
// the command name (e.g. "Geometry.Conway.Kis" -> "Kis"). Returns a pointer into
// the command's own name storage, valid for the command's lifetime.
[[nodiscard]] inline auto operation_short_label(const erhe::commands::Command* command) -> const char*
{
    const char* name = command->get_name();
    const char* last = name;
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p == '.') {
            last = p + 1;
        }
    }
    return last;
}

}
