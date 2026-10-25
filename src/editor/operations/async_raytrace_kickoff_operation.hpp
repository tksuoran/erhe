#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <vector>

namespace erhe {
    class Item_base;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class App_context;
class Scene_root;

// Dispatches the background deferred finalize for one mesh's node. Used by
// the edit commit operations (Move_mesh_vertices, Paint_weights,
// Paint_colors) after their synchronous base-only rebuild: the finalize's
// re-optimize path builds the missing optimized variant on a worker and
// commits it frame-safely (meshoptimizer doc, requirements 10-11). Call only
// when the device supports worker contexts - the inline fallback would run
// the build on the main thread the caller is trying to keep free.
void kickoff_deferred_finalize(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh);

// On-demand full build of prepared primitives (doc/editor/async_asset_loading.md
// "Prepared mesh data"). A primitive loaded with prepared mesh data has no
// Geometry and no full buffer mesh (edge lines, points, solid-wireframe
// triangles, element mappings) until something needs them. The change sites
// that start needing them call these: the selection of a mesh (mesh
// component tools, paint, the geometry spreadsheet and properties all work
// on the selection), and a viewport style turning on a full-build mode.
// Each queues the existing deferred finalize task, with the full build of
// prepared primitives enabled, for every mesh among `items` (all scenes for
// the second) with a prepared primitive that has no edge lines yet. A task
// for a mesh that is already complete does nothing.
void request_prepared_full_builds(App_context& context, const std::vector<std::shared_ptr<erhe::Item_base>>& items);
void request_prepared_full_builds_in_all_scenes(App_context& context);

// Deferred base variants (load.base_variant on_demand,
// doc/plans/gltf_fast_load.md "Item 10"): a primitive whose load left its
// base variant a descriptor draws from the optimized variant until a feature
// needs the base; the sites that turn such a feature on call these, which
// queue a build of the base once per primitive: base_only (the soup build
// of the base, the optimized variant kept) for readers of the base buffers,
// full (the prepared full build: Geometry, edge lines, a new optimized
// variant) for in-place edits. The all-scenes request is base_only (DDGI,
// radiance cascades, GPU ray tracing, lightmap bakes).
// has_deferred_base() tells a consumer the base is not there yet.
enum class Deferred_base_build : unsigned int {
    base_only = 0,
    full      = 1
};
[[nodiscard]] auto has_deferred_base(const erhe::scene::Mesh& mesh) -> bool;
void request_deferred_base_build(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh, Deferred_base_build build);
void request_deferred_base_builds_in_all_scenes(App_context& context);

// Some viewport style draws a primitive mode only the full build provides
// (edge lines, corner points, polygon centroids, solid wireframe). A load
// then builds prepared primitives in full in its deferred finalize.
[[nodiscard]] auto viewports_need_full_builds(App_context& context) -> bool;

class Async_raytrace_kickoff_operation : public Operation
{
public:
    Async_raytrace_kickoff_operation(
        std::shared_ptr<Scene_root>                   scene_root,
        std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items
    );
    ~Async_raytrace_kickoff_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<Scene_root>                   m_scene_root;
    std::vector<std::shared_ptr<erhe::Item_base>> m_mesh_node_items;
};

}
