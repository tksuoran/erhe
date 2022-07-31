#pragma once

#include "erhe/toolkit/filesystem.hpp"

#include <memory>

namespace erhe::primitive {
    class Build_info;
};

namespace editor {

class Materials;
class Scene_root;

void parse_gltf(
    const std::shared_ptr<Scene_root>& scene_root,
    erhe::primitive::Build_info&       build_info,
    const fs::path&                    path
);

}
