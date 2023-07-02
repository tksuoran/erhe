#pragma once

namespace erhe::commands
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

auto c_str(const State state) -> const char*;

} // namespace erhe::commands
