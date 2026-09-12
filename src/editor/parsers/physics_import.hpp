#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe { class Hierarchy; }
namespace erhe::physics { enum class Motion_mode : unsigned int; }
namespace erhe::scene { class Physics_description; class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Operation;
class Scene_root;

// Whether a property list is every local value of the item it belongs to, or
// only the values it names. A file that carries the complete local set (the
// glTF ERHE_* payloads) has every value the item is to hold in the list, so a
// value outside it inherits again after the reload; a file that names single
// values (the `erhe:Owner:name` attributes of a USD prim) says nothing about
// the rest, which the description's own fields carry.
enum class Physics_property_set : int {
    listed_values,
    complete_local_set
};

// One shared physics item the import makes - a physics material, a collision
// filter or a joint-settings item - as the file describes it beside the
// neutral record of the same index.
class Physics_import_item
{
public:
    // The name the item takes; the description's name, else a synthesized
    // one, when empty.
    std::string                                      name;
    std::vector<std::pair<std::string, std::string>> properties; // qualified erhe property name -> D16 text
    Physics_property_set                             property_set{Physics_property_set::listed_values};
    // The prim of the loaded tree the item is placed under. The item rides
    // that tree's insert then, the way a material the file placed does; an
    // item with no parent gets a content-library attach operation of its own,
    // which is what creates its kind scope.
    std::shared_ptr<erhe::Hierarchy>                 parent;
};

// The state of one body the neutral record has no field for, by the node the
// body sits on.
class Physics_import_body
{
public:
    std::optional<erhe::physics::Motion_mode>        motion_mode;
    std::vector<std::pair<std::string, std::string>> properties;
    Physics_property_set                             property_set{Physics_property_set::listed_values};
};

// What one file's physics import works from: the format-neutral description
// every reader fills, the file it came from (the content library's source
// reference names it), and the per-record state that is the reading format's
// own. A per-record list shorter than the description's leaves the records
// beyond it with the description's own values.
class Physics_import_arguments
{
public:
    const erhe::scene::Physics_description* description{nullptr};
    std::filesystem::path                   path;
    std::vector<Physics_import_item>        materials;
    std::vector<Physics_import_item>        collision_filters;
    std::vector<Physics_import_item>        joint_settings;
    std::unordered_map<const erhe::scene::Node*, Physics_import_body> bodies;
};

// Maps a format-neutral physics description onto editor physics: shared
// Physics_material / Collision_filter / Physics_joint_settings content-library
// items (placed in the loaded tree, or attached through library attach
// operations appended to operations), Node_physics attachments (rigid bodies /
// triggers, with compound folding of descendant colliders) and Node_joint
// attachments. Must be called after mesh finalization (mesh-sourced collision
// shapes need the built Geometry) and before the Compound_operation is
// composed.
void import_physics(
    App_context&                             context,
    const Physics_import_arguments&          arguments,
    const std::shared_ptr<Scene_root>&       scene_root,
    std::vector<std::shared_ptr<Operation>>& operations
);

}
