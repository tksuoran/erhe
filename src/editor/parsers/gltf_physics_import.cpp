#include "parsers/gltf_physics_import.hpp"

#include "parsers/gltf_extensions_import.hpp"
#include "parsers/physics_import.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_scene/physics_description.hpp"

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
    // (doc/editor/gltf_scene_roundtrip.md phase 3).
    const Gltf_physics_item_names item_names = parse_gltf_physics_item_names(gltf_data);
    const auto to_import_item = [](const Gltf_physics_item_record& record) -> Physics_import_item {
        return Physics_import_item{
            .name         = record.name,
            .properties   = record.properties,
            .property_set = record.has_properties
                ? Physics_property_set::complete_local_set
                : Physics_property_set::listed_values
        };
    };
    arguments.materials.reserve(item_names.physics_materials.size());
    for (const Gltf_physics_item_record& record : item_names.physics_materials) {
        arguments.materials.push_back(to_import_item(record));
    }
    // A `PhysicsJoint` entry has no name field, so a joint-settings item gets
    // its name back only from here; the properties are its complete local
    // set, so an axis value a folder or a style supplies is supplied by it
    // again after the reload instead of being baked in as a local value.
    arguments.joint_settings.reserve(item_names.physics_joints.size());
    for (const Gltf_physics_item_record& record : item_names.physics_joints) {
        arguments.joint_settings.push_back(to_import_item(record));
    }
    arguments.collision_filters.reserve(item_names.collision_filters.size());
    for (const std::string& name : item_names.collision_filters) {
        arguments.collision_filters.push_back(Physics_import_item{.name = name});
    }

    // A body's own values are the node's (P8): they ride ERHE_node
    // `properties` and the glTF reader has already applied them, so the
    // import has nothing to state per body beyond the KHR record.
    import_physics(context, arguments, scene_root, operations);
}

}
