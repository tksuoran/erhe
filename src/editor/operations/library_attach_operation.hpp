#pragma once

#include "assets/asset_key.hpp"
#include "scene/generated/gltf_source_reference.hpp"

#include <memory>
#include <optional>

namespace erhe {
    class Hierarchy;
    class Item_base;
    class Scope;
}
namespace erhe::gltf {
    class Gltf_image_source;
}

namespace editor {

class App_context;
class Content_library;
class Operation;

// The undoable attach of a resource to a scene's content library
// (doc/usd_compatibility_design.md U4). The returned operation is an
// `Item_insert_remove_operation` inserting the resource prim under `parent` -
// any prim (C5), and the resource kind's `Scope` when null - so a resource
// enters the scene the way every other prim does, and its undo takes the prim
// out and announces it through `items_removed`
// (doc/import_undo_reference_clearing.md).
//
// The `gltf_source` / `image_source` / `asset_key` bookkeeping is the
// library's, keyed by the resource, and is recorded here rather than carried
// by the operation: it must survive an undo/redo cycle, which takes the prim
// out of the tree and puts it back.
[[nodiscard]] auto make_library_attach_operation(
    App_context&                                          context,
    const std::shared_ptr<Content_library>&               content_library,
    const std::shared_ptr<erhe::Item_base>&               item,
    const Gltf_source_reference&                          gltf_source,
    const std::shared_ptr<erhe::gltf::Gltf_image_source>& image_source = {},
    const std::optional<Asset_key>&                       asset_key    = {},
    const std::shared_ptr<erhe::Hierarchy>&               parent       = {}
) -> std::shared_ptr<Operation>;

// The undoable insert of a resource prim under its kind's `Scope` - the one
// step every resource creator takes, so that every one of them creates the
// kind scope the same way. The kind scope is lazy (Content_library U4), so
// when the scene has none standing the returned operation is a compound of
// `Kind_scope_operation` and the insert, in that order: undo takes the
// resource out first and the scope it needed after, and only while nothing
// else sits in it (see `Kind_scope_operation`). When the scope is already in
// the tree the returned operation is the bare insert.
[[nodiscard]] auto make_library_insert_operation(
    App_context&                            context,
    const std::shared_ptr<Content_library>& content_library,
    const std::shared_ptr<erhe::Item_base>& item
) -> std::shared_ptr<Operation>;

// The undoable insert of a new resource prim: the last child of `parent`,
// any prim (doc/usd_compatibility_design.md C5), or into the resource kind's
// `Scope` through `make_library_insert_operation` when `parent` is null. The
// content library indexes the resource wherever it sits. Every resource
// creator that takes an optional parent (the Create menu, the MCP `create_*`
// tools, `make_library_attach_operation`) builds its insert here.
[[nodiscard]] auto make_resource_insert_operation(
    App_context&                            context,
    const std::shared_ptr<Content_library>& content_library,
    const std::shared_ptr<erhe::Item_base>& item,
    const std::shared_ptr<erhe::Hierarchy>& parent
) -> std::shared_ptr<Operation>;

}
