#include "commands/state.hpp"

namespace editor {

auto c_str(const State state) -> const char*
{
    return c_state_str[static_cast<size_t>(state)];
}


}  // namespace editor
