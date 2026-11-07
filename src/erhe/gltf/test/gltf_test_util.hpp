#pragma once

#include "erhe_gltf/gltf_fastgltf.hpp"
#include "erhe_dataformat/vertex_format.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
// `scene`; parse_gltf touches no graphics device. Gltf_document::item parses a
// document of derived data (a Scene item written with Prepared_mesh_data::write).
[[nodiscard]] auto parse_file(
    const std::filesystem::path& path,
    erhe::scene::Scene&          scene,
    erhe::gltf::Gltf_document    document = erhe::gltf::Gltf_document::file
) -> erhe::gltf::Gltf_data;

// The JSON and BIN chunks of a GLB file (BIN empty when there is none).
class Glb_chunks
{
public:
    std::string json;
    std::string bin;
};
[[nodiscard]] auto split_glb(std::string_view glb) -> Glb_chunks;
// A GLB file of the two chunks (padded to 4 bytes; no BIN chunk when bin is empty).
[[nodiscard]] auto make_glb(std::string_view json, std::string_view bin) -> std::string;

// The vertex layouts Mesh_memory derives on a device with position
// quantization and unorm16x3 vertex input: (base, optimized).
[[nodiscard]] auto make_test_gpu_formats() -> std::pair<erhe::dataformat::Vertex_format, erhe::dataformat::Vertex_format>;

} // namespace erhe_gltf_test
