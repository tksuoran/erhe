#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Instance;
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
    erhe::graphics::Instance&    graphics_instance,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path,
    bool                         y_up = true
);

[[nodiscard]] auto scan_gltf(const std::filesystem::path& path) -> std::vector<std::string>;

}
