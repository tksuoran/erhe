#include "erhe_file/file_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"

// Smoke tests
#include "../item/test/hierarchy_smoke.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

void print_usage(const char* program_name)
{
    std::printf("Usage: %s [options]\n", program_name);
    std::printf("Options:\n");
    std::printf("  --seed <value>      Use specific pseudo-random seed (decimal or 0x hex)\n");
    std::printf("  --duration <secs>   Run for N seconds (0 = run until Ctrl+C, default)\n");
    std::printf("  --help              Show this help\n");
}

auto parse_seed(const char* str) -> uint64_t
{
    return std::strtoull(str, nullptr, 0); // handles 0x prefix
}

void print_result(const erhe::item::test::Smoke_result& result, double elapsed)
{
    std::printf("\n=== Hierarchy Smoke Test Results ===\n");
    std::printf("  Seed:           0x%016llX\n", static_cast<unsigned long long>(result.seed));
    std::printf("  Duration:       %.1f seconds\n", elapsed);
    std::printf("  Operations:     %zu\n", result.operations);
    std::printf("  Nodes created:  %zu\n", result.nodes_created);
    std::printf("  Reparents:      %zu\n", result.reparents);
    std::printf("  Removes:        %zu\n", result.removes);
    std::printf("  Clones:         %zu\n", result.clones);
    std::printf("  Iterations:     %zu\n", result.iterations);
    std::printf("  Sanity checks:  %zu\n", result.sanity_checks);
    std::printf("  Errors:         %zu\n", result.errors);
    std::printf("====================================\n");
}

} // namespace

int main(int argc, char** argv)
{
    uint64_t seed     = 0;
    bool     has_seed = false;
    double   duration = 0.0; // 0 = run forever

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed     = parse_seed(argv[++i]);
            has_seed = true;
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::atof(argv[++i]);
        } else {
            std::printf("Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!has_seed) {
        seed = std::random_device{}();
        seed = (seed << 32) | std::random_device{}();
    }

    // Initialize logging
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    erhe::item::initialize_logging();

    const bool run_forever = (duration <= 0.0);
    if (run_forever) {
        duration = 1e12; // effectively forever
    }

    std::printf("Hierarchy smoke test starting\n");
    std::printf("  Seed:     0x%016llX\n", static_cast<unsigned long long>(seed));
    if (run_forever) {
        std::printf("  Duration: until Ctrl+C\n");
    } else {
        std::printf("  Duration: %.1f seconds\n", duration);
    }
    std::printf("\nTo reproduce: %s --seed 0x%016llX\n\n", argv[0], static_cast<unsigned long long>(seed));

    const auto start  = std::chrono::steady_clock::now();
    const auto result = erhe::item::test::run_hierarchy_smoke(seed, duration);
    const auto end    = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(end - start).count();

    print_result(result, elapsed);

    return (result.errors == 0) ? 0 : 1;
}
