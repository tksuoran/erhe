#pragma once

#include <cstdio>
#include <cstdlib>

// Check macro for the codegen test programs. Unlike assert(), it is never
// compiled out: the tests put the work under test (parsing, serialization)
// inside the checked expression, and a Release build with NDEBUG would
// otherwise skip that work and pass vacuously.
#define CODEGEN_TEST_CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
            std::exit(EXIT_FAILURE); \
        } \
    } while (false)
