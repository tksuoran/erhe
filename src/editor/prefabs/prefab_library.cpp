#include "prefabs/prefab_library.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "prefabs/prefab_instance.hpp"
#include "scene/generated/gltf_source_reference.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace editor {

namespace {

// Point meshes in the subtree at the given content layer (prefab templates
// are parsed without a destination scene), refresh their raytrace
// primitives, and collect the mesh-carrying nodes when requested (for the
// raytrace kickoff operation).
void retarget_meshes(
    const std::shared_ptr<erhe::scene::Node>&      node,
    const erhe::scene::Layer_id                    content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
    if (mesh) {
        mesh->layer_id = content_layer_id;
        mesh->update_rt_primitives();
        if (out_mesh_node_items != nullptr) {
            out_mesh_node_items->push_back(node);
        }
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node) {
            retarget_meshes(child_node, content_layer_id, out_mesh_node_items);
        }
    }
}

} // namespace

Prefab_library::Prefab_library(App_context& context)
    : m_context{context}
{
}

auto Prefab_library::get_prefabs() const -> const std::map<std::filesystem::path, std::shared_ptr<Prefab>>&
{
    return m_prefabs;
}

auto Prefab_library::get_or_load(const std::filesystem::path& path) -> std::shared_ptr<Prefab>
{
    std::error_code error_code;
    std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
    if (error_code) {
        canonical_path = path;
    }

    const auto existing = m_prefabs.find(canonical_path);
    if (existing != m_prefabs.end()) {
        return existing->second;
    }

    // glTF 2.1 strictly prohibits cyclical references between assets; a
    // cycle here would otherwise recurse forever through external-asset
    // resolution.
    const auto cycle = std::find(m_active_load_stack.begin(), m_active_load_stack.end(), canonical_path);
    if (cycle != m_active_load_stack.end()) {
        std::string cycle_description;
        for (auto i = cycle; i != m_active_load_stack.end(); ++i) {
            cycle_description += erhe::file::to_string(*i);
            cycle_description += " -> ";
        }
        cycle_description += erhe::file::to_string(canonical_path);
        log_parsers->error("Prefab reference cycle detected (prohibited by glTF 2.1): {}", cycle_description);
        return {};
    }

    const bool exists = std::filesystem::exists(canonical_path, error_code);
    if (!exists || error_code) {
        log_parsers->error("Prefab source file not found: {}", erhe::file::to_string(canonical_path));
        return {};
    }

    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY(m_context.executor != nullptr);
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);

    m_active_load_stack.push_back(canonical_path);

    std::shared_ptr<Prefab> prefab = std::make_shared<Prefab>();
    prefab->source_path   = canonical_path;
    prefab->name          = erhe::file::to_string(canonical_path.filename());
    prefab->holding_scene = std::make_shared<erhe::scene::Scene>(fmt::format("prefab holding scene: {}", prefab->name), nullptr);
    prefab->template_root = std::make_shared<erhe::scene::Node>(prefab->name);
    prefab->template_root->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    prefab->template_root->set_parent(prefab->holding_scene->get_root_node());

    erhe::gltf::Image_transfer image_transfer{*m_context.graphics_device, *m_context.current_command_buffer};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = *m_context.graphics_device,
        .executor        = *m_context.executor,
        .image_transfer  = image_transfer,
        .root_node       = prefab->template_root,
        .mesh_layer_id   = 0, // instances are retargeted to the destination scene's content layer
        .path            = canonical_path,
    };
    prefab->gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    const bool has_nodes = std::any_of(
        prefab->gltf_data.nodes.begin(),
        prefab->gltf_data.nodes.end(),
        [](const std::shared_ptr<erhe::scene::Node>& node) { return static_cast<bool>(node); }
    );
    if (!has_nodes) {
        m_active_load_stack.pop_back();
        log_parsers->error("Prefab '{}' produced no nodes - not caching", erhe::file::to_string(canonical_path));
        return {};
    }

    finalize_imported_meshes(m_context, make_import_build_info(m_context), prefab->gltf_data, nullptr);

    // glTF 2.1: resolve external assets inside the template, so instance
    // clones reproduce nested content. This runs while this path is still
    // on the active load stack, so reference cycles through the recursive
    // get_or_load are detected.
    resolve_external_assets(*this, prefab->gltf_data, 0, nullptr);

    m_active_load_stack.pop_back();

    if (!prefab->gltf_data.skins.empty() || !prefab->gltf_data.animations.empty()) {
        log_parsers->warn(
            "Prefab '{}' contains skins/animations; instances are static for now (joint/animation target remapping is not implemented)",
            prefab->name
        );
    }

    m_prefabs.emplace(canonical_path, prefab);
    log_parsers->info("Prefab loaded: {}", erhe::file::to_string(canonical_path));
    return prefab;
}

auto instantiate_prefab(
    App_context&                              context,
    const std::shared_ptr<Prefab>&            prefab,
    Scene_root&                               scene_root,
    const glm::mat4&                          world_from_node,
    const std::shared_ptr<erhe::scene::Node>& parent
) -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_VERIFY(prefab);

    constexpr uint64_t node_flags =
        erhe::Item_flags::visible |
        erhe::Item_flags::content |
        erhe::Item_flags::expand  |
        erhe::Item_flags::show_in_ui;

    std::shared_ptr<erhe::scene::Node> instance_root = std::make_shared<erhe::scene::Node>(prefab->name);
    instance_root->enable_flag_bits(node_flags);
    instance_root->set_world_from_node(world_from_node);

    // Clone (including nested prefab content), retarget to the destination
    // scene's content layer, and collect the mesh nodes for the raytrace
    // kickoff.
    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    attach_prefab_instance(prefab, instance_root, scene_root.layers().content()->id, &mesh_node_items);

    // Register the prefab's shared resources in the destination scene's
    // content library (idempotent per resource; mirrors import_gltf).
    std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    const std::string gltf_path_str = prefab->source_path.generic_string();

    std::vector<std::shared_ptr<Operation>> operations;

    for (size_t i = 0; i < prefab->gltf_data.images.size(); ++i) {
        const std::shared_ptr<erhe::graphics::Texture>& image = prefab->gltf_data.images[i];
        if (image) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::graphics::Texture>>(
                    content_library,
                    content_library->textures,
                    image,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = image->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "texture",
                    }
                )
            );
        }
    }

    for (size_t i = 0; i < prefab->gltf_data.materials.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material>& material = prefab->gltf_data.materials[i];
        if (material) {
            operations.push_back(
                std::make_shared<Content_library_attach_operation<erhe::primitive::Material>>(
                    content_library,
                    content_library->materials,
                    material,
                    Gltf_source_reference{
                        .gltf_path  = gltf_path_str,
                        .item_name  = material->get_name(),
                        .item_index = static_cast<int>(i),
                        .item_type  = "material",
                    }
                )
            );
        }
    }

    std::shared_ptr<erhe::scene::Node> insert_parent = parent;
    if (!insert_parent) {
        insert_parent = scene_root.get_hosted_scene()->get_root_node();
    }

    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = context,
                .item    = instance_root,
                .parent  = insert_parent,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    operations.push_back(
        std::make_shared<Async_raytrace_kickoff_operation>(
            std::dynamic_pointer_cast<Scene_root>(scene_root.shared_from_this()),
            std::move(mesh_node_items)
        )
    );

    std::shared_ptr<Compound_operation> compound = std::make_shared<Compound_operation>(
        Compound_operation::Parameters{.operations = std::move(operations)}
    );
    compound->set_description(
        fmt::format("[{}] Instantiate prefab {}", compound->get_serial(), prefab->name)
    );
    context.operation_stack->queue(compound);

    return instance_root;
}

namespace {

void collect_prefab_external_assets_visit(
    const erhe::scene::Node&                                                 node,
    const std::filesystem::path&                                             export_directory,
    std::map<const erhe::scene::Node*, erhe::gltf::Gltf_export_external_asset>& result
)
{
    const std::shared_ptr<Prefab_instance> prefab_instance = erhe::scene::get_attachment<Prefab_instance>(&node);
    if (prefab_instance) {
        const std::filesystem::path& source_path = prefab_instance->get_prefab_source_path();
        std::error_code error_code;
        const std::filesystem::path relative_path = std::filesystem::relative(source_path, export_directory, error_code);
        const std::string uri = (error_code || relative_path.empty())
            ? source_path.generic_string()
            : relative_path.generic_string();
        const bool is_binary = source_path.extension() == std::filesystem::path{".glb"};
        result.emplace(
            &node,
            erhe::gltf::Gltf_export_external_asset{
                .uri       = uri,
                .mime_type = is_binary ? "model/gltf-binary" : "model/gltf+json",
                .name      = prefab_instance->get_prefab_name()
            }
        );
        return; // instance content lives in the referenced file
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : node.get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node) {
            collect_prefab_external_assets_visit(*child_node, export_directory, result);
        }
    }
}

} // namespace

auto collect_prefab_external_assets(
    const erhe::scene::Node&     root_node,
    const std::filesystem::path& export_directory
) -> std::map<const erhe::scene::Node*, erhe::gltf::Gltf_export_external_asset>
{
    std::map<const erhe::scene::Node*, erhe::gltf::Gltf_export_external_asset> result;
    collect_prefab_external_assets_visit(root_node, export_directory, result);
    return result;
}

void resolve_external_assets(
    Prefab_library&                                prefab_library,
    const erhe::gltf::Gltf_data&                   gltf_data,
    const erhe::scene::Layer_id                    content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
)
{
    for (std::size_t node_index = 0, end = gltf_data.node_external_assets.size(); node_index < end; ++node_index) {
        const std::optional<std::size_t>& external_asset_index_opt = gltf_data.node_external_assets[node_index];
        if (!external_asset_index_opt.has_value()) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> carrier =
            (node_index < gltf_data.nodes.size()) ? gltf_data.nodes[node_index] : std::shared_ptr<erhe::scene::Node>{};
        if (!carrier) {
            continue; // node outside the parsed scene
        }
        const std::size_t external_asset_index = external_asset_index_opt.value();
        if (external_asset_index >= gltf_data.external_assets.size()) {
            log_parsers->error(
                "glTF node '{}': externalAsset index {} out of range ({} entries)",
                carrier->get_name(), external_asset_index, gltf_data.external_assets.size()
            );
            continue;
        }
        const erhe::gltf::Gltf_external_asset& external_asset = gltf_data.external_assets[external_asset_index];
        if (external_asset.file_index >= gltf_data.files.size()) {
            log_parsers->error(
                "glTF external asset '{}': file index {} out of range ({} entries)",
                external_asset.name, external_asset.file_index, gltf_data.files.size()
            );
            continue;
        }
        const erhe::gltf::Gltf_file_reference& file = gltf_data.files[external_asset.file_index];
        if (file.embedded || file.resolved_path.empty()) {
            log_parsers->error(
                "glTF external asset '{}': embedded or unresolved file references are not supported yet",
                external_asset.name
            );
            continue;
        }
        const std::shared_ptr<Prefab> prefab = prefab_library.get_or_load(file.resolved_path);
        if (!prefab) {
            log_parsers->error(
                "glTF external asset '{}': failed to load {} (missing file, no nodes, or reference cycle - see log)",
                external_asset.name, erhe::file::to_string(file.resolved_path)
            );
            continue;
        }
        attach_prefab_instance(prefab, carrier, content_layer_id, out_mesh_node_items);
    }
}

// Mark the node as a prefab instance and clone the prefab's template
// subtree under it. Mesh clones share the template's Primitives (GPU
// vertex/index ranges in Mesh_memory), so no GPU upload happens per
// instance.
void attach_prefab_instance(
    const std::shared_ptr<Prefab>&                 prefab,
    const std::shared_ptr<erhe::scene::Node>&      node,
    const erhe::scene::Layer_id                    content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items
)
{
    std::shared_ptr<Prefab_instance> prefab_instance = std::make_shared<Prefab_instance>(prefab->source_path, prefab->name);
    prefab_instance->enable_flag_bits(erhe::Item_flags::visible | erhe::Item_flags::no_message | erhe::Item_flags::show_in_ui);
    node->attach(prefab_instance);

    for (const std::shared_ptr<erhe::Hierarchy>& child : prefab->template_root->get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (!child_node) {
            continue;
        }
        const std::shared_ptr<erhe::Item_base> clone = child_node->clone();
        const std::shared_ptr<erhe::scene::Node> clone_node = std::dynamic_pointer_cast<erhe::scene::Node>(clone);
        if (!clone_node) {
            log_parsers->warn("Prefab '{}': template child '{}' could not be cloned", prefab->name, child_node->get_name());
            continue;
        }
        // Retarget meshes BEFORE parenting: node may already be hosted in a
        // live scene (.erhescene load), and parenting registers meshes into
        // the scene by their layer_id at that moment (Scene::register_mesh).
        // With the template's placeholder layer id 0 they would silently
        // land in the brush layer (Mesh_layer_id::brush == 0) and never
        // render as content.
        retarget_meshes(clone_node, content_layer_id, out_mesh_node_items);
        // Node::set_parent preserves the world transform by rewriting the
        // local transform; a prefab clone must instead keep its local
        // (template) transform under its new parent, so restore it after
        // parenting.
        const erhe::scene::Trs_transform parent_from_node = clone_node->parent_from_node_transform();
        clone_node->set_parent(node);
        clone_node->set_parent_from_node(parent_from_node);
    }
}

}
