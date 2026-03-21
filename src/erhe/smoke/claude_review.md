# erhe_smoke -- Code Review

## Summary
A small, well-written smoke test executable for the `erhe::item` hierarchy system. It provides a command-line interface for reproducible testing with seed control and duration limits. Clean, focused, and does what it needs to do.

## Strengths
- Proper seed-based reproducibility with hex seed output for exact reproduction
- Clean argument parsing with `--seed` and `--duration` options
- Good use of `std::random_device` for seed generation when not specified
- Clear results output with operation counts
- Non-zero exit code on errors for CI integration
- Initializes logging properly before running tests

## Issues
- **[minor]** `parse_seed` uses `std::strtoull` which returns 0 on parse failure with no error indication. Invalid seed strings silently become 0.
- **[minor]** `std::atof` used for duration parsing (main.cpp:66) does not report errors. `std::strtod` with error checking would be more robust.

## Suggestions
- Use `std::strtod` with `endptr` check for duration parsing to catch invalid input
- Consider adding `--verbose` flag to control log level during smoke testing
