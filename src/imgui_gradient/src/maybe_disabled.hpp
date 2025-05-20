#pragma once
#include <functional>

namespace ImGG {

void maybe_disabled(
    bool                         condition,
    const char*                  reason_to_disable,
    std::function<void()> const& widgets
);

} // namespace ImGG