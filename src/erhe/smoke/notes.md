# erhe_smoke

## Purpose
A standalone smoke test executable that stress-tests the `erhe::item` hierarchy system. It performs randomized operations (create nodes, reparent, remove, clone) on a hierarchy tree for a specified duration, verifying structural invariants after each operation. Useful for catching memory corruption, dangling pointers, and hierarchy logic bugs, especially under AddressSanitizer.

## Key Types
- No library types -- this is an executable, not a library.
- Uses `erhe::item::test::run_hierarchy_smoke(seed, duration)` from the item test code.
- `Smoke_result` -- Returned struct with operation counts (nodes created, reparents, removes, clones, iterations, sanity checks, errors).

## Public API
Command-line executable:
- `erhe_smoke --seed <value>` -- Use a specific PRNG seed for reproducibility.
- `erhe_smoke --duration <seconds>` -- Run for N seconds (0 = until Ctrl+C).
- Exit code 0 on success, 1 if errors detected.

## Dependencies
- erhe::item (Hierarchy, the code under test)
- erhe::file (logging bootstrap)
- erhe::log (spdlog initialization)
- erhe::verify

## Notes
- The smoke test source is shared with the item library: `../item/test/hierarchy_smoke.cpp`.
- Seeds are printed at startup for easy reproduction of failures.
- Designed to run under ASAN for maximum bug detection.
