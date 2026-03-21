#include "hierarchy_smoke.hpp"

#include <gtest/gtest.h>

namespace {

// Fixed seed for deterministic unit test runs
constexpr uint64_t deterministic_seed   = 0x4572'6865'5465'7374; // "ErheTest"
constexpr double   short_duration_seconds = 2.0;

TEST(HierarchySmoke, DeterministicRun)
{
    const auto result = erhe::item::test::run_hierarchy_smoke(deterministic_seed, short_duration_seconds);

    EXPECT_EQ(result.errors, 0u);
    EXPECT_GT(result.operations, 0u);
    EXPECT_GT(result.nodes_created, 0u);
    EXPECT_GT(result.reparents, 0u);
    EXPECT_GT(result.removes, 0u);
    EXPECT_GT(result.iterations, 0u);
    EXPECT_GT(result.sanity_checks, 0u);
}

} // namespace
