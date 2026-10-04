#pragma once

namespace erhe::version {

// The project version of the build, CMake's project(VERSION) of the root
// CMakeLists.txt, e.g. "1.0".
[[nodiscard]] auto get_project_version() -> const char*;

// `git describe --tags --always --dirty` of the source tree, evaluated at
// every build (the erhe_version_stamp target), e.g. "414c4e285" or
// "v1.0-12-g414c4e2-dirty"; "unknown" when the tree is not a git checkout
// or git is not available.
[[nodiscard]] auto get_git_describe() -> const char*;

// "erhe <project version> (<git describe>)", the line the editor logs at
// startup beside the dependency commits.
[[nodiscard]] auto get_description() -> const char*;

} // namespace erhe::version
