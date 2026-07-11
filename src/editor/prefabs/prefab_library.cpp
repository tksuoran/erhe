#include "prefabs/prefab_library.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/content_library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "parsers/gltf_physics_export.hpp"
#include "prefabs/prefab_instance.hpp"
#include "scene/generated/gltf_source_reference.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <mutex>

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

// Seal a cloned instance subtree: interior nodes and their attachments are
// not user-editable (editing prefab content requires opening the prefab's
// own scene; changes propagate to instances on reload). The instance root
// node itself stays editable - moving / renaming / deleting the instance as
// a whole is a normal scene edit. lock_viewport_selection additionally
// keeps box-select and other direct-pick paths off the interior; click
// selection resolves to the instance root (get_outermost_prefab_instance_node).
void seal_instance_subtree(const std::shared_ptr<erhe::scene::Node>& node)
{
    constexpr uint64_t seal_flags =
        erhe::Item_flags::lock_edit               |
        erhe::Item_flags::lock_viewport_selection |
        erhe::Item_flags::lock_viewport_transform;
    node->enable_flag_bits(seal_flags);
    for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
        attachment->enable_flag_bits(seal_flags);
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node) {
            seal_instance_subtree(child_node);
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
        record_reference(canonical_path);
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

    std::shared_ptr<Prefab> prefab = std::make_shared<Prefab>();
    prefab->source_path = canonical_path;
    prefab->name        = erhe::file::to_string(canonical_path.filename());
    if (!load_template(*prefab)) {
        log_parsers->error("Prefab '{}' produced no nodes - not caching", erhe::file::to_string(canonical_path));
        return {};
    }

    m_prefabs.emplace(canonical_path, prefab);
    record_reference(canonical_path);
    log_parsers->info("Prefab loaded: {}", erhe::file::to_string(canonical_path));
    return prefab;
}

auto Prefab_library::load_template(Prefab& prefab) -> bool
{
    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY(m_context.executor != nullptr);
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);

    // A fresh holding scene / template root every time: reload discards the
    // previous template wholesale (existing instance clones keep their own
    // copies alive until they are refreshed).
    prefab.holding_scene = std::make_shared<erhe::scene::Scene>(fmt::format("prefab holding scene: {}", prefab.name), nullptr);
    prefab.template_root = std::make_shared<erhe::scene::Node>(prefab.name);
    prefab.template_root->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    prefab.template_root->set_parent(prefab.holding_scene->get_root_node());

    m_active_load_stack.push_back(prefab.source_path);

    erhe::gltf::Image_transfer image_transfer{*m_context.graphics_device, *m_context.current_command_buffer};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = *m_context.graphics_device,
        .executor        = *m_context.executor,
        .image_transfer  = image_transfer,
        .root_node       = prefab.template_root,
        .mesh_layer_id   = 0, // instances are retargeted to the destination scene's content layer
        .path            = prefab.source_path,
    };
    prefab.gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    const bool has_nodes = std::any_of(
        prefab.gltf_data.nodes.begin(),
        prefab.gltf_data.nodes.end(),
        [](const std::shared_ptr<erhe::scene::Node>& node) { return static_cast<bool>(node); }
    );
    if (!has_nodes) {
        m_active_load_stack.pop_back();
        return false;
    }

    finalize_imported_meshes(m_context, make_import_build_info(m_context), prefab.gltf_data, nullptr);

    // glTF 2.1: resolve external assets inside the template, so instance
    // clones reproduce nested content. This runs while this path is still
    // on the active load stack, so reference cycles through the recursive
    // get_or_load are detected.
    resolve_external_assets(*this, prefab.gltf_data, 0, nullptr);

    m_active_load_stack.pop_back();

    if (!prefab.gltf_data.skins.empty() || !prefab.gltf_data.animations.empty()) {
        log_parsers->warn(
            "Prefab '{}' contains skins/animations; instances are static for now (joint/animation target remapping is not implemented)",
            prefab.name
        );
    }
    return true;
}

void Prefab_library::record_reference(const std::filesystem::path& referenced_path)
{
    if (m_active_load_stack.empty()) {
        return; // top-level load (scene import / instantiate), not a prefab template
    }
    const std::filesystem::path& referencing_path = m_active_load_stack.back();
    if (referencing_path == referenced_path) {
        return;
    }
    m_references[referencing_path].insert(referenced_path);
}

auto Prefab_library::collect_affected_in_dependency_order(const std::filesystem::path& path) const -> std::vector<std::filesystem::path>
{
    // Transitive closure over reverse references.
    std::set<std::filesystem::path> affected;
    affected.insert(path);
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& [referencing_path, referenced_paths] : m_references) {
            if (affected.contains(referencing_path)) {
                continue;
            }
            for (const std::filesystem::path& referenced_path : referenced_paths) {
                if (affected.contains(referenced_path)) {
                    affected.insert(referencing_path);
                    grew = true;
                    break;
                }
            }
        }
    }

    // Topological order: referenced before referencing, so rebuilding a
    // template always clones already-rebuilt nested templates.
    std::vector<std::filesystem::path> order;
    std::set<std::filesystem::path>    placed;
    while (placed.size() < affected.size()) {
        bool progressed = false;
        for (const std::filesystem::path& candidate : affected) {
            if (placed.contains(candidate)) {
                continue;
            }
            bool ready = true;
            const auto references = m_references.find(candidate);
            if (references != m_references.end()) {
                for (const std::filesystem::path& referenced_path : references->second) {
                    if (affected.contains(referenced_path) && !placed.contains(referenced_path)) {
                        ready = false;
                        break;
                    }
                }
            }
            if (ready) {
                order.push_back(candidate);
                placed.insert(candidate);
                progressed = true;
            }
        }
        if (!progressed) {
            // Cannot happen with an acyclic reference graph (glTF 2.1
            // prohibits cycles and loading rejects them); guard against an
            // infinite loop anyway and surface the inconsistency.
            log_parsers->error("Prefab reference graph inconsistency: cycle among loaded prefabs");
            for (const std::filesystem::path& candidate : affected) {
                if (!placed.contains(candidate)) {
                    order.push_back(candidate);
                    placed.insert(candidate);
                }
            }
        }
    }
    return order;
}

auto Prefab_library::reload(const std::filesystem::path& path) -> bool
{
    std::error_code error_code;
    std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
    if (error_code) {
        canonical_path = path;
    }

    if (!m_prefabs.contains(canonical_path)) {
        log_parsers->error("Prefab reload: '{}' is not a loaded prefab", erhe::file::to_string(canonical_path));
        return false;
    }
    ERHE_VERIFY(m_active_load_stack.empty()); // reload is a top-level operation, never re-entered from a load

    const std::vector<std::filesystem::path> affected = collect_affected_in_dependency_order(canonical_path);

    bool all_ok = true;
    std::vector<std::filesystem::path> rebuilt_paths;
    for (const std::filesystem::path& affected_path : affected) {
        const auto it = m_prefabs.find(affected_path);
        if (it == m_prefabs.end()) {
            continue; // reference recorded for a load that later failed; nothing to rebuild
        }
        const bool exists = std::filesystem::exists(affected_path, error_code);
        if (!exists || error_code) {
            log_parsers->error("Prefab reload: source file missing: {}", erhe::file::to_string(affected_path));
            all_ok = false;
            continue;
        }
        // Forward references are re-recorded by the nested get_or_load calls
        // during the template rebuild.
        m_references.erase(affected_path);
        if (!load_template(*it->second)) {
            log_parsers->error("Prefab reload: '{}' produced no nodes; its instances keep the previous content", erhe::file::to_string(affected_path));
            all_ok = false;
            continue;
        }
        rebuilt_paths.push_back(affected_path);
        log_parsers->info("Prefab reloaded: {}", erhe::file::to_string(affected_path));
    }

    refresh_instances(rebuilt_paths);
    return all_ok;
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
        if ((child_node->get_flag_bits() & erhe::Item_flags::exclude_from_prefab) != 0) {
            continue; // editor-generated helper, never part of prefab content
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
        seal_instance_subtree(clone_node);
    }
}

namespace {

// Remove content-library entries recorded from a previous load of the given
// glTF source; a reload produces new texture / material objects, so the old
// entries would otherwise linger (and keep the previous GPU textures alive).
template <typename T>
void remove_gltf_source_entries(const std::shared_ptr<Content_library_node>& folder, const std::string& gltf_path)
{
    std::vector<std::shared_ptr<T>> stale_entries;
    for (const std::shared_ptr<erhe::Hierarchy>& child : folder->get_children()) {
        const std::shared_ptr<Content_library_node> entry = std::dynamic_pointer_cast<Content_library_node>(child);
        if (!entry || !entry->gltf_source.has_value() || (entry->gltf_source->gltf_path != gltf_path)) {
            continue;
        }
        const std::shared_ptr<T> item = std::dynamic_pointer_cast<T>(entry->item);
        if (item) {
            stale_entries.push_back(item);
        }
    }
    for (const std::shared_ptr<T>& item : stale_entries) {
        folder->remove(item);
    }
}

void replace_content_library_entries(Content_library& content_library, const Prefab& prefab)
{
    const std::string gltf_path_str = prefab.source_path.generic_string();
    remove_gltf_source_entries<erhe::graphics::Texture  >(content_library.textures,  gltf_path_str);
    remove_gltf_source_entries<erhe::primitive::Material>(content_library.materials, gltf_path_str);
    for (std::size_t i = 0; i < prefab.gltf_data.images.size(); ++i) {
        const std::shared_ptr<erhe::graphics::Texture>& image = prefab.gltf_data.images[i];
        if (image) {
            content_library.textures->add(
                image,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = image->get_name(),
                    .item_index = static_cast<int>(i),
                    .item_type  = "texture",
                }
            );
        }
    }
    for (std::size_t i = 0; i < prefab.gltf_data.materials.size(); ++i) {
        const std::shared_ptr<erhe::primitive::Material>& material = prefab.gltf_data.materials[i];
        if (material) {
            content_library.materials->add(
                material,
                Gltf_source_reference{
                    .gltf_path  = gltf_path_str,
                    .item_name  = material->get_name(),
                    .item_index = static_cast<int>(i),
                    .item_type  = "material",
                }
            );
        }
    }
}

void refresh_instance_subtrees(
    const std::shared_ptr<erhe::scene::Node>&                       node,
    const std::map<std::filesystem::path, std::shared_ptr<Prefab>>& prefabs,
    const std::set<std::filesystem::path>&                          rebuilt_paths,
    const erhe::scene::Layer_id                                     content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>&                  mesh_node_items,
    std::set<std::filesystem::path>&                                refreshed_paths
)
{
    const std::shared_ptr<Prefab_instance> prefab_instance = erhe::scene::get_attachment<Prefab_instance>(node.get());
    if (prefab_instance) {
        const std::filesystem::path& source_path = prefab_instance->get_prefab_source_path();
        if (rebuilt_paths.contains(source_path)) {
            const auto it = prefabs.find(source_path);
            if (it != prefabs.end()) {
                // Everything under an instance carrier is prefab content
                // (the same model save and export already use: instance
                // subtrees are never persisted), so drop all children and
                // re-clone from the rebuilt template. The carrier node's
                // own transform / name / flags are untouched.
                node->detach(prefab_instance.get());
                while (!node->get_children().empty()) {
                    const std::shared_ptr<erhe::Hierarchy> child = node->get_children().back();
                    child->set_parent(std::shared_ptr<erhe::Hierarchy>{});
                }
                attach_prefab_instance(it->second, node, content_layer_id, &mesh_node_items);
                refreshed_paths.insert(source_path);
            }
        }
        // Instance interiors are sealed; a nested instance is refreshed via
        // its outer template (a nested rebuild always marks every referencing
        // prefab affected too), so never descend past an instance carrier.
        return;
    }
    for (const std::shared_ptr<erhe::Hierarchy>& child : node->get_children()) {
        const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
        if (child_node) {
            refresh_instance_subtrees(child_node, prefabs, rebuilt_paths, content_layer_id, mesh_node_items, refreshed_paths);
        }
    }
}

} // namespace

void Prefab_library::refresh_instances(const std::vector<std::filesystem::path>& rebuilt_paths)
{
    if (rebuilt_paths.empty() || (m_context.app_scenes == nullptr)) {
        return;
    }
    const std::set<std::filesystem::path> rebuilt{rebuilt_paths.begin(), rebuilt_paths.end()};

    for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
        erhe::scene::Scene* scene = scene_root->get_hosted_scene();
        if (scene == nullptr) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> root_node = scene->get_root_node();
        if (!root_node) {
            continue;
        }

        std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
        std::set<std::filesystem::path>               refreshed_paths;
        {
            erhe::Item_host_lock_guard scene_lock{root_node.get()};
            refresh_instance_subtrees(root_node, m_prefabs, rebuilt, scene_root->layers().content()->id, mesh_node_items, refreshed_paths);
        }
        if (refreshed_paths.empty()) {
            continue;
        }
        log_parsers->info("Prefab reload: refreshed {} prefab(s) in scene '{}'", refreshed_paths.size(), scene_root->get_name());

        const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
        if (content_library) {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
            for (const std::filesystem::path& refreshed_path : refreshed_paths) {
                replace_content_library_entries(*content_library, *m_prefabs.at(refreshed_path));
            }
        }

        // Build raytrace primitives for the fresh clones (outside the scene
        // lock: the kickoff's async tasks take it themselves).
        Async_raytrace_kickoff_operation kickoff{scene_root, std::move(mesh_node_items)};
        kickoff.execute(m_context);
    }
}

auto save_prefab_scene(App_context& context, Scene_root& scene_root) -> bool
{
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if (source_path.empty()) {
        log_parsers->error("save_prefab: scene '{}' was not opened from a glTF file", scene_root.get_name());
        return false;
    }
    erhe::scene::Scene* scene = scene_root.get_hosted_scene();
    const std::shared_ptr<erhe::scene::Node> root_node = (scene != nullptr) ? scene->get_root_node() : std::shared_ptr<erhe::scene::Node>{};
    if (!root_node) {
        log_parsers->error("save_prefab: scene '{}' has no root node", scene_root.get_name());
        return false;
    }

    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(*scene);
    const bool binary = source_path.extension() == std::filesystem::path{".glb"};
    // exclude_from_prefab items (the editor-added default camera / lights)
    // ARE written to the file - the flag rides in node extras and round-trips
    // through parse_gltf, so the prefab scene's editing setup survives
    // reopening; attach_prefab_instance filters them out of instances.
    const std::string gltf = erhe::gltf::export_gltf(
        erhe::gltf::Gltf_export_arguments{
            .root_node       = *root_node,
            .binary          = binary,
            .physics_data    = &physics_data,
            .external_assets = collect_prefab_external_assets(*root_node, source_path.parent_path())
        }
    );
    if (!erhe::file::write_file(source_path, gltf)) {
        log_parsers->error("save_prefab: failed to write {}", erhe::file::to_string(source_path));
        return false;
    }
    log_parsers->info("save_prefab: scene '{}' written to {}", scene_root.get_name(), erhe::file::to_string(source_path));

    if ((context.prefab_library != nullptr) && context.prefab_library->get_prefabs().contains(source_path)) {
        context.prefab_library->reload(source_path);
    }
    return true;
}

}
