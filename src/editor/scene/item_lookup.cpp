#include "scene/item_lookup.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "content_library/style.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "scene/scene_root.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_node.hpp"

#include "erhe_graphics/texture.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

namespace {

// The prims of a subtree, and the attachments of the transformable ones.
// The scene TREE is walked rather than the registered node buckets: any prim
// may parent any other prim (doc/usd-compatibility-plan.md C5), and only the
// transformable prims are registered, so a Scope - and everything below one -
// is reachable this way alone.
template <typename Predicate>
auto find_prim_in_subtree(const std::shared_ptr<erhe::Hierarchy>& prim, Predicate&& matches) -> std::shared_ptr<erhe::Item_base>
{
    if (!prim) {
        return {};
    }
    if (matches(*prim)) {
        return prim;
    }
    if (erhe::is<erhe::scene::Node>(prim.get())) {
        const erhe::scene::Node* const node = static_cast<const erhe::scene::Node*>(prim.get());
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            if (attachment && matches(*attachment)) {
                return attachment;
            }
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : prim->get_children()) {
        const std::shared_ptr<erhe::Item_base> found = find_prim_in_subtree(child, matches);
        if (found) {
            return found;
        }
    }
    return {};
}

template <typename Predicate>
auto find_item_in_scene(Scene_root& scene_root, Predicate&& matches) -> std::shared_ptr<erhe::Item_base>
{
    std::shared_ptr<erhe::Item_base> result;
    const erhe::scene::Scene& scene = scene_root.get_scene();

    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root.get_scene_item();
    if (scene_item && matches(*scene_item)) {
        return scene_item;
    }

    // The root node included: it is a prim of the tree like any other.
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    result = find_prim_in_subtree(std::static_pointer_cast<erhe::Hierarchy>(root_node), matches);
    if (result) {
        return result;
    }

    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (library) {
        for (const std::shared_ptr<erhe::primitive::Material>& material : library->get_all<erhe::primitive::Material>()) {
            if (material && matches(*material)) {
                return material;
            }
        }
    }
    // Styles: the targets of every item's style property (doc/style-library.md D3).
    if (library) {
        for (const std::shared_ptr<Style>& style : library->get_all<Style>()) {
            if (style && matches(*style)) {
                return style;
            }
        }
    }
    // Textures: the targets of a material's texture slot properties (D28).
    if (library) {
        for (const std::shared_ptr<erhe::graphics::Texture>& texture : library->get_all<erhe::graphics::Texture>()) {
            if (texture && matches(*texture)) {
                return texture;
            }
        }
    }
    // Physics joint settings: the targets of a Node_joint's joint_settings
    // property (section 4.17).
    if (library) {
        for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : library->get_all<erhe::physics::Physics_joint_settings>()) {
            if (settings && matches(*settings)) {
                return settings;
            }
        }
    }
    // Animations: content-library items with computed properties
    // (section 4.16) the property tools address by id or name.
    if (library) {
        for (const std::shared_ptr<erhe::scene::Animation>& animation : library->get_all<erhe::scene::Animation>()) {
            if (animation && matches(*animation)) {
                return animation;
            }
        }
    }
    // Brushes: the targets of a Brush_placement's brush property
    // (section 4.11).
    if (library) {
        for (const std::shared_ptr<Brush>& brush : library->get_all<Brush>()) {
            if (brush && matches(*brush)) {
                return brush;
            }
        }
    }
    // Physics materials and collision filters: the targets of a
    // Node_physics' reference properties (section 4.10).
    if (library) {
        for (const std::shared_ptr<erhe::physics::Physics_material>& physics_material : library->get_all<erhe::physics::Physics_material>()) {
            if (physics_material && matches(*physics_material)) {
                return physics_material;
            }
        }
    }
    if (library) {
        for (const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter : library->get_all<erhe::physics::Collision_filter>()) {
            if (collision_filter && matches(*collision_filter)) {
                return collision_filter;
            }
        }
    }
    // Graph assets and their nodes: the nodes share the asset's host
    // (Graph_asset::set_item_host), so a D22 expression or an MCP property
    // call reaches a graph node the way it reaches a scene item.
    if (library) {
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : library->get_all<Graph_mesh>()) {
            if (!graph_mesh) {
                continue;
            }
            if (matches(*graph_mesh)) {
                return graph_mesh;
            }
            for (const std::shared_ptr<Geometry_graph_node>& node : graph_mesh->nodes()) {
                if (node && matches(*node)) {
                    return node;
                }
            }
        }
    }
    if (library) {
        for (const std::shared_ptr<Graph_texture>& graph_texture : library->get_all<Graph_texture>()) {
            if (!graph_texture) {
                continue;
            }
            if (matches(*graph_texture)) {
                return graph_texture;
            }
            for (const std::shared_ptr<Texture_graph_node>& node : graph_texture->nodes()) {
                if (node && matches(*node)) {
                    return node;
                }
            }
        }
    }
    return {};
}

} // anonymous namespace

auto find_item_in_scene_by_id(Scene_root& scene_root, const std::size_t id) -> std::shared_ptr<erhe::Item_base>
{
    return find_item_in_scene(scene_root, [id](const erhe::Item_base& item) { return item.get_id() == id; });
}

auto find_item_in_scene_by_name(Scene_root& scene_root, const std::string_view name) -> std::shared_ptr<erhe::Item_base>
{
    return find_item_in_scene(scene_root, [name](const erhe::Item_base& item) { return item.get_name() == name; });
}

auto find_item_in_scene_by_reference(Scene_root& scene_root, const std::string_view name_or_path) -> std::shared_ptr<erhe::Item_base>
{
    if (name_or_path.find('/') != std::string_view::npos) {
        const std::shared_ptr<erhe::scene::Node> root_node = scene_root.get_scene().get_root_node();
        if (root_node) {
            erhe::Hierarchy* const node = erhe::find_by_path(*root_node, name_or_path);
            if (node != nullptr) {
                return node->shared_from_this();
            }
        }
    }
    return find_item_in_scene_by_name(scene_root, name_or_path);
}

auto resolve_reference_by_name(App_context& context, const erhe::Item_base& from, const std::string_view name_or_path) -> std::shared_ptr<erhe::Item_base>
{
    Scene_root* const scene_root = find_scene_root_for_item(context, from);
    if (scene_root == nullptr) {
        return {};
    }
    return find_item_in_scene_by_reference(*scene_root, name_or_path);
}

auto find_scene_root_for_item(App_context& context, const erhe::Item_base& item) -> Scene_root*
{
    if (context.app_scenes == nullptr) {
        return nullptr;
    }
    const erhe::Item_host* const host = item.get_item_host();
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        const erhe::Item_host* const candidate = scene_root.get();
        if (host != nullptr) {
            if (host == candidate) {
                return scene_root.get();
            }
            continue;
        }
        // An unhosted item: the scene whose content library lists it (a
        // resource this scene references but does not own), else the scene
        // the manager records as defining it.
        const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
        if (library && library->has_item(item)) {
            return scene_root.get();
        }
        if ((context.asset_manager != nullptr) && context.asset_manager->is_defined_by(item, candidate)) {
            return scene_root.get();
        }
    }
    return nullptr;
}

void collect_reference_candidates(
    App_context&                                   context,
    const erhe::Item_base&                         target,
    const uint64_t                                 item_types,
    std::vector<std::shared_ptr<erhe::Item_base>>& out
)
{
    out.clear();
    Scene_root* const scene_root = find_scene_root_for_item(context, target);
    if (scene_root == nullptr) {
        return;
    }
    const bool developer_mode = context.developer_mode;
    const auto consider = [&out, &target, item_types, developer_mode](const std::shared_ptr<erhe::Item_base>& item) {
        if (!item || ((item->get_type() & item_types) == 0)) {
            return;
        }
        // A style is offered only where it applies (doc/style-library.md R3)
        // and only when it would not form a style chain cycle - a style has a
        // style of its own, so it is never a candidate for itself or for
        // anything already on its chain (D25 style chain).
        if ((item->get_type() & erhe::Item_type::style) != 0) {
            if (!erhe::Item_base::style_applies(*item, target)) {
                return;
            }
            if (item->style_chain_reaches(target)) {
                return;
            }
        }
        const bool shown =
            item->is_shown_in_ui() ||
            (developer_mode && ((item->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
        if (shown) {
            out.push_back(item);
        }
    };

    // Resource prims are prims of the scene tree, so the node walk below
    // reaches the ones placed there; a referenced listing (a resource another
    // container owns) is not in the tree and is offered from the index.
    const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
    if (library) {
        for (const uint64_t kind_type_bit : Content_library::get_kind_type_bits()) {
            for (const std::shared_ptr<erhe::Item_base>& item : library->get_all_of_kind(kind_type_bit)) {
                consider(item);
            }
        }
    }

    // Scene nodes and their attachments, for a node-typed (or mesh-, camera-,
    // light-typed) reference. The root node is not in the transform-update
    // buckets for_each_node visits, so it is considered on its own.
    const erhe::scene::Scene& scene = scene_root->get_scene();
    consider(scene.get_root_node());
    scene.for_each_node(
        [&consider](const std::shared_ptr<erhe::scene::Node>& node) {
            consider(node);
            for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                consider(attachment);
            }
            return true;
        }
    );
}

}
