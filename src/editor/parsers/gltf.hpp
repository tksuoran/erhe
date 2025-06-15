#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Device;
}
namespace erhe::gltf {
    class Image_transfer;
}
namespace erhe::primitive {
    class Build_info;
}

namespace editor {

class Materials;
class Scene_root;

void import_gltf(
    erhe::graphics::Device&      graphics_device,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
);

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> std::vector<std::string>;

}
