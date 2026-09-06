#include "prefabs/prefab_library.hpp"

#include "assets/asset_load_task.hpp"
#include "assets/asset_manager.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/library_attach_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "parsers/usd.hpp"
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
#include "erhe_scene/xform.hpp"
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
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node.get());
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

auto Prefab_library::get_prefabs() const -> const std::map<Prefab_key, std::shared_ptr<Prefab>>&
{
    return m_prefabs;
}

namespace {

[[nodiscard]] auto canonical_prefab_path(const std::filesystem::path& path) -> std::filesystem::path
{
    std::error_code error_code;
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error_code);
    return error_code ? path : canonical_path;
}

// What a Prefab_key names, for logs: the file, plus the prim inside it when
// the template is one prim of a USD file.
[[nodiscard]] auto to_string(const Prefab_key& key) -> std::string
{
    return key.prim_path.empty()
        ? erhe::file::to_string(key.source_path)
        : fmt::format("{}{}", erhe::file::to_string(key.source_path), key.prim_path);
}

// The display name of a template: the file name, and the prim path with it
// when the template is one prim of a USD file.
[[nodiscard]] auto make_prefab_name(const Prefab_key& key) -> std::string
{
    return key.prim_path.empty()
        ? erhe::file::to_string(key.source_path.filename())
        : fmt::format("{}{}", erhe::file::to_string(key.source_path.filename()), key.prim_path);
}

} // namespace

auto Prefab_library::get_or_load(const std::filesystem::path& path, const std::string& prim_path) -> std::shared_ptr<Prefab>
{
    const Prefab_key key{canonical_prefab_path(path), prim_path};

    const auto existing = m_prefabs.find(key);
    if (existing != m_prefabs.end()) {
        record_reference(key);
        return existing->second;
    }

    // glTF 2.1 strictly prohibits cyclical references between assets, and a
    // USD reference cycle is prohibited the same way; a cycle here would
    // otherwise recurse forever through reference resolution.
    const auto cycle = std::find(m_active_load_stack.begin(), m_active_load_stack.end(), key);
    if (cycle != m_active_load_stack.end()) {
        std::string cycle_description;
        for (auto i = cycle; i != m_active_load_stack.end(); ++i) {
            cycle_description += to_string(*i);
            cycle_description += " -> ";
        }
        cycle_description += to_string(key);
        log_parsers->error("Prefab reference cycle detected: {}", cycle_description);
        return {};
    }

    std::error_code error_code;
    const bool exists = std::filesystem::exists(key.source_path, error_code);
    if (!exists || error_code) {
        log_parsers->error("Prefab source file not found: {}", erhe::file::to_string(key.source_path));
        return {};
    }

    std::shared_ptr<Prefab> prefab = std::make_shared<Prefab>();
    prefab->source_path = key.source_path;
    prefab->prim_path   = key.prim_path;
    prefab->name        = make_prefab_name(key);
    if (!load_template(*prefab)) {
        log_parsers->error("Prefab '{}' produced no nodes - not caching", to_string(key));
        return {};
    }

    m_prefabs.emplace(key, prefab);
    record_reference(key);
    log_parsers->info("Prefab loaded: {}", to_string(key));
    return prefab;
}

void Prefab_library::get_or_load_async(
    const std::filesystem::path&                       path,
    std::function<void(const std::shared_ptr<Prefab>&)> on_ready
)
{
    get_or_load_async(path, std::string{}, std::move(on_ready));
}

void Prefab_library::get_or_load_async(
    const std::filesystem::path&                       path,
    const std::string&                                 prim_path,
    std::function<void(const std::shared_ptr<Prefab>&)> on_ready
)
{
    const std::filesystem::path canonical_path = canonical_prefab_path(path);
    const Prefab_key            key{canonical_path, prim_path};

    // Already loaded: no task, no frame of latency.
    const auto existing = m_prefabs.find(key);
    if (existing != m_prefabs.end()) {
        record_reference(key);
        on_ready(existing->second);
        return;
    }

    // Cycle detection still works on the call stack here: a nested prefab
    // load runs synchronously inside finish_load_template, so anything that
    // reaches this while a template is being finished is on the stack.
    const auto cycle = std::find(m_active_load_stack.begin(), m_active_load_stack.end(), key);
    if (cycle != m_active_load_stack.end()) {
        std::string cycle_description;
        for (auto i = cycle; i != m_active_load_stack.end(); ++i) {
            cycle_description += to_string(*i);
            cycle_description += " -> ";
        }
        cycle_description += to_string(key);
        log_parsers->error("Prefab reference cycle detected: {}", cycle_description);
        on_ready({});
        return;
    }

    std::error_code error_code;
    const bool exists = std::filesystem::exists(canonical_path, error_code);
    if (!exists || error_code) {
        log_parsers->error("Prefab source file not found: {}", erhe::file::to_string(canonical_path));
        on_ready({});
        return;
    }

    // Only the glTF parse has an asynchronous path; a USD template is loaded
    // inline (doc/usd-compatibility-plan.md X1).
    if ((m_context.asset_manager == nullptr) || is_usd_file_extension(canonical_path)) {
        on_ready(get_or_load(canonical_path, prim_path));
        return;
    }

    const std::string prefab_name = make_prefab_name(key);
    Asset_load_request request{
        .path            = canonical_path,
        .prefab_template = true,
        .root_node_name  = prefab_name
    };
    std::shared_ptr<Asset_load_handle> handle = m_context.asset_manager->queue_load(
        std::move(request),
        [this, key, canonical_path, prefab_name, on_ready](const Asset_load_result& result) {
            // A second request for the same path may have completed while
            // this one was in flight (both were queued before either
            // finished): keep the cached one, drop this parse.
            const auto already = m_prefabs.find(key);
            if (already != m_prefabs.end()) {
                record_reference(key);
                on_ready(already->second);
                return;
            }
            if (!result.prepared_parse) {
                on_ready({}); // failed or cancelled
                return;
            }
            std::shared_ptr<Prefab> prefab = std::make_shared<Prefab>();
            prefab->source_path = canonical_path;
            prefab->name        = prefab_name;
            const bool ok = finish_load_template(
                *prefab,
                std::move(result.prepared_parse->gltf_data),
                result.prepared_parse->root_node
            );
            if (!ok) {
                log_parsers->error("Prefab '{}' produced no nodes - not caching", erhe::file::to_string(canonical_path));
                on_ready({});
                return;
            }
            m_prefabs.emplace(key, prefab);
            record_reference(key);
            log_parsers->info("Prefab loaded (async): {}", erhe::file::to_string(canonical_path));
            on_ready(prefab);
        }
    );
    if (!handle) {
        on_ready(get_or_load(canonical_path, prim_path)); // async_gltf_load is off
    }
}

auto Prefab_library::load_template(Prefab& prefab) -> bool
{
    if (is_usd_file_extension(prefab.source_path)) {
        return load_usd_template(prefab);
    }

    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY(m_context.executor != nullptr);
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);

    auto template_root = std::make_shared<erhe::scene::Xform>(prefab.name);
    template_root->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);

    erhe::gltf::Image_transfer image_transfer{*m_context.graphics_device};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .executor        = *m_context.executor,
        .device_options  = erhe::gltf::query_gltf_device_options(*m_context.graphics_device),
        .root_node       = template_root,
        .mesh_layer_id   = 0, // instances are retargeted to the destination scene's content layer
        .path            = prefab.source_path,
        .fix_spot_lights = m_context.fix_gltf_spot_lights,
    };
    erhe::gltf::Gltf_data gltf_data = erhe::gltf::parse_gltf(parse_arguments);
    // parse_gltf creates no GPU objects (async-asset-loading plan step 3);
    // this is the synchronous path, so drain residency in full.
    gltf_data.image_residency.drain(gltf_data, *m_context.graphics_device, image_transfer);

    return finish_load_template(prefab, std::move(gltf_data), template_root);
}

auto Prefab_library::load_usd_template(Prefab& prefab) -> bool
{
    ERHE_VERIFY(m_context.graphics_device != nullptr);

    // On the stack while the template's own arcs are resolved, so a USD
    // reference cycle is caught exactly as a glTF one is.
    m_active_load_stack.push_back(prefab.get_key());
    Usd_prefab_template usd_template = load_usd_prefab_template(m_context, *this, prefab.source_path, prefab.prim_path);
    m_active_load_stack.pop_back();

    if (!usd_template.error.empty()) {
        log_parsers->error("Prefab '{}': {}", prefab.name, usd_template.error);
        return false;
    }
    if (!usd_template.root || usd_template.root->get_children().empty()) {
        return false;
    }

    // A fresh holding scene every time, as the glTF path does.
    prefab.holding_scene = std::make_shared<erhe::scene::Scene>(fmt::format("prefab holding scene: {}", prefab.name), nullptr);
    prefab.template_root = usd_template.root;
    prefab.template_root->set_parent(prefab.holding_scene->get_root_node());
    prefab.gltf_data     = erhe::gltf::Gltf_data{};
    prefab.materials     = std::move(usd_template.materials);
    return true;
}

auto Prefab_library::finish_load_template(
    Prefab&                                   prefab,
    erhe::gltf::Gltf_data&&                   gltf_data,
    const std::shared_ptr<erhe::scene::Node>& template_root
) -> bool
{
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);

    // A fresh holding scene / template root every time: reload discards the
    // previous template wholesale (existing instance clones keep their own
    // copies alive until they are refreshed). The parse produced an UNHOSTED
    // template_root (that is what makes an asynchronous parse safe); hosting
    // it here reproduces the ordering the synchronous path always had -
    // everything below runs with the template hosted.
    prefab.holding_scene = std::make_shared<erhe::scene::Scene>(fmt::format("prefab holding scene: {}", prefab.name), nullptr);
    prefab.template_root = template_root;
    prefab.template_root->set_parent(prefab.holding_scene->get_root_node());
    prefab.gltf_data = std::move(gltf_data);
    prefab.materials = prefab.gltf_data.materials;

    m_active_load_stack.push_back(prefab.get_key());

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
    // Nested prefab templates: the holding scene has no content library
    // (templates host nothing), so no reference entries to add.
    resolve_external_assets(*this, prefab.gltf_data, 0, nullptr, nullptr);

    m_active_load_stack.pop_back();

    if (!prefab.gltf_data.skins.empty() || !prefab.gltf_data.animations.empty()) {
        log_parsers->warn(
            "Prefab '{}' contains skins/animations; instances are static for now (joint/animation target remapping is not implemented)",
            prefab.name
        );
    }
    return true;
}

void Prefab_library::record_reference(const Prefab_key& referenced_key)
{
    if (m_active_load_stack.empty()) {
        return; // top-level load (scene import / instantiate), not a prefab template
    }
    const Prefab_key& referencing_key = m_active_load_stack.back();
    if (referencing_key == referenced_key) {
        return;
    }
    m_references[referencing_key].insert(referenced_key);
}

auto Prefab_library::collect_affected_in_dependency_order(const std::vector<Prefab_key>& seeds) const -> std::vector<Prefab_key>
{
    // Transitive closure over reverse references.
    std::set<Prefab_key> affected;
    affected.insert(seeds.begin(), seeds.end());
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& [referencing_path, referenced_paths] : m_references) {
            if (affected.contains(referencing_path)) {
                continue;
            }
            for (const Prefab_key& referenced_key : referenced_paths) {
                if (affected.contains(referenced_key)) {
                    affected.insert(referencing_path);
                    grew = true;
                    break;
                }
            }
        }
    }

    // Topological order: referenced before referencing, so rebuilding a
    // template always clones already-rebuilt nested templates.
    std::vector<Prefab_key> order;
    std::set<Prefab_key>    placed;
    while (placed.size() < affected.size()) {
        bool progressed = false;
        for (const Prefab_key& candidate : affected) {
            if (placed.contains(candidate)) {
                continue;
            }
            bool ready = true;
            const auto references = m_references.find(candidate);
            if (references != m_references.end()) {
                for (const Prefab_key& referenced_key : references->second) {
                    if (affected.contains(referenced_key) && !placed.contains(referenced_key)) {
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
            for (const Prefab_key& candidate : affected) {
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
    const std::filesystem::path canonical_path = canonical_prefab_path(path);

    // A file can hold several templates (one per referenced USD prim); a
    // reload of the file rebuilds all of them.
    std::vector<Prefab_key> seeds;
    for (const std::pair<const Prefab_key, std::shared_ptr<Prefab>>& entry : m_prefabs) {
        if (entry.first.source_path == canonical_path) {
            seeds.push_back(entry.first);
        }
    }
    if (seeds.empty()) {
        log_parsers->error("Prefab reload: '{}' is not a loaded prefab", erhe::file::to_string(canonical_path));
        return false;
    }
    ERHE_VERIFY(m_active_load_stack.empty()); // reload is a top-level operation, never re-entered from a load

    const std::vector<Prefab_key> affected = collect_affected_in_dependency_order(seeds);

    std::error_code         error_code;
    bool                    all_ok = true;
    std::vector<Prefab_key> rebuilt_keys;
    for (const Prefab_key& affected_key : affected) {
        const auto it = m_prefabs.find(affected_key);
        if (it == m_prefabs.end()) {
            continue; // reference recorded for a load that later failed; nothing to rebuild
        }
        const bool exists = std::filesystem::exists(affected_key.source_path, error_code);
        if (!exists || error_code) {
            log_parsers->error("Prefab reload: source file missing: {}", erhe::file::to_string(affected_key.source_path));
            all_ok = false;
            continue;
        }
        // Forward references are re-recorded by the nested get_or_load calls
        // during the template rebuild.
        m_references.erase(affected_key);
        if (!load_template(*it->second)) {
            log_parsers->error("Prefab reload: '{}' produced no nodes; its instances keep the previous content", to_string(affected_key));
            all_ok = false;
            continue;
        }
        rebuilt_keys.push_back(affected_key);
        log_parsers->info("Prefab reloaded: {}", to_string(affected_key));
    }

    refresh_instances(rebuilt_keys);
    return all_ok;
}

auto instantiate_prefab(
    App_context&                              context,
    const std::shared_ptr<Prefab>&            prefab,
    Scene_root&                               scene_root,
    const glm::mat4&                          world_from_node,
    const std::shared_ptr<erhe::scene::Node>& parent,
    const std::size_t                         index_in_parent
) -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_VERIFY(prefab);

    constexpr uint64_t node_flags =
        erhe::Item_flags::content |
        erhe::Item_flags::expand  |
        erhe::Item_flags::show_in_ui;

    std::shared_ptr<erhe::scene::Node> instance_root = std::make_shared<erhe::scene::Xform>(prefab->name);
    instance_root->enable_flag_bits(node_flags);
    instance_root->set_world_from_node(world_from_node);

    // Clone (including nested prefab content), retarget to the destination
    // scene's content layer, and collect the mesh nodes for the raytrace
    // kickoff.
    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items;
    attach_prefab_instance(prefab, instance_root, scene_root.layers().content()->id, &mesh_node_items);

    // Register the prefab's shared resources in the destination scene's
    // content library (idempotent per resource; mirrors import_gltf). These
    // are REFERENCE entries: the prefab template owns the texture / material
    // objects and shares them with every instancing scene (GPU textures
    // cannot be duplicated per scene), so the entries never claim the item's
    // Item_host - unlike scene-owned imports.
    std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    const std::string gltf_path_str = prefab->source_path.generic_string();

    // The template's textures and materials belong to the template's own
    // tree: the instance's meshes bind them, and the scene's Material_set
    // gives them slots through its per-object membership
    // (Scene_root::enqueue_mesh_materials), so the instancing scene lists
    // nothing (doc/usd-compatibility-plan.md U4).
    std::vector<std::shared_ptr<Operation>> operations;

    std::shared_ptr<erhe::scene::Node> insert_parent = parent;
    if (!insert_parent) {
        insert_parent = scene_root.get_hosted_scene()->get_root_node();
    }

    operations.push_back(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context         = context,
                .item            = instance_root,
                .parent          = insert_parent,
                .mode            = Item_insert_remove_operation::Mode::insert,
                .index_in_parent = index_in_parent
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
    std::vector<std::shared_ptr<erhe::Item_base>>* out_mesh_node_items,
    Content_library*                               content_library
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
        // The template owns its textures and materials: the instancing scene
        // lists nothing (doc/usd-compatibility-plan.md U4). register_mesh
        // adopts a material only when this scene's container record defines
        // it (Scene_root::is_asset_definition), which a template material is
        // not, and the instance's meshes are what give it a material slot.
        static_cast<void>(content_library);
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
    std::shared_ptr<Prefab_instance> prefab_instance = std::make_shared<Prefab_instance>(prefab->source_path, prefab->name, prefab->prim_path);
    prefab_instance->enable_flag_bits(erhe::Item_flags::no_message | erhe::Item_flags::show_in_ui);
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
        // live scene, and parenting registers meshes into
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

void refresh_instance_subtrees(
    const std::shared_ptr<erhe::scene::Node>&            node,
    const std::map<Prefab_key, std::shared_ptr<Prefab>>& prefabs,
    const std::set<Prefab_key>&                          rebuilt_keys,
    const erhe::scene::Layer_id                          content_layer_id,
    std::vector<std::shared_ptr<erhe::Item_base>>&       mesh_node_items,
    std::set<Prefab_key>&                                refreshed_keys
)
{
    const std::shared_ptr<Prefab_instance> prefab_instance = erhe::scene::get_attachment<Prefab_instance>(node.get());
    if (prefab_instance) {
        const Prefab_key key{prefab_instance->get_prefab_source_path(), prefab_instance->get_prefab_prim_path()};
        if (rebuilt_keys.contains(key)) {
            const auto it = prefabs.find(key);
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
                refreshed_keys.insert(key);
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
            refresh_instance_subtrees(child_node, prefabs, rebuilt_keys, content_layer_id, mesh_node_items, refreshed_keys);
        }
    }
}

} // namespace

void Prefab_library::refresh_instances(const std::vector<Prefab_key>& rebuilt_keys)
{
    if (rebuilt_keys.empty() || (m_context.app_scenes == nullptr)) {
        return;
    }
    const std::set<Prefab_key> rebuilt{rebuilt_keys.begin(), rebuilt_keys.end()};

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
        std::set<Prefab_key>                          refreshed_keys;
        {
            erhe::Item_host_lock_guard scene_lock{root_node.get()};
            refresh_instance_subtrees(root_node, m_prefabs, rebuilt, scene_root->layers().content()->id, mesh_node_items, refreshed_keys);
        }
        if (refreshed_keys.empty()) {
            continue;
        }
        log_parsers->info("Prefab reload: refreshed {} prefab(s) in scene '{}'", refreshed_keys.size(), scene_root->get_name());

        // Build raytrace primitives for the fresh clones (outside the scene
        // lock: the kickoff's async tasks take it themselves).
        Async_raytrace_kickoff_operation kickoff{scene_root, std::move(mesh_node_items)};
        kickoff.execute(m_context);
    }
}

}
