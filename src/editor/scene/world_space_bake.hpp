#pragma once

#include "erhe_gltf/gltf.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <unordered_set>

struct Scene_export_settings; // generated into the global namespace by erhe_codegen

namespace erhe::scene {
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class Scene_root;

// Which nodes the world-space bake collapses and with what transforms
// (doc/editor/world_space_bake.md "Which meshes are baked").
class World_space_bake_plan
{
public:
    erhe::gltf::Gltf_export_world_bake bake;
    std::size_t                        candidate_mesh_count  {0}; // static meshes the walk reached
    std::size_t                        shared_skip_count     {0}; // instancing skip / over budget
    std::size_t                        duplicated_bytes      {0}; // estimated, shared primitives baked per instance
    std::map<std::string, std::size_t> kept_reasons;              // why nodes keep their transforms -> count
};

// Main thread, with the scene quiescent. external_asset_nodes: nodes the
// export writes as external asset references (never collapsed, their
// subtree is not written).
[[nodiscard]] auto plan_world_space_bake(
    const Scene_root&                                   scene_root,
    const Scene_export_settings&                        settings,
    const std::unordered_set<const erhe::scene::Node*>& external_asset_nodes
) -> World_space_bake_plan;

} // namespace editor
