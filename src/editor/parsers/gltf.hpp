#pragma once

#include <filesystem>
#include <memory>

namespace editor {

class Scene_root;

auto parse_gltf(
    const std::shared_ptr<Scene_root>& scene_root,
    const std::filesystem::path&       path
) -> bool;

}
