#pragma once

#include "erhe_gltf/gltf_fastgltf.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace erhe::scene {
    class Scene;
}

namespace erhe_gltf_test {

// The fixture directory (src/erhe/gltf/test/data).
[[nodiscard]] auto data_path(std::string_view file_name) -> std::filesystem::path;

// A directory this test process owns: created on first use, removed at exit,
// so cases running concurrently under ctest -j never share a path
// (doc/testing.md).
[[nodiscard]] auto process_temporary_directory() -> const std::filesystem::path&;

// Writes text to a new file in process_temporary_directory().
[[nodiscard]] auto write_temporary_file(std::string_view file_name, std::string_view text) -> std::filesystem::path;

// Parses a glTF file serially (no worker tasks) below a fresh root node of
// `scene`; parse_gltf touches no graphics device.
[[nodiscard]] auto parse_file(const std::filesystem::path& path, erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data;

} // namespace erhe_gltf_test
