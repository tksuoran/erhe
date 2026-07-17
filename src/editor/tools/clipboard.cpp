#include "tools/clipboard.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "app_message_bus.hpp"
#include "items.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_profile//profile.hpp"

#include <algorithm>

namespace editor {

namespace {

// Collects, without duplicates, the materials of every mesh in the subtree
// that are not hosted by any scene - the pasted content whose ownership the
// paste site must decide (R5.2b: mesh registration never claims ownership).
void collect_unhosted_materials(
    const std::shared_ptr<erhe::Hierarchy>&                  hierarchy,
    std::vector<std::shared_ptr<erhe::primitive::Material>>& out_materials
)
{
    if (!hierarchy) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(hierarchy);
    if (node) {
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(attachment);
            if (!mesh) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& primitive : mesh->get_primitives()) {
                const std::shared_ptr<erhe::primitive::Material>& material = primitive.material;
                if (!material || (material->get_item_host() != nullptr)) {
                    continue;
                }
                if (std::find(out_materials.begin(), out_materials.end(), material) == out_materials.end()) {
                    out_materials.push_back(material);
                }
            }
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
        collect_unhosted_materials(child, out_materials);
    }
}

// Collects everything a held clipboard item keeps alive: the item subtree
// itself plus, transitively, node attachments, mesh materials, skins and
// material textures. See Clipboard::collect_pinned_items.
void collect_clipboard_pins(const std::shared_ptr<erhe::Item_base>& item, std::unordered_set<const erhe::Item_base*>& out_pinned)
{
    if (!item) {
        return;
    }
    out_pinned.insert(item.get());
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
    if (node) {
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            out_pinned.insert(attachment.get());
            const std::shared_ptr<erhe::scene::Mesh> mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(attachment);
            if (!mesh) {
                continue;
            }
            if (mesh->skin) {
                out_pinned.insert(mesh->skin.get());
            }
            for (const erhe::scene::Mesh_primitive& primitive : mesh->get_primitives()) {
                const std::shared_ptr<erhe::primitive::Material>& material = primitive.material;
                if (!material) {
                    continue;
                }
                out_pinned.insert(material.get());
                const erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
                for (const erhe::primitive::Material_texture_sampler* sampler : {&samplers.base_color, &samplers.metallic_roughness, &samplers.normal, &samplers.occlusion, &samplers.emissive}) {
                    const erhe::Item_base* const texture_item = dynamic_cast<const erhe::Item_base*>(sampler->texture_reference.get());
                    if (texture_item != nullptr) {
                        out_pinned.insert(texture_item);
                    }
                }
            }
        }
    }
    const std::shared_ptr<erhe::Hierarchy> hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
    if (hierarchy) {
        for (const std::shared_ptr<erhe::Hierarchy>& child : hierarchy->get_children()) {
            collect_clipboard_pins(child, out_pinned);
        }
    }
}

// The loaded prefab whose template owns the given material, if any: a pasted
// prefab-instance subtree carries the template's shared, deliberately
// unhosted materials.
auto find_owning_prefab(
    App_context&                                      context,
    const std::shared_ptr<erhe::primitive::Material>& material
) -> std::shared_ptr<Prefab>
{
    if (context.prefab_library == nullptr) {
        return {};
    }
    for (const auto& [path, prefab] : context.prefab_library->get_prefabs()) {
        if (!prefab) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = prefab->gltf_data.materials;
        if (std::find(materials.begin(), materials.end(), material) != materials.end()) {
            return prefab;
        }
    }
    return {};
}

} // namespace

Clipboard_paste_command::Clipboard_paste_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Clipboard.paste"}
    , m_context{context}
{
}

void Clipboard_paste_command::try_ready()
{
    if (m_context.clipboard->try_ready()) {
        set_ready();
    }
}

auto Clipboard_paste_command::try_call() -> bool
{
    return m_context.clipboard->try_paste();
}

Clipboard::Clipboard(erhe::commands::Commands& commands, App_context& context, App_message_bus& app_message_bus)
    : m_paste_command{commands, context}
    , m_context      {context}
{
    ERHE_PROFILE_FUNCTION();

    commands.register_command(&m_paste_command);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_insert, true, erhe::window::Key_modifier_bit_shift);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_v,      true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_menu(&m_paste_command, "Edit.Paste");
    m_paste_command.set_host(this);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            // A closed viewport's Scene_view is being destroyed: drop any cached
            // pointer to it so try_ready() / get_hierarchy() do not dereference a
            // dead Scene_view (#256).
            if (message.destroyed_scene_view != nullptr) {
                if (m_hover_scene_view == message.destroyed_scene_view) {
                    m_hover_scene_view = nullptr;
                }
                if (m_last_hover_scene_view == message.destroyed_scene_view) {
                    m_last_hover_scene_view = nullptr;
                }
            }
            m_hover_scene_view = message.scene_view;
            if (message.scene_view != nullptr) {
                m_last_hover_scene_view = message.scene_view;
                m_last_hover_scene_item_tree = nullptr;
            }
        }
    );
    m_hover_scene_item_tree_subscription = app_message_bus.hover_scene_item_tree.subscribe(
        [&](Hover_scene_item_tree_message& message) {
            m_last_hover_scene_item_tree = message.scene_root.get();
            if (message.scene_root) {
                m_last_hover_scene_view = nullptr;
            }
        }
    );
}

auto Clipboard::try_ready() -> bool
{
    // TODO Paste into content library
    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    const auto target_node = get<erhe::scene::Node>(selected_items);
    return !m_contents.empty() && target_node;
}

[[nodiscard]] auto Clipboard::resolve_paste_target() -> std::shared_ptr<erhe::Hierarchy>
{
    if (m_last_hover_scene_item_tree != nullptr) {
        const std::shared_ptr<erhe::Hierarchy>& root = m_last_hover_scene_item_tree->get_scene().get_root_node();
        if (root) {
            return root;
        }
    }

    Selection& selection = *m_context.selection;
    // Paste is a command: it targets the active scene's selection (plus
    // non-hosted items), never a hierarchy selected in another scene.
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_command_target_selection();
    const std::shared_ptr<erhe::Hierarchy>& selected_hierarchy = get<erhe::Hierarchy>(selected_items);
    if (selected_hierarchy) {
        return selected_hierarchy;
    }

    const std::shared_ptr<erhe::Hierarchy>& last_selected_hierarchy = selection.get_last_selected<erhe::Hierarchy>();
    if (last_selected_hierarchy) {
        return last_selected_hierarchy;
    }

    if (m_last_hover_scene_view != nullptr) {
        const std::shared_ptr<Scene_root> scene_root = m_last_hover_scene_view->get_scene_root();
        if (scene_root) {
            const erhe::scene::Scene& scene = scene_root->get_scene();
            std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
            const std::shared_ptr<erhe::Hierarchy>& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(root_node);
            if (hierarchy) {
                return hierarchy;
            }
        }
    }
    return {};
}

auto Clipboard::try_paste() -> bool
{
    const std::shared_ptr<erhe::Hierarchy>& target = resolve_paste_target();
    if (!target) {
        return false;
    }
    const std::size_t index_in_parent = target->get_child_count();
    return try_paste(target, index_in_parent);
}

auto Clipboard::try_paste(const std::shared_ptr<erhe::Hierarchy>& target_parent, std::size_t index_in_parent) -> bool
{
    if (!target_parent) {
        return false;
    }
    // TODO Paste into content library

    Compound_operation::Parameters compound_parameters{};

    std::vector<std::shared_ptr<erhe::Hierarchy>> pasted_hierarchies;
    for (const auto& item : m_contents) {
        const auto& src_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
        if (!src_hierarchy) {
            continue;
        }
        std::shared_ptr<erhe::Item_base> clone_of_item = item->clone();
        const auto& dst_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(clone_of_item);

        // Clones keep the source name; pasting is where a duplicate
        // wants a distinguishing name, so rename the pasted root here
        // (descendants keep their names).
        clone_of_item->set_name(item->get_name() + " Copy");

        pasted_hierarchies.push_back(dst_hierarchy);
    }

    // Ownership of materials carried by the pasted meshes is decided HERE
    // (R5.2b: mesh registration never claims ownership). Per unhosted
    // material (a hosted one keeps its owner and gets a reference listing
    // from register_mesh):
    // - already listed in the target scene's library (e.g. a prefab template
    //   resource the scene already references): nothing to do;
    // - owned by a loaded prefab template (pasted prefab-instance subtree):
    //   list the template's resources as reference entries, exactly like
    //   scene load / instantiate do (direct, not undoable - matching
    //   replace_content_library_entries semantics);
    // - otherwise the source scene is gone: the target scene claims the
    //   definition, undoably with the paste itself and BEFORE the node
    //   insert, so register_mesh sees a hosted material.
    const std::shared_ptr<erhe::scene::Node> target_node = std::dynamic_pointer_cast<erhe::scene::Node>(target_parent);
    Scene_root* const target_scene_root = target_node ? static_cast<Scene_root*>(target_node->get_item_host()) : nullptr;
    if (target_scene_root != nullptr) {
        std::vector<std::shared_ptr<erhe::primitive::Material>> unhosted_materials;
        for (const std::shared_ptr<erhe::Hierarchy>& pasted_hierarchy : pasted_hierarchies) {
            collect_unhosted_materials(pasted_hierarchy, unhosted_materials);
        }
        const std::shared_ptr<Content_library> content_library = target_scene_root->get_content_library();
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
        for (const std::shared_ptr<erhe::primitive::Material>& material : unhosted_materials) {
            if (content_library->materials->has_item(*material)) {
                continue;
            }
            const std::shared_ptr<Prefab> owning_prefab = find_owning_prefab(m_context, material);
            if (owning_prefab) {
                add_prefab_reference_entries(*content_library, *owning_prefab);
                continue;
            }
            log_scene->info(
                "Paste re-registers material '{}' into scene '{}': its source scene is gone, so the"
                " target scene becomes the defining scene (undo removes the definition again).",
                material->get_name(),
                target_scene_root->get_name()
            );
            compound_parameters.operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::primitive::Material>>(
                    content_library,
                    content_library->materials,
                    material,
                    Gltf_source_reference{
                        .item_name = material->get_name(),
                        .item_type = "material"
                    }
                )
            );
        }
    }

    for (const std::shared_ptr<erhe::Hierarchy>& pasted_hierarchy : pasted_hierarchies) {
        compound_parameters.operations.push_back(
            std::make_shared<Item_insert_remove_operation>(
                Item_insert_remove_operation::Parameters{
                    .context         = m_context,
                    .item            = pasted_hierarchy,
                    .parent          = target_parent,
                    .mode            = Item_insert_remove_operation::Mode::insert,
                    .index_in_parent = index_in_parent
                }
            )
        );
    }
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(std::move(compound_parameters))
    );

    return true; // TODO
}

void Clipboard::set_contents(const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    m_contents = items;
}

void Clipboard::set_contents(const std::shared_ptr<erhe::Item_base>& item)
{
    m_contents.clear();
    m_contents.push_back(item);
}

auto Clipboard::get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_contents;
}

void Clipboard::collect_pinned_items(std::unordered_set<const erhe::Item_base*>& out_pinned) const
{
    for (const std::shared_ptr<erhe::Item_base>& item : m_contents) {
        collect_clipboard_pins(item, out_pinned);
    }
}

}
