#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace erhe { class Item_base; }
namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class App_context;
class Operation;
class Scene_root;

// The navigation seed change sites (doc/editor/navigation.md "Seeds"). They
// compile in every build: seeds need neither Recast nor Detour.
enum class Navigation_seed_state : unsigned int
{
    unmarked = 0,
    marked
};

// The undoable flag operation that gives `state` to the nodes of `items` whose
// state differs (non-nodes and nodes outside a scene are skipped); null when none differs.
[[nodiscard]] auto make_navigation_seed_operation(
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    Navigation_seed_state                                state
) -> std::shared_ptr<Operation>;

// Queues make_navigation_seed_operation() for the selected nodes of `scene_root`; returns their count.
auto queue_mark_selected_navigation_seeds(App_context& context, Scene_root& scene_root, Navigation_seed_state state) -> std::size_t;

// Replaces the selection with the scene's seed nodes.
void select_navigation_seeds(App_context& context, Scene_root& scene_root);

// The world position of the last hovered viewport's camera when that viewport shows `scene_root`.
[[nodiscard]] auto get_navigation_seed_camera_position(App_context& context, const Scene_root& scene_root) -> std::optional<glm::vec3>;

// Queues one compound operation: a new empty node "Navigation Seed" under the
// scene root at `world_position`, flagged as a seed. Returns the node.
auto queue_add_navigation_seed(App_context& context, Scene_root& scene_root, const glm::vec3& world_position) -> std::shared_ptr<erhe::scene::Node>;

} // namespace editor
