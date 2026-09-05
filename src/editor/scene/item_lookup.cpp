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

template <typename Predicate>
auto find_item_in_scene(Scene_root& scene_root, Predicate&& matches) -> std::shared_ptr<erhe::Item_base>
{
    std::shared_ptr<erhe::Item_base> result;
    const erhe::scene::Scene& scene = scene_root.get_scene();

    const std::shared_ptr<erhe::scene::Scene> scene_item = scene_root.get_scene_item();
    if (scene_item && matches(*scene_item)) {
        return scene_item;
    }

    // The root node is not registered in the transform-update buckets that
    // for_each_node visits.
    const std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
    if (root_node && matches(*root_node)) {
        return root_node;
    }

    scene.for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& node) {
            if (matches(*node)) {
                result = node;
                return false;
            }
            for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                if (attachment && matches(*attachment)) {
                    result = attachment;
                    return false;
                }
            }
            return true;
        }
    );
    if (result) {
        return result;
    }

    const std::shared_ptr<Content_library> library = scene_root.get_content_library();
    if (library && library->materials) {
        for (const std::shared_ptr<erhe::primitive::Material>& material : library->materials->get_all<erhe::primitive::Material>()) {
            if (material && matches(*material)) {
                return material;
            }
        }
    }
    // Styles: the targets of every item's style property (doc/style-library.md D3).
    if (library && library->styles) {
        for (const std::shared_ptr<Style>& style : library->styles->get_all<Style>()) {
            if (style && matches(*style)) {
                return style;
            }
        }
    }
    // Textures: the targets of a material's texture slot properties (D28).
    if (library && library->textures) {
        for (const std::shared_ptr<erhe::graphics::Texture>& texture : library->textures->get_all<erhe::graphics::Texture>()) {
            if (texture && matches(*texture)) {
                return texture;
            }
        }
    }
    // Physics joint settings: the targets of a Node_joint's joint_settings
    // property (section 4.17).
    if (library && library->physics_joints) {
        for (const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings : library->physics_joints->get_all<erhe::physics::Physics_joint_settings>()) {
            if (settings && matches(*settings)) {
                return settings;
            }
        }
    }
    // Animations: content-library items with computed properties
    // (section 4.16) the property tools address by id or name.
    if (library && library->animations) {
        for (const std::shared_ptr<erhe::scene::Animation>& animation : library->animations->get_all<erhe::scene::Animation>()) {
            if (animation && matches(*animation)) {
                return animation;
            }
        }
    }
    // Brushes: the targets of a Brush_placement's brush property
    // (section 4.11).
    if (library && library->brushes) {
        for (const std::shared_ptr<Brush>& brush : library->brushes->get_all<Brush>()) {
            if (brush && matches(*brush)) {
                return brush;
            }
        }
    }
    // Physics materials and collision filters: the targets of a
    // Node_physics' reference properties (section 4.10).
    if (library && library->physics_materials) {
        for (const std::shared_ptr<erhe::physics::Physics_material>& physics_material : library->physics_materials->get_all<erhe::physics::Physics_material>()) {
            if (physics_material && matches(*physics_material)) {
                return physics_material;
            }
        }
    }
    if (library && library->collision_filters) {
        for (const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter : library->collision_filters->get_all<erhe::physics::Collision_filter>()) {
            if (collision_filter && matches(*collision_filter)) {
                return collision_filter;
            }
        }
    }
    // Graph assets and their nodes: the nodes share the asset's host
    // (Graph_asset::set_item_host), so a D22 expression or an MCP property
    // call reaches a graph node the way it reaches a scene item.
    if (library && library->graph_meshes) {
        for (const std::shared_ptr<Graph_mesh>& graph_mesh : library->graph_meshes->get_all<Graph_mesh>()) {
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
    if (library && library->graph_textures) {
        for (const std::shared_ptr<Graph_texture>& graph_texture : library->graph_textures->get_all<Graph_texture>()) {
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
    // Content-library nodes themselves - folders above all
    // (doc/content-library-folders.md D7), so the property tools address a
    // folder by id or name; an entry node shares its item's name, and the
    // item is found first above.
    if (library && library->root) {
        std::shared_ptr<erhe::Item_base> found_node{};
        library->root->for_each<Content_library_node>(
            [&found_node, &matches](Content_library_node& node) -> bool {
                if (matches(node)) {
                    found_node = node.shared_from_this();
                    return false;
                }
                return true;
            }
        );
        if (found_node) {
            return found_node;
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
        const std::shared_ptr<Content_library>& library = scene_root.get_content_library();
        if (library && library->root) {
            erhe::Hierarchy* const found = erhe::find_by_path(*library->root, name_or_path);
            Content_library_node* const library_node = dynamic_cast<Content_library_node*>(found);
            if (library_node != nullptr) {
                // An entry node carries the item the path names; a folder
                // node is itself the addressed item.
                return library_node->item ? library_node->item : library_node->shared_from_this();
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
        // An unhosted item (an asset-typed one): the scene whose content
        // library lists it, else the scene the manager records as defining it.
        // A library node itself (a folder holding a reference property,
        // doc/content-library-folders.md D8) belongs to its library's scene.
        const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
        const Content_library_node* const library_node = dynamic_cast<const Content_library_node*>(&item);
        if ((library_node != nullptr) && library && (library_node->get_library() == library.get())) {
            return scene_root.get();
        }
        if (library && library->root && library->root->has_item(item)) {
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
        // A style is offered only where it applies (doc/style-library.md R3).
        if (((item->get_type() & erhe::Item_type::style) != 0) && !erhe::Item_base::style_applies(*item, target)) {
            return;
        }
        const bool shown =
            item->is_shown_in_ui() ||
            (developer_mode && ((item->get_flag_bits() & erhe::Item_flags::show_in_developer_ui) != 0));
        if (shown) {
            out.push_back(item);
        }
    };

    const std::shared_ptr<Content_library>& library = scene_root->get_content_library();
    if (library && library->root) {
        library->root->for_each_const<Content_library_node>(
            [&consider](const Content_library_node& node) -> bool {
                consider(node.item);
                return true;
            }
        );
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
