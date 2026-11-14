#include "navigation/navigation_seeds.hpp"

#include "app_context.hpp"
#include "editor_item_bits.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/item_set_flag_bits_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <limits>

namespace editor {

auto make_navigation_seed_operation(
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    const Navigation_seed_state                          state
) -> std::shared_ptr<Operation>
{
    const bool marked = (state == Navigation_seed_state::marked);
    std::vector<std::shared_ptr<erhe::Item_base>> changed_nodes;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node || !get_hosting_scene_root(node.get())) {
            continue;
        }
        const bool is_seed = ((node->get_flag_bits() & Editor_item_flags::navigation_seed) != 0u);
        if (is_seed != marked) {
            changed_nodes.push_back(node);
        }
    }
    if (changed_nodes.empty()) {
        return {};
    }
    return std::make_shared<Item_set_flag_bits_operation>(
        std::move(changed_nodes),
        Editor_item_flags::navigation_seed,
        marked,
        marked ? "Mark Navigation Seed" : "Unmark Navigation Seed"
    );
}

auto queue_mark_selected_navigation_seeds(App_context& context, Scene_root& scene_root, const Navigation_seed_state state) -> std::size_t
{
    std::vector<std::shared_ptr<erhe::Item_base>> nodes;
    for (const std::shared_ptr<erhe::Item_base>& item : context.selection->get_selected_items()) {
        if (item && (item->get_item_host() == static_cast<erhe::Item_host*>(&scene_root)) && erhe::is<erhe::scene::Node>(item.get())) {
            nodes.push_back(item);
        }
    }
    const std::shared_ptr<Operation> operation = make_navigation_seed_operation(nodes, state);
    if (operation) {
        context.operation_stack->queue(operation);
    }
    return nodes.size();
}

void select_navigation_seeds(App_context& context, Scene_root& scene_root)
{
    std::vector<std::shared_ptr<erhe::scene::Node>> seed_nodes;
    scene_root.get_navigation_seed_nodes(seed_nodes);
    std::vector<std::shared_ptr<erhe::Item_base>> selection;
    selection.reserve(seed_nodes.size());
    for (const std::shared_ptr<erhe::scene::Node>& node : seed_nodes) {
        selection.push_back(node);
    }
    context.selection->set_selection(selection);
}

auto get_navigation_seed_camera_position(App_context& context, const Scene_root& scene_root) -> std::optional<glm::vec3>
{
    if (context.scene_views == nullptr) {
        return {};
    }
    const std::shared_ptr<Viewport_scene_view> scene_view = context.scene_views->last_scene_view();
    if (!scene_view || (scene_view->get_scene_root().get() != &scene_root)) {
        return {};
    }
    const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
    if (!camera) {
        return {};
    }
    return glm::vec3{camera->position_in_world()};
}

auto queue_add_navigation_seed(App_context& context, Scene_root& scene_root, const glm::vec3& world_position) -> std::shared_ptr<erhe::scene::Node>
{
    const std::shared_ptr<erhe::scene::Node> root = scene_root.get_hosted_scene()->get_root_node();
    if (!root) {
        return {};
    }
    std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>("Navigation Seed");
    node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    // Set before the insert; Node::set_parent keeps the world transform.
    node->set_world_from_node(erhe::math::create_translation<float>(world_position));

    Compound_operation::Parameters compound_parameters;
    compound_parameters.operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = context,
                .item            = node,
                .parent          = std::static_pointer_cast<erhe::Hierarchy>(root),
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = std::numeric_limits<std::size_t>::max() // last child
            }
        )
    );
    compound_parameters.operations.push_back(
        std::make_shared<Item_set_flag_bits_operation>(
            std::vector<std::shared_ptr<erhe::Item_base>>{node},
            Editor_item_flags::navigation_seed,
            true,
            "Mark Navigation Seed"
        )
    );
    context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(compound_parameters)));
    return node;
}

} // namespace editor
