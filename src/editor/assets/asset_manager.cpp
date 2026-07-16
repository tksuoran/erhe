#include "assets/asset_manager.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/asset_paths.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/image_transfer.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <thread>
#include <utility>

namespace editor {

auto Asset_container_record::get_type_entries(const Asset_type type) -> Type_entries*
{
    switch (type) {
        case Asset_type::material:  return &materials;
        case Asset_type::animation: return &animations;
        default:                    return nullptr;
    }
}

auto Asset_container_record::get_type_entries(const Asset_type type) const -> const Type_entries*
{
    switch (type) {
        case Asset_type::material:  return &materials;
        case Asset_type::animation: return &animations;
        default:                    return nullptr;
    }
}

namespace {

// Builds the per-type resolution maps for one managed asset type of a
// freshly parsed container, validating identifiability per plan decision
// 11: an asset must carry a uid or a non-empty name that is unique within
// (container, type); anything else is recorded as an error and cannot be
// acquired - no tie-breaking, no guessing. Note on "conforming": the glTF
// 2.1 identifier charset is a pre-ratification open question (recommended
// alphanumeric); uniqueness is enforced here, the charset rule is
// re-checked against the normative text at ratification.
template <typename T>
void build_type_entries(Asset_container_record& record, const Asset_type type, const std::vector<std::shared_ptr<T>>& items)
{
    Asset_container_record::Type_entries* entries = record.get_type_entries(type);
    ERHE_VERIFY(entries != nullptr);
    const char* type_name = c_str(type);

    for (const std::shared_ptr<T>& item : items) {
        if (!item) {
            continue;
        }
        const std::string& name = item->get_name();
        if (!name.empty()) {
            ++entries->name_counts[name];
        }
    }

    for (const std::shared_ptr<T>& item : items) {
        if (!item) {
            continue;
        }
        const std::string& uid  = item->get_gltf_uid();
        const std::string& name = item->get_name();
        const bool name_unique  = !name.empty() && (entries->name_counts[name] == 1);
        if (!uid.empty()) {
            const auto [existing, inserted] = entries->by_uid.try_emplace(uid, item);
            if (!inserted) {
                record.errors.push_back(
                    fmt::format("{} uid '{}' is declared more than once - only the first occurrence is addressable", type_name, uid)
                );
                continue;
            }
        } else if (!name_unique) {
            record.errors.push_back(
                name.empty()
                    ? fmt::format("a {} has no uid and no name and cannot be addressed (decision 11: stamp uids into the file or name it in the DCC tool)", type_name)
                    : fmt::format(
                        "{} name '{}' matches {} {}s and none of them can be addressed by name without a uid (decision 11: stamp uids into the file or rename in the DCC tool)",
                        type_name, name, entries->name_counts[name], type_name
                    )
            );
            continue;
        }
        if (name_unique) {
            entries->by_unique_name.emplace(name, item);
        }
        entries->items.push_back(item);
    }
}

}

Asset_manager::Asset_manager(App_context& context, App_message_bus& app_message_bus, App_scenes& app_scenes)
    : m_context        {context}
    , m_app_message_bus{app_message_bus}
    , m_app_scenes     {app_scenes}
{
    // m_app_message_bus is reserved for the R5 scene-open routing; read it
    // here so the field is not flagged unused until then.
    static_cast<void>(m_app_message_bus);
}

Asset_manager::~Asset_manager() noexcept
{
    // Debug holds unregister into m_users on destruction; drop them while
    // every registry map is still fully alive.
    m_debug_holds.clear();
}

void Asset_manager::verify_main_thread() const
{
    ERHE_VERIFY(std::this_thread::get_id() == m_context.main_thread_id);
}

auto Asset_manager::acquire(const Asset_key& key) -> std::shared_ptr<erhe::Item_base>
{
    std::string error;
    std::shared_ptr<erhe::Item_base> item = acquire(key, error);
    if (!item) {
        log_asset->warn("Asset_manager::acquire failed for {}: {}", key.describe(), error);
    }
    return item;
}

auto Asset_manager::acquire(const Asset_key& key, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    verify_main_thread();
    switch (key.scope) {
        case Asset_scope::builtin: {
            return resolve_builtin(key, out_error);
        }
        case Asset_scope::file: {
            if (key.path.empty()) {
                out_error = "file-scope key has no path";
                return {};
            }
            const std::shared_ptr<Asset_container_record> record = get_or_load_container(std::filesystem::path{key.path}, out_error);
            if (!record) {
                return {};
            }
            return resolve_in_container(*record, key, out_error);
        }
        case Asset_scope::scene_local: {
            return resolve_scene_local(key, out_error);
        }
        case Asset_scope::none:
        default: {
            out_error = "key has no scope";
            return {};
        }
    }
}

auto Asset_manager::resolve_builtin(const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>
{
    const auto type_it = m_builtins.find(key.type);
    if (type_it != m_builtins.end()) {
        const auto item_it = type_it->second.find(key.name);
        if (item_it != type_it->second.end()) {
            return item_it->second;
        }
    }
    out_error = fmt::format("no builtin {} named '{}' is registered", c_str(key.type), key.name);
    return {};
}

auto Asset_manager::resolve_in_container(const Asset_container_record& record, const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>
{
    const Asset_container_record::Type_entries* entries = record.get_type_entries(key.type);
    if (entries == nullptr) {
        out_error = fmt::format(
            "file-scope acquisition of type '{}' is not supported yet (containers serve materials and animations until the R5 single-loader flip)",
            c_str(key.type)
        );
        return {};
    }
    if (!key.uid.empty()) {
        const auto uid_it = entries->by_uid.find(key.uid);
        if (uid_it != entries->by_uid.end()) {
            return uid_it->second;
        }
        // Fall through to the name fallback: the uid may predate a re-authored
        // container. A name hit is logged so the drift is visible.
    }
    if (!key.name.empty()) {
        const auto name_it = entries->by_unique_name.find(key.name);
        if (name_it != entries->by_unique_name.end()) {
            if (!key.uid.empty()) {
                log_asset->warn(
                    "Asset container '{}': uid '{}' not found; {} resolved by name '{}' instead",
                    record.display_path, key.uid, c_str(key.type), key.name
                );
            }
            return name_it->second;
        }
        const auto count_it = entries->name_counts.find(key.name);
        if ((count_it != entries->name_counts.end()) && (count_it->second > 1)) {
            out_error = fmt::format(
                "{} name '{}' is ambiguous in container '{}' ({} matches); addressing requires a uid (decision 11)",
                c_str(key.type), key.name, record.display_path, count_it->second
            );
            return {};
        }
    }
    out_error = fmt::format(
        "no {} with{}{} in container '{}'",
        c_str(key.type),
        key.uid.empty()  ? "" : fmt::format(" uid '{}'", key.uid),
        key.name.empty() ? (key.uid.empty() ? " an empty key" : "") : fmt::format(" name '{}'", key.name),
        record.display_path
    );
    return {};
}

auto Asset_manager::resolve_scene_local(const Asset_key& key, std::string& out_error) const -> std::shared_ptr<erhe::Item_base>
{
    const Asset_type_info* info = get_asset_type_info(key.type);
    if (info == nullptr) {
        out_error = fmt::format("'{}' is not a managed asset type", c_str(key.type));
        return {};
    }
    if (key.name.empty()) {
        out_error = "scene_local key has no name";
        return {};
    }

    // Deterministic resolution order (plan risk 6): the manager registry
    // first (builtins, then loaded containers in path order), then open
    // scene libraries in registration order; the first match wins and
    // multi-matches are debug-logged.
    std::shared_ptr<erhe::Item_base> match;
    std::size_t                      match_count = 0;
    const auto consider = [&match, &match_count](const std::shared_ptr<erhe::Item_base>& candidate) {
        ++match_count;
        if (!match) {
            match = candidate;
        }
    };

    {
        const auto type_it = m_builtins.find(key.type);
        if (type_it != m_builtins.end()) {
            const auto item_it = type_it->second.find(key.name);
            if (item_it != type_it->second.end()) {
                consider(item_it->second);
            }
        }
    }
    for (const auto& [path, record] : m_containers) {
        const Asset_container_record::Type_entries* entries = record->get_type_entries(key.type);
        if (entries == nullptr) {
            continue;
        }
        const auto name_it = entries->by_unique_name.find(key.name);
        if (name_it != entries->by_unique_name.end()) {
            consider(name_it->second);
        }
    }
    for (const std::shared_ptr<Scene_root>& scene_root : m_app_scenes.get_scene_roots()) {
        if (key.type == Asset_type::mesh) {
            // Meshes are scene content, not library entries: match by the
            // mesh attachment's name over the scene's nodes.
            for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
                if (mesh && (mesh->get_name() == key.name)) {
                    consider(mesh);
                }
            }
            continue;
        }
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (!library || (info->library_folder == nullptr)) {
            continue;
        }
        const std::shared_ptr<Content_library_node>& folder = (*library).*(info->library_folder);
        if (!folder) {
            continue;
        }
        folder->for_each<Content_library_node>(
            [&key, &info, &consider](const Content_library_node& node) -> bool {
                if (node.item &&
                    ((node.item->get_type() & info->item_type_bit) == info->item_type_bit) &&
                    (node.item->get_name() == key.name))
                {
                    consider(node.item);
                }
                return true;
            }
        );
    }

    if (!match) {
        out_error = fmt::format("no {} named '{}' found in the registry or any open scene", c_str(key.type), key.name);
        return {};
    }
    if (match_count > 1) {
        log_asset->debug(
            "scene_local {} '{}' matched {} candidates; resolved to the first in deterministic order",
            c_str(key.type), key.name, match_count
        );
    }
    return match;
}

void Asset_manager::register_builtin(const Asset_type type, const std::shared_ptr<erhe::Item_base>& item)
{
    verify_main_thread();
    ERHE_VERIFY(item);
    const auto [existing, inserted] = m_builtins[type].try_emplace(item->get_name(), item);
    if (!inserted) {
        log_asset->warn("builtin {} '{}' is already registered - keeping the first registration", c_str(type), item->get_name());
        return;
    }
    log_asset->trace("registered builtin {} '{}'", c_str(type), item->get_name());
}

auto Asset_manager::find_loaded(const Asset_key& key) const -> std::shared_ptr<erhe::Item_base>
{
    std::string ignored_error;
    switch (key.scope) {
        case Asset_scope::builtin: {
            return resolve_builtin(key, ignored_error);
        }
        case Asset_scope::file: {
            const auto container_it = m_containers.find(normalize_asset_path(std::filesystem::path{key.path}));
            if (container_it == m_containers.end()) {
                return {};
            }
            return resolve_in_container(*container_it->second, key, ignored_error);
        }
        case Asset_scope::scene_local: {
            return resolve_scene_local(key, ignored_error);
        }
        case Asset_scope::none:
        default: {
            return {};
        }
    }
}

auto Asset_manager::make_key(const erhe::Item_base& item) const -> Asset_key
{
    Asset_key key;
    key.type = asset_type_from_item(item);
    key.uid  = item.get_gltf_uid();
    key.name = item.get_name();
    for (const auto& [type, by_name] : m_builtins) {
        for (const auto& [name, builtin_item] : by_name) {
            if (builtin_item.get() == &item) {
                key.scope = Asset_scope::builtin;
                return key;
            }
        }
    }
    for (const auto& [path, record] : m_containers) {
        const Asset_container_record::Type_entries* entries = record->get_type_entries(key.type);
        if (entries == nullptr) {
            continue;
        }
        for (const std::shared_ptr<erhe::Item_base>& container_item : entries->items) {
            if (container_item.get() == &item) {
                key.scope = Asset_scope::file;
                key.path  = record->display_path;
                return key;
            }
        }
    }
    key.scope = Asset_scope::scene_local;
    return key;
}

auto Asset_manager::get_users(const Asset_key& key) const -> std::vector<Asset_user_info>
{
    std::vector<Asset_user_info> result;
    const std::shared_ptr<erhe::Item_base> item = find_loaded(key);
    if (!item) {
        return result;
    }
    const auto user_it = m_users.find(item.get());
    if (user_it == m_users.end()) {
        return result;
    }
    for (const Asset_reference* reference : user_it->second) {
        result.push_back(Asset_user_info{reference->get_user_label(), item->get_name()});
    }
    return result;
}

auto Asset_manager::request_unload(const Asset_key& key) -> Unload_result
{
    verify_main_thread();
    Unload_result result;
    switch (key.scope) {
        case Asset_scope::builtin: {
            result.message = "builtin assets are not unloadable";
            return result;
        }
        case Asset_scope::scene_local: {
            result.message = "scene-local assets are owned by their scene, not the asset manager";
            return result;
        }
        case Asset_scope::file: {
            break;
        }
        case Asset_scope::none:
        default: {
            result.message = "key has no scope";
            return result;
        }
    }
    if (key.path.empty()) {
        result.message = "file-scope key has no path";
        return result;
    }
    const std::filesystem::path canonical_path = normalize_asset_path(std::filesystem::path{key.path});
    const auto container_it = m_containers.find(canonical_path);
    if (container_it == m_containers.end()) {
        result.message = fmt::format("container '{}' is not loaded", asset_path_to_string(canonical_path));
        return result;
    }
    std::shared_ptr<Asset_container_record> record = container_it->second;

    // Refuse while any asset of the container has users, naming them.
    std::vector<std::pair<std::weak_ptr<erhe::Item_base>, std::string>> weak_assets;
    for (const Asset_type_info& info : get_asset_type_infos()) {
        const Asset_container_record::Type_entries* entries = record->get_type_entries(info.type);
        if (entries == nullptr) {
            continue;
        }
        for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
            const auto user_it = m_users.find(item.get());
            if (user_it != m_users.end()) {
                for (const Asset_reference* reference : user_it->second) {
                    result.users.push_back(Asset_user_info{reference->get_user_label(), item->get_name()});
                }
            }
            weak_assets.emplace_back(item, fmt::format("{} '{}'", info.name, item->get_name()));
        }
    }
    if (!result.users.empty()) {
        std::string user_list;
        for (const Asset_user_info& user : result.users) {
            if (!user_list.empty()) {
                user_list += ", ";
            }
            user_list += fmt::format("{} ({} '{}')", user.label, "using", user.asset_name);
        }
        result.message = fmt::format("container '{}' is in use by: {}", record->display_path, user_list);
        return result;
    }

    const std::string display_path = record->display_path;
    m_containers.erase(container_it);
    record.reset(); // drop the last strong reference before verifying exclusivity

    // Exclusivity verification: a managed asset still lockable after the
    // container dropped means a raw shared_ptr bypassed Asset_reference -
    // an undeclared user (plan decision 2).
    for (const auto& [weak_item, description] : weak_assets) {
        if (weak_item.lock()) {
            ++result.undeclared_survivors;
            log_asset->warn(
                "undeclared asset user: {} of unloaded container '{}' is still alive (a raw shared_ptr bypassed Asset_reference)",
                description, display_path
            );
        }
    }
    result.ok = true;
    result.message = (result.undeclared_survivors == 0)
        ? fmt::format("container '{}' unloaded; all {} managed assets released", display_path, weak_assets.size())
        : fmt::format("container '{}' unloaded with {} undeclared surviving assets (see log)", display_path, result.undeclared_survivors);
    log_asset->info("{}", result.message);
    return result;
}

auto Asset_manager::get_or_load_container(const std::filesystem::path& path, std::string& out_error) -> std::shared_ptr<Asset_container_record>
{
    verify_main_thread();
    const std::filesystem::path canonical_path = normalize_asset_path(path);
    const auto existing = m_containers.find(canonical_path);
    if (existing != m_containers.end()) {
        return existing->second;
    }

    // Until the R5 single-loader flip routes scene open through the
    // manager, loading a file that is open as a scene would create a second
    // copy of every asset in it (the two-loader hazard, plan risk 2).
    for (const std::shared_ptr<Scene_root>& scene_root : m_app_scenes.get_scene_roots()) {
        const std::filesystem::path& scene_source_path = scene_root->get_source_path();
        if (scene_source_path.empty()) {
            continue;
        }
        if (normalize_asset_path(scene_source_path) == canonical_path) {
            out_error = fmt::format(
                "'{}' is currently open as scene '{}'; loading it as an asset container would duplicate its assets (scene open routes through the asset manager from phase R5 on)",
                asset_path_to_string(canonical_path), scene_root->get_name()
            );
            return {};
        }
    }

    std::error_code error_code;
    const bool exists = std::filesystem::exists(canonical_path, error_code);
    if (!exists || error_code) {
        out_error = fmt::format("asset container file not found: {}", asset_path_to_string(canonical_path));
        return {};
    }

    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY(m_context.executor != nullptr);
    ERHE_VERIFY(m_context.current_command_buffer != nullptr);

    std::shared_ptr<Asset_container_record> record = std::make_shared<Asset_container_record>();
    record->id             = m_next_container_id++;
    record->canonical_path = canonical_path;
    record->display_path   = asset_path_to_string(canonical_path);
    // Parsed nodes are parented under a free root node - no holding scene:
    // container node trees serve asset acquisition and are never rendered.
    record->root_node = std::make_shared<erhe::scene::Node>(fmt::format("asset container: {}", record->display_path));

    erhe::gltf::Image_transfer image_transfer{*m_context.graphics_device, *m_context.current_command_buffer};
    erhe::gltf::Gltf_parse_arguments parse_arguments{
        .graphics_device = *m_context.graphics_device,
        .executor        = *m_context.executor,
        .image_transfer  = image_transfer,
        .root_node       = record->root_node,
        .mesh_layer_id   = 0,
        .path            = canonical_path,
    };
    record->gltf_data = erhe::gltf::parse_gltf(parse_arguments);

    const bool has_content =
        !record->gltf_data.nodes.empty()     ||
        !record->gltf_data.materials.empty() ||
        !record->gltf_data.animations.empty();
    if (!has_content) {
        out_error = fmt::format("asset container '{}' produced no content - not cached", record->display_path);
        return {};
    }

    build_type_entries(*record, Asset_type::material,  record->gltf_data.materials);
    build_type_entries(*record, Asset_type::animation, record->gltf_data.animations);
    for (const std::string& error : record->errors) {
        log_asset->warn("asset container '{}': {}", record->display_path, error);
    }
    log_asset->info(
        "asset container loaded: '{}' ({} materials, {} animations, {} identifiability errors)",
        record->display_path, record->materials.items.size(), record->animations.items.size(), record->errors.size()
    );
    m_containers.emplace(canonical_path, record);
    return record;
}

auto Asset_manager::inspect_assets() const -> std::vector<Asset_info>
{
    std::vector<Asset_info> result;
    std::unordered_map<const erhe::Item_base*, bool> covered;
    const auto user_labels_of = [this](const erhe::Item_base* item) {
        std::vector<std::string> labels;
        const auto user_it = m_users.find(item);
        if (user_it != m_users.end()) {
            for (const Asset_reference* reference : user_it->second) {
                labels.push_back(reference->get_user_label());
            }
        }
        return labels;
    };
    for (const auto& [type, by_name] : m_builtins) {
        for (const auto& [name, item] : by_name) {
            Asset_info info;
            info.key.scope   = Asset_scope::builtin;
            info.key.type    = type;
            info.key.uid     = item->get_gltf_uid();
            info.key.name    = name;
            info.user_labels = user_labels_of(item.get());
            covered.emplace(item.get(), true);
            result.push_back(std::move(info));
        }
    }
    for (const auto& [path, record] : m_containers) {
        for (const Asset_type_info& type_info : get_asset_type_infos()) {
            const Asset_container_record::Type_entries* entries = record->get_type_entries(type_info.type);
            if (entries == nullptr) {
                continue;
            }
            for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
                Asset_info info;
                info.key.scope   = Asset_scope::file;
                info.key.type    = type_info.type;
                info.key.path    = record->display_path;
                info.key.uid     = item->get_gltf_uid();
                info.key.name    = item->get_name();
                info.user_labels = user_labels_of(item.get());
                covered.emplace(item.get(), true);
                result.push_back(std::move(info));
            }
        }
    }
    // Declared userships of items the manager does not own (scene_local
    // resolutions): visible so where-used stays truthful.
    for (const auto& [item, references] : m_users) {
        if (covered.contains(item) || references.empty()) {
            continue;
        }
        Asset_info info;
        info.key = make_key(*item);
        for (const Asset_reference* reference : references) {
            info.user_labels.push_back(reference->get_user_label());
        }
        result.push_back(std::move(info));
    }
    return result;
}

auto Asset_manager::inspect_containers() const -> std::vector<Asset_container_info>
{
    std::vector<Asset_container_info> result;
    for (const auto& [path, record] : m_containers) {
        Asset_container_info info;
        info.id              = record->id;
        info.path            = record->display_path;
        info.material_count  = record->materials.items.size();
        info.animation_count = record->animations.items.size();
        info.errors          = record->errors;
        info.dirty           = record->dirty;
        result.push_back(std::move(info));
    }
    return result;
}

auto Asset_manager::is_pinned(const erhe::Item_base* item) const -> bool
{
    if (item == nullptr) {
        return false;
    }
    const auto user_it = m_users.find(item);
    if ((user_it != m_users.end()) && !user_it->second.empty()) {
        return true;
    }
    for (const auto& [type, by_name] : m_builtins) {
        for (const auto& [name, builtin_item] : by_name) {
            if (builtin_item.get() == item) {
                return true;
            }
        }
    }
    for (const auto& [path, record] : m_containers) {
        for (const Asset_type_info& type_info : get_asset_type_infos()) {
            const Asset_container_record::Type_entries* entries = record->get_type_entries(type_info.type);
            if (entries == nullptr) {
                continue;
            }
            for (const std::shared_ptr<erhe::Item_base>& container_item : entries->items) {
                if (container_item.get() == item) {
                    return true;
                }
            }
        }
    }
    return false;
}

auto Asset_manager::debug_acquire(const std::string_view hold_name, const Asset_key& key, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    verify_main_thread();
    if (hold_name.empty()) {
        out_error = "hold_name must not be empty";
        return {};
    }
    std::shared_ptr<erhe::Item_base> item = acquire(key, out_error);
    if (!item) {
        return {};
    }
    const std::string name{hold_name};
    const std::string label = fmt::format("debug hold '{}'", name);
    auto [it, inserted] = m_debug_holds.try_emplace(name, key, label);
    if (!inserted) {
        it->second.set_key(key); // releases the previous usership
        it->second.set_user_label(label);
    }
    it->second.resolve(*this); // same key, same object; registers the hold as a user
    return item;
}

auto Asset_manager::debug_release(const std::string_view hold_name) -> bool
{
    verify_main_thread();
    return m_debug_holds.erase(std::string{hold_name}) > 0;
}

auto Asset_manager::get_debug_holds() const -> const std::map<std::string, Asset_reference>&
{
    return m_debug_holds;
}

void Asset_manager::register_user(Asset_reference* reference)
{
    ERHE_VERIFY(reference != nullptr);
    const std::shared_ptr<erhe::Item_base>& item = reference->get();
    ERHE_VERIFY(item);
    m_users[item.get()].push_back(reference);
}

void Asset_manager::unregister_user(Asset_reference* reference)
{
    ERHE_VERIFY(reference != nullptr);
    const std::shared_ptr<erhe::Item_base>& item = reference->get();
    if (!item) {
        return;
    }
    const auto user_it = m_users.find(item.get());
    if (user_it == m_users.end()) {
        return;
    }
    std::vector<Asset_reference*>& references = user_it->second;
    references.erase(std::remove(references.begin(), references.end(), reference), references.end());
    if (references.empty()) {
        m_users.erase(user_it);
    }
}

}
