#pragma once

#include <filesystem>

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

void parse_gltf(
    erhe::graphics::Instance&    graphics_instance,
    erhe::primitive::Build_info  build_info,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
);

}
