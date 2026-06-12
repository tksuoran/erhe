#include "gltf_none.hpp"

namespace erhe::gltf {

auto parse_gltf(const Gltf_parse_arguments&) -> Gltf_data
{
    return {};
}

auto scan_gltf(std::filesystem::path) -> Gltf_scan
{
    return {};
}

auto export_gltf(const erhe::scene::Node&, bool, const Gltf_physics_data*) -> std::string
{
    return {};
}

}
