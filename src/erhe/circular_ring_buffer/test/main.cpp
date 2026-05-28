#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>

// Stress-mode parameters consumed by the long-running smoke test. Both come
// from environment variables so the test executable can be driven from a
// shell script without GoogleTest argument plumbing.
//
//   ERHE_STRESS_SECONDS  Run the random-mix phase of the smoke test for at
//                        least this many seconds before draining. Unset or 0
//                        keeps the test in its quick, deterministic form.
//   ERHE_STRESS_SEED     Seed for the random-mix RNG. When unset, the smoke
//                        test falls back to a fixed seed so CTest runs stay
//                        reproducible.
std::uint64_t g_stress_seconds      = 0;
std::uint64_t g_stress_seed         = 0;
bool          g_stress_seed_was_set = false;

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);

    if (const char* env = std::getenv("ERHE_STRESS_SECONDS")) {
        g_stress_seconds = std::strtoull(env, nullptr, 10);
    }
    if (const char* env = std::getenv("ERHE_STRESS_SEED")) {
        g_stress_seed         = std::strtoull(env, nullptr, 10);
        g_stress_seed_was_set = true;
    }

    return RUN_ALL_TESTS();
}
