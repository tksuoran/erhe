#pragma once

#include "assets/asset_key.hpp"
#include "scene/generated/gltf_source_reference.hpp"

#include <memory>
#include <optional>

namespace erhe {
    class Hierarchy;
    class Item_base;
}
namespace erhe::gltf {
    class Gltf_image_source;
}

namespace editor {

class App_context;
class Content_library;
class Operation;

// The undoable attach of a resource to a scene's content library
// (doc/usd-compatibility-plan.md U4). The returned operation is an
// `Item_insert_remove_operation` inserting the resource prim under `parent` -
// any prim (C5), and the resource kind's `Scope` when null - so a resource
// enters the scene the way every other prim does, and its undo takes the prim
// out and announces it through `items_removed`
// (doc/import-undo-reference-clearing.md).
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

}
