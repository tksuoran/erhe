#include "erhe/commands/state.hpp"

#include <cstddef>

namespace erhe::commands {

auto c_str(const State state) -> const char*
{
    return c_state_str[static_cast<size_t>(state)];
}

}  // namespace erhe::commands
