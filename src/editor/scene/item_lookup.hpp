#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe { class Item_base; }

namespace editor {

class App_context;
class Scene_root;

// Any item of a scene by unique id or by name: nodes, their attachments
// (meshes, lights, cameras, ...), the content library's materials and
// textures, and its graph assets with their nodes. Name lookup returns the
// first match in that order.
[[nodiscard]] auto find_item_in_scene_by_id  (Scene_root& scene_root, std::size_t id)        -> std::shared_ptr<erhe::Item_base>;
[[nodiscard]] auto find_item_in_scene_by_name(Scene_root& scene_root, std::string_view name) -> std::shared_ptr<erhe::Item_base>;

// The item a stored reference text names (doc/usd-compatibility-plan.md M1):
// a text holding '/' is a path (erhe::Hierarchy::get_path()) and is looked
// up first from the scene's root node and then from the content library
// root, where it is the ERHE_scene library_folders folder path; a library
// path reaching an entry node yields the entry's item. Every other text,
// and a path that matches nothing, is a name (find_item_in_scene_by_name),
// which is the form older files store.
[[nodiscard]] auto find_item_in_scene_by_reference(Scene_root& scene_root, std::string_view name_or_path) -> std::shared_ptr<erhe::Item_base>;

// The scene an item belongs to: the registered scene that is its Item_host,
// or, for an asset-typed item (materials never claim a host), the scene
// whose container defines it. nullptr when neither applies (a preview or
// tool scene item, an unregistered asset).
[[nodiscard]] auto find_scene_root_for_item(App_context& context, const erhe::Item_base& item) -> Scene_root*;

// The item `name_or_path` names for an object reference written on `from`
// (D28): find_item_in_scene_by_reference in from's scene. This is the editor's name
// resolution for object values; the library's parse_value context overload
// walks from's Item_host, which an asset-typed item (a material) does not
// have.
[[nodiscard]] auto resolve_reference_by_name(App_context& context, const erhe::Item_base& from, std::string_view name_or_path) -> std::shared_ptr<erhe::Item_base>;

// Object reference candidates (doc/property-system.md D28): the items of
// the target's scene whose type bit is in item_types and that are shown in
// the UI (developer-only items in developer mode) - the content library
// items, and the scene's nodes and node attachments (a node-typed
// reference lists scene nodes). Clears `out` first; the caller clears it
// again after the draw so the strong references do not outlive the frame.
void collect_reference_candidates(
    App_context&                                   context,
    const erhe::Item_base&                         target,
    uint64_t                                       item_types,
    std::vector<std::shared_ptr<erhe::Item_base>>& out
);

}
