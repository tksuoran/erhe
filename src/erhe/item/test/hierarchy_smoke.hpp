#pragma once

#include <cstdint>
#include <string>

namespace erhe::item::test {

struct Smoke_result
{
    uint64_t    seed{0};
    std::size_t operations{0};
    std::size_t nodes_created{0};
    std::size_t reparents{0};
    std::size_t removes{0};
    std::size_t clones{0};
    std::size_t iterations{0};
    std::size_t sanity_checks{0};
    std::size_t errors{0};
};

// Runs hierarchy smoke test for the given duration (seconds).
// Fully deterministic given the same seed.
// Returns statistics about what was done.
auto run_hierarchy_smoke(uint64_t seed, double duration_seconds) -> Smoke_result;

} // namespace erhe::item::test
