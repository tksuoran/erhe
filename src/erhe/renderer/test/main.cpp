#include <gtest/gtest.h>

#include <filesystem>

int main(int argc, char** argv)
{
    // Debug_renderer loads its shaders from res/shaders/ relative to the
    // working directory, as the editor does from the repository root.
    std::filesystem::current_path(std::filesystem::path{ERHE_RENDERER_TEST_REPO_ROOT});

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
