#include "gltf_test_util.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

#include <cstdlib>
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

auto parse_file(const std::filesystem::path& path, erhe::scene::Scene& scene) -> erhe::gltf::Gltf_data
{
    tf::Executor executor{1};
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    return erhe::gltf::parse_gltf(
        erhe::gltf::Gltf_parse_arguments{
            .executor  = executor,
            .root_node = root_node,
            .path      = path,
            .parallel  = false
        }
    );
}

} // namespace erhe_gltf_test
