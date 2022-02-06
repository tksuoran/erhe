#pragma once

#include "erhe/toolkit/filesystem.hpp"

#include <memory>

namespace editor {

class Scene_root;

auto parse_gltf(
    const std::shared_ptr<Scene_root>& scene_root,
    const fs::path&                    path
) -> bool;

}
