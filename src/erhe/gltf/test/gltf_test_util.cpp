#include "gltf_test_util.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

#if defined(_WIN32)
#   include <process.h>
#else
#   include <unistd.h>
#endif

namespace erhe_gltf_test {

auto data_path(const std::string_view file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_GLTF_TEST_DATA_DIR} / file_name;
}

namespace {

class Temporary_directory
{
public:
    Temporary_directory()
    {
#if defined(_WIN32)
        const int pid = _getpid();
#else
        const int pid = static_cast<int>(getpid());
#endif
        path = std::filesystem::temp_directory_path() / ("erhe_gltf_tests_" + std::to_string(pid));
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path);
    }
    ~Temporary_directory() noexcept
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

} // anonymous namespace

auto process_temporary_directory() -> const std::filesystem::path&
{
    static const Temporary_directory s_directory;
    return s_directory.path;
}

auto write_temporary_file(const std::string_view file_name, const std::string_view text) -> std::filesystem::path
{
    const std::filesystem::path path = process_temporary_directory() / file_name;
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    ERHE_VERIFY(stream.good());
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    ERHE_VERIFY(stream.good());
    return path;
}

auto split_glb(const std::string_view glb) -> Glb_chunks
{
    Glb_chunks chunks;
    std::size_t offset = 12;
    while ((offset + 8) <= glb.size()) {
        uint32_t length = 0;
        uint32_t type   = 0;
        std::memcpy(&length, glb.data() + offset, sizeof(uint32_t));
        std::memcpy(&type,   glb.data() + offset + 4, sizeof(uint32_t));
        ERHE_VERIFY((offset + 8 + length) <= glb.size());
        const std::string_view data = glb.substr(offset + 8, length);
        if (type == 0x4e4f534au) { // JSON
            chunks.json.assign(data);
        } else if (type == 0x004e4942u) { // BIN
            chunks.bin.assign(data);
        }
        offset += 8 + length;
    }
    return chunks;
}

auto make_glb(const std::string_view json, const std::string_view bin) -> std::string
{
    const auto append_u32 = [](std::string& out, const uint32_t value) {
        char bytes[4];
        std::memcpy(bytes, &value, sizeof(uint32_t));
        out.append(bytes, 4);
    };
    std::string padded_json{json};
    while ((padded_json.size() % 4) != 0) {
        padded_json.push_back(' ');
    }
    std::string padded_bin{bin};
    while ((padded_bin.size() % 4) != 0) {
        padded_bin.push_back('\0');
    }
    const std::size_t total = 12 + 8 + padded_json.size() + (padded_bin.empty() ? 0 : (8 + padded_bin.size()));
    std::string glb;
    append_u32(glb, 0x46546c67u); // glTF
    append_u32(glb, 2);
    append_u32(glb, static_cast<uint32_t>(total));
    append_u32(glb, static_cast<uint32_t>(padded_json.size()));
    append_u32(glb, 0x4e4f534au);
    glb += padded_json;
    if (!padded_bin.empty()) {
        append_u32(glb, static_cast<uint32_t>(padded_bin.size()));
        append_u32(glb, 0x004e4942u);
        glb += padded_bin;
    }
    return glb;
}

auto parse_file(const std::filesystem::path& path, erhe::scene::Scene& scene, const erhe::gltf::Gltf_document document) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor  = executor,
            .root_node = root_node,
            .path      = path,
            .parallel  = false,
            .document  = document
        }
    );
}

// The vertex layouts Mesh_memory derives on a device with position
// quantization and unorm16x3 vertex input (doc/erhe/gltf.md "GPU vertex streams on export"
// "Signature"), built here without a device.
auto make_test_gpu_formats() -> std::pair<erhe::dataformat::Vertex_format, erhe::dataformat::Vertex_format>
{
    using erhe::dataformat::Format;
    using erhe::dataformat::Vertex_attribute_usage;
    erhe::dataformat::Vertex_format base{
        {
            {0, {{Format::format_32_vec3_float, Vertex_attribute_usage::position, 0}}},
            {1, {
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,    erhe::dataformat::normal_attribute},
                {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,   0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 1},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, 2},
                {Format::format_32_vec4_float, Vertex_attribute_usage::color,     0}
            }},
            {2, {
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute_smooth},
                {Format::format_8_vec2_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control},
                {Format::format_16_vec2_uint,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_valency_edge_count},
                {Format::format_8_vec4_unorm,  Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_id}
            }}
        }
    };
    erhe::dataformat::Vertex_format optimized{
        {
            {0, {{Format::format_16_vec3_snorm, Vertex_attribute_usage::position, 0}}},
            {1, {
                {Format::format_16_vec4_sint,  Vertex_attribute_usage::tangent,   0},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 0},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 1},
                {Format::format_16_vec2_unorm, Vertex_attribute_usage::tex_coord, 2},
                {Format::format_8_vec4_unorm,  Vertex_attribute_usage::color,     0}
            }},
            {2, {{Format::format_8_vec2_unorm, Vertex_attribute_usage::custom, erhe::dataformat::custom_attribute_aniso_control}}}
        }
    };
    const erhe::dataformat::Vertex_stream_packing packing{.min_attribute_alignment = 1, .min_stride_alignment = 4};
    base.repack(packing);
    optimized.repack(packing);
    return {std::move(base), std::move(optimized)};
}

} // namespace erhe_gltf_test
