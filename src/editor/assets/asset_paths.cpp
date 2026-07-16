#include "assets/asset_paths.hpp"

namespace editor {

auto normalize_asset_path(const std::filesystem::path& path) -> std::filesystem::path
{
    std::error_code error_code;
    std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
    if (error_code) {
        return path;
    }
    return canonical_path;
}

auto asset_path_to_string(const std::filesystem::path& path) -> std::string
{
    return path.generic_string();
}

}
