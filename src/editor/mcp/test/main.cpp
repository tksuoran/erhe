#include "editor_launcher.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

auto env_or(const char* name, const char* fallback) -> std::string
{
    const char* v = std::getenv(name);
    return (v != nullptr) ? std::string{v} : std::string{fallback};
}

auto env_or_int(const char* name, int fallback) -> int
{
    const char* v = std::getenv(name);
    return (v != nullptr) ? std::atoi(v) : fallback;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        // The FIXTURES_CLEANUP step of the ctest fixtures (CMakeLists.txt).
        // ERHE_MCP_TEST_TOKEN_FILE names the token of an auth-enabled editor.
        if (std::strcmp(argv[i], "--request-editor-exit") == 0) {
            return mcp_test::request_exit_and_wait(
                env_or    ("ERHE_MCP_TEST_HOST", "127.0.0.1"),
                env_or_int("ERHE_MCP_TEST_PORT", 3743),
                mcp_test::read_token_file(env_or("ERHE_MCP_TEST_TOKEN_FILE", ""))
            );
        }
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
