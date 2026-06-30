#pragma once

#include "config/generated/operation_params.hpp"

namespace erhe::commands { class Command; }

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

}
