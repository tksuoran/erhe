#pragma once

namespace editor
{

enum class State : unsigned int
{
    Disabled = 0,
    Inactive,
    Ready,
    Active
};

static constexpr const char* c_state_str[] =
{
    "disabled",
    "inactive",
    "ready",
    "active"
};

} // namespace editor
