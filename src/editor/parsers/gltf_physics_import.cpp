#include "parsers/gltf_physics_import.hpp"

#include "parsers/gltf_extensions_import.hpp"
#include "parsers/physics_import.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/physics_description.hpp"

#include <unordered_map>

namespace editor {

void import_gltf_physics(
    App_context&                             context,
    const erhe::gltf::Gltf_data&             gltf_data,
    const std::shared_ptr<Scene_root>&       scene_root,
    const std::filesystem::path&             path,
    std::vector<std::shared_ptr<Operation>>& operations
)
{
    Physics_import_arguments arguments{};
    arguments.description = &gltf_data.physics;
    arguments.path        = path;

    // The names and local property sets the ERHE_* payloads carry beside the
    // KHR entries: an erhe-authored file states the item names and the
    // complete local value set of each item, so a value the KHR entry baked
    // from an inherited one inherits again after the reload
    // (doc/gltf-scene-roundtrip-plan.md phase 3).
    const Gltf_physics_item_names item_names = parse_gltf_physics_item_names(gltf_data);
    arguments.materials.reserve(item_names.physics_materials.size());
    for (const Gltf_physics_material_record& record : item_names.physics_materials) {
        arguments.materials.push_back(
            Physics_import_item{
                .name         = record.name,
                .properties   = record.properties,
                .property_set = record.has_properties
                    ? Physics_property_set::complete_local_set
                    : Physics_property_set::listed_values
            }
        );
    }
    arguments.collision_filters.reserve(item_names.collision_filters.size());
    for (const std::string& name : item_names.collision_filters) {
        arguments.collision_filters.push_back(Physics_import_item{.name = name});
    }

    // ERHE_physics node payloads: rigid-body state KHR_physics_rigid_bodies
    // cannot carry.
    const std::unordered_map<const erhe::scene::Node*, Gltf_physics_overrides> physics_overrides =
        parse_gltf_physics_overrides(gltf_data);
    for (const std::pair<const erhe::scene::Node* const, Gltf_physics_overrides>& entry : physics_overrides) {
        arguments.bodies.emplace(
            entry.first,
            Physics_import_body{
                .motion_mode  = entry.second.motion_mode,
                .properties   = entry.second.properties,
                .property_set = entry.second.has_properties
                    ? Physics_property_set::complete_local_set
                    : Physics_property_set::listed_values
            }
        );
    }

    import_physics(context, arguments, scene_root, operations);
}

}
