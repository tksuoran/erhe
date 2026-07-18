#include "assets/asset_manager.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "assets/asset_paths.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/texture.hpp"
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

auto Asset_container_record::get_scene_entries(const Asset_type type) -> Scene_entries*
{
    switch (type) {
        case Asset_type::brush:     return &scene_brushes;
        case Asset_type::material:  return &scene_materials;
        case Asset_type::animation: return &scene_animations;
        default:                    return nullptr;
    }
}

auto Asset_container_record::get_scene_entries(const Asset_type type) const -> const Scene_entries*
{
    switch (type) {
        case Asset_type::brush:     return &scene_brushes;
        case Asset_type::material:  return &scene_materials;
        case Asset_type::animation: return &scene_animations;
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

[[nodiscard]] auto describe_scene_record(const Asset_container_record& record) -> std::string
{
    return record.display_path.empty()
        ? fmt::format("scene '{}' (session)", record.scene_name)
        : fmt::format("scene '{}' ('{}')", record.scene_name, record.display_path);
}

// Resolution within a scene record - the scene's own library objects, held
// strongly by the record since the R5.6 flip. Same precedence as the parsed
// container path (uid wins, unique name is the fallback, ambiguity is an
// error), but names are read LIVE from the items: scene content is mutable,
// so there are no prebuilt name maps and uniqueness is evaluated against
// current names.
auto resolve_in_scene_record(const Asset_container_record& record, const Asset_key& key, std::string& out_error) -> std::shared_ptr<erhe::Item_base>
{
    const Asset_container_record::Scene_entries* entries = record.get_scene_entries(key.type);
    if (entries == nullptr) {
        out_error = fmt::format(
            "acquisition of type '{}' from a scene is not supported (scene records serve brushes, materials and animations)",
            c_str(key.type)
        );
        return {};
    }
    if (!key.uid.empty()) {
        for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
            if (item->get_gltf_uid() == key.uid) {
                return item;
            }
        }
        // Fall through to the name fallback, matching resolve_in_container:
        // the uid may predate a re-authored scene.
    }
    if (!key.name.empty()) {
        std::shared_ptr<erhe::Item_base> match;
        std::size_t                      match_count = 0;
        for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
            if (item->get_name() == key.name) {
                ++match_count;
                if (!match) {
                    match = item;
                }
            }
        }
        if (match_count == 1) {
            if (!key.uid.empty()) {
                log_asset->warn(
                    "{}: uid '{}' not found; {} resolved by name '{}' instead",
                    describe_scene_record(record), key.uid, c_str(key.type), key.name
                );
            }
            return match;
        }
        if (match_count > 1) {
            out_error = fmt::format(
                "{} name '{}' is ambiguous in {} ({} matches); addressing requires a uid (decision 11)",
                c_str(key.type), key.name, describe_scene_record(record), match_count
            );
            return {};
        }
    }
    out_error = fmt::format(
        "no {} with{}{} in {}",
        c_str(key.type),
        key.uid.empty()  ? "" : fmt::format(" uid '{}'", key.uid),
        key.name.empty() ? (key.uid.empty() ? " an empty key" : "") : fmt::format(" name '{}'", key.name),
        describe_scene_record(record)
    );
    return {};
}

// Unique-current-name lookup over a scene record's entries, the scene-record
// counterpart of Type_entries::by_unique_name used by resolve_scene_local.
// Null when absent or ambiguous.
auto find_scene_entry_by_unique_name(const Asset_container_record& record, const Asset_type type, const std::string& name) -> std::shared_ptr<erhe::Item_base>
{
    const Asset_container_record::Scene_entries* entries = record.get_scene_entries(type);
    if (entries == nullptr) {
        return {};
    }
    std::shared_ptr<erhe::Item_base> match;
    std::size_t                      match_count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
        if (item->get_name() == name) {
            ++match_count;
            if (!match) {
                match = item;
            }
        }
    }
    return (match_count == 1) ? match : std::shared_ptr<erhe::Item_base>{};
}

// Applies fn(type_info, item) to every managed asset the record keeps
// alive: the parsed container entries and (for scene records) the strong
// scene entries.
template <typename Fn>
void visit_record_assets(const Asset_container_record& record, Fn&& fn)
{
    for (const Asset_type_info& info : get_asset_type_infos()) {
        const Asset_container_record::Type_entries* type_entries = record.get_type_entries(info.type);
        if (type_entries != nullptr) {
            for (const std::shared_ptr<erhe::Item_base>& item : type_entries->items) {
                fn(info, item);
            }
        }
        const Asset_container_record::Scene_entries* scene_entries = record.get_scene_entries(info.type);
        if (scene_entries != nullptr) {
            for (const std::shared_ptr<erhe::Item_base>& item : scene_entries->items) {
                fn(info, item);
            }
        }
    }
}

}

Asset_manager::Asset_manager(App_context& context, App_message_bus& app_message_bus, App_scenes& app_scenes)
    : m_context        {context}
    , m_app_message_bus{app_message_bus}
    , m_app_scenes     {app_scenes}
{
    m_close_scene_subscription = m_app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(message.scene_root.get());
        }
    );
}

Asset_manager::~Asset_manager() noexcept
{
    // Unhook the content libraries' bookkeeping pointers: libraries (and
    // their scenes) can outlive this manager during editor teardown, and
    // their claim/release walks must not call into a destroyed manager.
    for (const auto& [container_id, record] : m_containers) {
        const std::shared_ptr<Scene_root> scene_root = record->scene_root.lock();
        if (scene_root) {
            const std::shared_ptr<Content_library> library = scene_root->get_content_library();
            if (library) {
                library->set_asset_manager(nullptr);
            }
        }
    }
    // Debug holds unregister into m_users on destruction; drop them while
    // every registry map is still fully alive.
    m_debug_holds.clear();
    // Detach every still-registered Asset_reference: teardown order lets
    // scene-owned holders (library entry userships, graph nodes) outlive
    // this manager, and their destructors must not unregister into a
    // destroyed registry.
    for (auto& [item, references] : m_users) {
        for (Asset_reference* reference : references) {
            reference->on_manager_teardown();
        }
    }
    m_users.clear();
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
    if (record.is_scene_record()) {
        // A scene record serves THE scene's objects through its strong
        // entries - one object regardless of which direction reached it
        // first, and also after the scene closed with a refused courtesy
        // unload (the record then survives as a plain loaded container).
        return resolve_in_scene_record(record, key, out_error);
    }
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
    // first (builtins, then containers in path order), then open scene
    // libraries in registration order; the first match wins and
    // multi-matches are debug-logged. Match counting dedupes by object
    // identity (R5.5): a path-bound open scene's record entries and the
    // scene library walk below find the SAME object - one object is one
    // candidate.
    std::shared_ptr<erhe::Item_base> match;
    std::size_t                      match_count = 0;
    static thread_local std::vector<const erhe::Item_base*> s_seen;
    s_seen.clear();
    const auto consider = [&match, &match_count](const std::shared_ptr<erhe::Item_base>& candidate) {
        if (!candidate) {
            return;
        }
        if (std::find(s_seen.begin(), s_seen.end(), candidate.get()) != s_seen.end()) {
            return;
        }
        s_seen.push_back(candidate.get());
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
    for (const auto& [path, container_id] : m_path_index) {
        const std::shared_ptr<Asset_container_record> record = m_containers.at(container_id);
        if (record->is_scene_record()) {
            // Path-bound scene records participate through their strong
            // entries (unique-current-name semantics, the mutable
            // counterpart of by_unique_name).
            consider(find_scene_entry_by_unique_name(*record, key.type, key.name));
            continue;
        }
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
            const std::shared_ptr<Asset_container_record> record = find_record_by_path(normalize_asset_path(std::filesystem::path{key.path}));
            if (!record) {
                return {};
            }
            return resolve_in_container(*record, key, ignored_error);
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

auto Asset_manager::find_loaded_by_id(const std::size_t item_id) const -> std::shared_ptr<erhe::Item_base>
{
    for (const auto& [type, by_name] : m_builtins) {
        for (const auto& [name, item] : by_name) {
            if (item->get_id() == item_id) {
                return item;
            }
        }
    }
    // visit_record_assets covers both the parsed container entries and the
    // strong scene entries, so scene-defined assets are id-addressable too.
    std::shared_ptr<erhe::Item_base> found;
    for (const auto& [container_id, record] : m_containers) {
        visit_record_assets(
            *record,
            [item_id, &found](const Asset_type_info&, const std::shared_ptr<erhe::Item_base>& item) {
                if (!found && (item->get_id() == item_id)) {
                    found = item;
                }
            }
        );
        if (found) {
            return found;
        }
    }
    return {};
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
    for (const auto& [container_id, record] : m_containers) {
        const Asset_container_record::Type_entries* entries = record->get_type_entries(key.type);
        if (entries != nullptr) {
            for (const std::shared_ptr<erhe::Item_base>& container_item : entries->items) {
                if (container_item.get() == &item) {
                    key.scope = Asset_scope::file;
                    key.path  = record->display_path;
                    return key;
                }
            }
        }
        // R5.7 key flip (resolution 8): assets DEFINED in a path-bound scene
        // record produce durable file keys {path, uid, name} - exact
        // identity, resolvable through record adoption whether the file is
        // later loaded as a container or opened as a scene. Restricted to
        // the types a loaded container can serve WITHOUT its scene open
        // (materials, animations): brushes only materialize when the
        // editor-state import consumes ERHE_brushes, so a brush file key
        // could not resolve against a plain container load (a startup slot
        // resolve would latch failed) - brushes keep scene_local keys until
        // containers serve them (R5.9 / R7 territory). Session (never-saved)
        // scene records have no path to serialize and stay scene_local too.
        if (
            record->is_scene_record()          &&
            !record->canonical_path.empty()    &&
            ((key.type == Asset_type::material) || (key.type == Asset_type::animation))
        ) {
            const Asset_container_record::Scene_entries* scene_entries = record->get_scene_entries(key.type);
            if (scene_entries != nullptr) {
                for (const std::shared_ptr<erhe::Item_base>& scene_item : scene_entries->items) {
                    if (scene_item.get() == &item) {
                        key.scope = Asset_scope::file;
                        key.path  = record->display_path;
                        return key;
                    }
                }
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

void Asset_manager::mark_item_dirty(const erhe::Item_base& item)
{
    verify_main_thread();
    // Own scan instead of find_defining_record: that helper matches SCENE
    // records only (the defining-scene lookups depend on it), while an edit
    // can just as well hit a plain loaded container's asset (a slot-held or
    // debug-held material edited while no scene has the file open).
    for (const auto& [container_id, record] : m_containers) {
        bool holds_item = false;
        visit_record_assets(
            *record,
            [&item, &holds_item](const Asset_type_info&, const std::shared_ptr<erhe::Item_base>& entry) {
                holds_item = holds_item || (entry.get() == &item);
            }
        );
        if (!holds_item) {
            continue;
        }
        if (!record->dirty) {
            record->dirty = true;
            log_asset->info(
                "container record {} ({}) marked dirty by edit of {} '{}'",
                record->id,
                record->is_scene_record() ? describe_scene_record(*record) : record->display_path,
                c_str(asset_type_from_item(item)), item.get_name()
            );
        }
        return;
    }
}

void Asset_manager::on_scene_saved(Scene_root& scene_root)
{
    verify_main_thread();
    const std::shared_ptr<Asset_container_record> record = find_scene_record(&scene_root);
    if (!record || !record->dirty) {
        return;
    }
    record->dirty = false;
    log_asset->info("scene '{}' saved; container record {} is clean", scene_root.get_name(), record->id);
}

auto Asset_manager::save_container(const std::filesystem::path& path, std::string& out_error) -> bool
{
    verify_main_thread();
    const std::shared_ptr<Asset_container_record> record = find_record_by_path(normalize_asset_path(path));
    if (!record) {
        out_error = fmt::format("container '{}' is not loaded", asset_path_to_string(normalize_asset_path(path)));
        return false;
    }
    if (record->is_open_as_scene()) {
        const std::shared_ptr<Scene_root> scene_root = record->scene_root.lock();
        if (!scene_root) {
            out_error = fmt::format("record {} is open as scene '{}' but the scene is gone", record->id, record->scene_name);
            return false;
        }
        const bool ok = save_scene_gltf(m_context, *scene_root, record->canonical_path);
        if (!ok) {
            out_error = fmt::format("scene save of '{}' failed", record->display_path);
        }
        return ok;
    }
    out_error = record->dirty
        ? fmt::format("dirty non-scene container '{}' cannot be written back until the R7 asset-file save; unload with discard to drop the edits", record->display_path)
        : fmt::format("non-scene container '{}' has no save path yet (R7 asset-file save)", record->display_path);
    return false;
}

auto Asset_manager::request_unload(const Asset_key& key, const bool discard) -> Unload_result
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
    std::shared_ptr<Asset_container_record> record = find_record_by_path(canonical_path);
    if (!record) {
        result.message = fmt::format("container '{}' is not loaded", asset_path_to_string(canonical_path));
        return result;
    }
    if (record->is_open_as_scene()) {
        result.message = fmt::format(
            "'{}' is open as scene '{}'; close the scene instead of unloading its identity record",
            record->display_path, record->scene_name
        );
        return result;
    }
    // R5.8 dirty-unload policy: unloading a dirty container silently drops
    // live edits, so it is refused with a reason; discard=true is the
    // explicit opt-in. The courtesy unload at scene close deliberately does
    // NOT take this path - closing a scene discards its unsaved state by
    // design (the close itself is the user's decision).
    if (record->dirty && !discard) {
        result.message = fmt::format(
            "container '{}' has unsaved changes; save it first, or pass discard to drop the edits",
            record->display_path
        );
        return result;
    }
    return unload_record(std::move(record));
}

auto Asset_manager::unload_record(std::shared_ptr<Asset_container_record> record) -> Unload_result
{
    Unload_result result;
    const std::string description = record->is_scene_record()
        ? describe_scene_record(*record)
        : fmt::format("container '{}'", record->display_path);

    // Refuse while any asset of the record has users, naming them. Direct
    // holders are declared users (Asset_reference); the undo/redo history's
    // INDIRECT pins - retained node subtrees whose mesh primitives hold
    // materials - are consulted through
    // Operation_stack::collect_item_references (R5.4, resolution 7
    // mechanism b) and refuse with a collective label.
    std::unordered_set<const erhe::Item_base*> history_items;
    if (m_context.operation_stack != nullptr) {
        m_context.operation_stack->collect_item_references(history_items);
    }
    std::vector<std::pair<std::weak_ptr<erhe::Item_base>, std::string>> weak_assets;
    visit_record_assets(
        *record,
        [this, &result, &history_items, &weak_assets](const Asset_type_info& info, const std::shared_ptr<erhe::Item_base>& item) {
            const auto user_it = m_users.find(item.get());
            if (user_it != m_users.end()) {
                for (const Asset_reference* reference : user_it->second) {
                    result.users.push_back(Asset_user_info{reference->get_user_label(), item->get_name()});
                }
            }
            if (history_items.contains(item.get())) {
                result.users.push_back(Asset_user_info{"undo/redo history (clear history to release)", item->get_name()});
            }
            weak_assets.emplace_back(item, fmt::format("{} '{}'", info.name, item->get_name()));
        }
    );
    if (!result.users.empty()) {
        std::string user_list;
        for (const Asset_user_info& user : result.users) {
            if (!user_list.empty()) {
                user_list += ", ";
            }
            user_list += fmt::format("{} ({} '{}')", user.label, "using", user.asset_name);
        }
        result.message = fmt::format("{} is in use by: {}", description, user_list);
        return result;
    }

    if (!record->canonical_path.empty()) {
        const auto index_it = m_path_index.find(record->canonical_path);
        if ((index_it != m_path_index.end()) && (index_it->second == record->id)) {
            m_path_index.erase(index_it);
        }
    }
    m_containers.erase(record->id);
    record.reset(); // drop the last strong reference before verifying exclusivity

    // Exclusivity verification: a managed asset still lockable after the
    // record dropped means a raw shared_ptr bypassed Asset_reference - an
    // undeclared user (plan decision 2).
    for (const auto& [weak_item, item_description] : weak_assets) {
        if (weak_item.lock()) {
            ++result.undeclared_survivors;
            log_asset->warn(
                "undeclared asset user: {} of unloaded {} is still alive (a raw shared_ptr bypassed Asset_reference)",
                item_description, description
            );
        }
    }
    result.ok = true;
    result.message = (result.undeclared_survivors == 0)
        ? fmt::format("{} unloaded; all {} managed assets released", description, weak_assets.size())
        : fmt::format("{} unloaded with {} undeclared surviving assets (see log)", description, result.undeclared_survivors);
    log_asset->info("{}", result.message);
    return result;
}

void Asset_manager::courtesy_unload_container(const std::uint64_t record_id)
{
    verify_main_thread();
    const auto record_it = m_containers.find(record_id);
    if (record_it == m_containers.end()) {
        return;
    }
    std::shared_ptr<Asset_container_record> record = record_it->second;
    const std::string description = record->is_scene_record()
        ? describe_scene_record(*record)
        : fmt::format("container '{}'", record->display_path);
    const Unload_result result = unload_record(std::move(record));
    if (!result.ok) {
        // Refusal is the normal path when declared users remain (slots,
        // other scenes' reference entries, debug holds): the container
        // stays loaded with the refusing users intact - teardown symmetry,
        // not eviction policy (plan resolution 4). unload_record already
        // logged the success case.
        log_asset->info("courtesy unload (scene close) refused; {} stays loaded: {}", description, result.message);
    }
}

auto Asset_manager::get_or_load_container(const std::filesystem::path& path, std::string& out_error) -> std::shared_ptr<Asset_container_record>
{
    verify_main_thread();
    const std::filesystem::path canonical_path = normalize_asset_path(path);
    const std::shared_ptr<Asset_container_record> existing = find_record_by_path(canonical_path);
    if (existing) {
        // A scene's record IS the container for its path - file-scope
        // acquisition against a path open as a scene serves THE scene's
        // objects through the record's strong entries. The remaining
        // pre-single-loader seam is the open direction (opening a scene
        // over an already-loaded container), which still warns in
        // bind_record_path until the R5.7 record adoption.
        return existing;
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

    // R6 documented v1 restriction: container loads do NOT resolve
    // ERHE_asset_reference proxies transitively (which also makes reference
    // cycles impossible by construction). A contained proxy is served with
    // its stub fallback appearance; opening the file as a scene resolves
    // its proxies properly.
    for (std::size_t i = 0; i < record->gltf_data.material_extensions.size(); ++i) {
        for (const auto& [extension_name, extension_json] : record->gltf_data.material_extensions[i].entries) {
            if ((extension_name == "ERHE_asset_reference") && (i < record->gltf_data.materials.size()) && record->gltf_data.materials[i]) {
                log_asset->warn(
                    "asset container '{}': material '{}' is a cross-container reference proxy; nested reference resolution is not supported - serving the stub fallback",
                    record->display_path, record->gltf_data.materials[i]->get_name()
                );
            }
        }
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
    m_containers.emplace(record->id, record);
    const auto [index_it, index_inserted] = m_path_index.try_emplace(canonical_path, record->id);
    // Any record already bound to this path (scene identity or loaded
    // container) returned at the top, so the slot must be free here.
    ERHE_VERIFY(index_inserted);
    return record;
}

auto Asset_manager::find_record_by_path(const std::filesystem::path& canonical_path) const -> std::shared_ptr<Asset_container_record>
{
    const auto index_it = m_path_index.find(canonical_path);
    if (index_it == m_path_index.end()) {
        return {};
    }
    const auto record_it = m_containers.find(index_it->second);
    ERHE_VERIFY(record_it != m_containers.end());
    return record_it->second;
}

auto Asset_manager::find_scene_record(const Scene_root* scene_root) const -> std::shared_ptr<Asset_container_record>
{
    if (scene_root == nullptr) {
        return {};
    }
    for (const auto& [container_id, record] : m_containers) {
        if (record->scene_root_identity == scene_root) {
            return record;
        }
    }
    return {};
}

void Asset_manager::bind_record_path(Asset_container_record& record, const std::filesystem::path& path)
{
    if (!record.canonical_path.empty()) {
        const auto index_it = m_path_index.find(record.canonical_path);
        if ((index_it != m_path_index.end()) && (index_it->second == record.id)) {
            m_path_index.erase(index_it);
        }
    }
    record.canonical_path.clear();
    record.display_path.clear();
    if (path.empty()) {
        return;
    }
    record.canonical_path = normalize_asset_path(path);
    record.display_path   = asset_path_to_string(record.canonical_path);
    const auto [index_it, inserted] = m_path_index.try_emplace(record.canonical_path, record.id);
    if (!inserted && (index_it->second != record.id)) {
        // Scene open no longer collides here (R5.7 registration adopts the
        // bound record or takes its slot); what remains is save-as onto a
        // path some OTHER record already serves (a loaded container or
        // another open scene) - two records then share one path and the
        // established one keeps the index slot.
        log_asset->warn(
            "path '{}' is already bound to container record {}; record {} keeps the path but not the index slot (save-as onto an already-served path)",
            record.display_path, index_it->second, record.id
        );
    }
}

void Asset_manager::on_scene_registered(const std::shared_ptr<Scene_root>& scene_root)
{
    verify_main_thread();
    ERHE_VERIFY(scene_root);
    const std::shared_ptr<Asset_container_record> existing = find_scene_record(scene_root.get());
    if (existing) {
        if (existing->is_open_as_scene()) {
            log_asset->warn("scene '{}' already has container record {}", scene_root->get_name(), existing->id);
            return;
        }
        if (existing->scene_root.lock() == scene_root) {
            // The SAME living Scene_root re-registers (redo of a scene open /
            // create re-registers without re-importing): re-open its record.
            existing->scene_open = true;
            existing->scene_name = scene_root->get_name();
            log_asset->info(
                "scene '{}' re-registered, container record {} re-opened ({})",
                scene_root->get_name(), existing->id,
                existing->canonical_path.empty() ? std::string{"session"} : existing->display_path
            );
            arm_scene_library(scene_root);
            return;
        }
        // A closed-but-surviving record whose identity pointer matches this
        // scene's address but whose weak link is dead: the address was
        // reused after an unregistration that never reached the
        // courtesy-unload detach (a teardown path). Sever the stale identity
        // so the survivor cannot masquerade as the new scene, then register
        // the new scene normally.
        log_asset->warn(
            "container record {} held a stale scene identity matching new scene '{}'; severing it",
            existing->id, scene_root->get_name()
        );
        existing->scene_root_identity = nullptr;
        existing->scene_root.reset();
    }

    // R5.7 record adoption (plan resolution 3): a record already bound to
    // this scene's source path becomes THE scene's record - one record, one
    // parse, both directions. A loaded container record carries its parse
    // for the import to take (take_adopted_parse); a surviving closed scene
    // record (refused courtesy unload) has no parse to give, so it releases
    // the path-index slot to the reopening scene instead and lives on as an
    // unaddressable session survivor (its objects stay pinned for the
    // slots / references that refused the unload).
    const std::filesystem::path source_path = scene_root->get_source_path();
    if (!source_path.empty()) {
        const std::shared_ptr<Asset_container_record> bound = find_record_by_path(normalize_asset_path(source_path));
        if (bound && !bound->is_open_as_scene()) {
            if (!bound->is_scene_record()) {
                bound->scene_record        = true;
                bound->scene_open          = true;
                bound->scene_name          = scene_root->get_name();
                bound->scene_root          = scene_root;
                bound->scene_root_identity = scene_root.get();
                log_asset->info(
                    "scene '{}' adopted loaded container record {} ('{}')",
                    scene_root->get_name(), bound->id, bound->display_path
                );
                arm_scene_library(scene_root);
                return;
            }
            log_asset->info(
                "surviving scene record {} ('{}') released its path binding to reopening scene '{}'",
                bound->id, bound->display_path, scene_root->get_name()
            );
            bind_record_path(*bound, std::filesystem::path{});
        }
    }

    std::shared_ptr<Asset_container_record> record = std::make_shared<Asset_container_record>();
    record->id                  = m_next_container_id++;
    record->scene_record        = true;
    record->scene_open          = true;
    record->scene_name          = scene_root->get_name();
    record->scene_root          = scene_root;
    record->scene_root_identity = scene_root.get();
    m_containers.emplace(record->id, record);
    // An opened scene arrives with its source path already set (open paths
    // call set_source_path before registering); a created scene is a
    // session record (empty path) until its first save.
    bind_record_path(*record, scene_root->get_source_path());
    log_asset->info(
        "scene '{}' registered as container record {} ({})",
        scene_root->get_name(), record->id,
        record->canonical_path.empty() ? std::string{"session"} : record->display_path
    );

    arm_scene_library(scene_root);
}

// Sweep the scene's already-present library entries into its record (a
// scene can register fully populated - e.g. redo of a scene open
// re-registers without re-importing), then arm the library's claim/release
// hooks for changes from here on. The hook gives owning entries their
// strong record entry and every asset-typed entry its usership; both are
// idempotent, so re-arming on re-registration is safe.
void Asset_manager::arm_scene_library(const std::shared_ptr<Scene_root>& scene_root)
{
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (!library) {
        return;
    }
    library->set_asset_manager(this);
    if (library->root) {
        erhe::Item_host* const owner = static_cast<erhe::Item_host*>(scene_root.get());
        library->root->for_each<Content_library_node>(
            [this, owner](Content_library_node& node) -> bool {
                if (node.item) {
                    on_library_node_attached(owner, node);
                }
                return true;
            }
        );
    }
}

auto Asset_manager::find_adoptable_container(const std::filesystem::path& path) const -> std::shared_ptr<Asset_container_record>
{
    if (path.empty()) {
        return {};
    }
    const std::shared_ptr<Asset_container_record> record = find_record_by_path(normalize_asset_path(path));
    if (!record || record->is_scene_record() || record->is_open_as_scene()) {
        return {};
    }
    const bool has_parse =
        !record->gltf_data.nodes.empty()     ||
        !record->gltf_data.materials.empty() ||
        !record->gltf_data.animations.empty();
    if (!has_parse || !record->root_node) {
        return {};
    }
    return record;
}

auto Asset_manager::take_adopted_parse(const Scene_root& scene_root, const std::filesystem::path& path) -> std::optional<Adopted_container_parse>
{
    verify_main_thread();
    const std::shared_ptr<Asset_container_record> record = find_scene_record(&scene_root);
    if (!record || record->canonical_path.empty()) {
        return std::nullopt;
    }
    if (record->canonical_path != normalize_asset_path(path)) {
        return std::nullopt;
    }
    const bool has_parse =
        !record->gltf_data.nodes.empty()     ||
        !record->gltf_data.materials.empty() ||
        !record->gltf_data.animations.empty();
    if (!has_parse || !record->root_node) {
        return std::nullopt;
    }
    Adopted_container_parse parse;
    parse.gltf_data = std::move(record->gltf_data);
    parse.root_node = std::move(record->root_node);
    // Structure pins are gone (resolution 3: adopted nodes must die with
    // the scene). The parsed Type_entries go too: the import attaches the
    // SAME asset objects to the scene's library, and the attach hooks hand
    // the record its strong scene entries - keeping the Type_entries would
    // pin objects the scene later deletes from its library.
    record->gltf_data = erhe::gltf::Gltf_data{};
    record->materials  = Asset_container_record::Type_entries{};
    record->animations = Asset_container_record::Type_entries{};
    log_asset->info(
        "scene '{}' took adopted parse of container record {} ('{}')",
        record->scene_name, record->id, record->display_path
    );
    return parse;
}

void Asset_manager::on_scene_unregistered(Scene_root* scene_root)
{
    verify_main_thread();
    // Disarm the library hooks. Safe also mid-~Scene_root (the fallback
    // unregistration path): the destructor body has not touched the
    // members yet when it unregisters.
    if (scene_root != nullptr) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library) {
            library->set_asset_manager(nullptr);
        }
    }
    const std::shared_ptr<Asset_container_record> record = find_scene_record(scene_root);
    if (!record) {
        // Scenes registered before the Asset_manager existed have no record;
        // in practice every registration happens at runtime (startup script
        // or later), so this indicates a bookkeeping bug.
        log_asset->warn("unregistered scene has no container record");
        return;
    }
    // R5.6: the record survives unregistration - it owns the scene's assets
    // until the courtesy unload at the watchdog arming point releases them
    // (or declared users keep the container loaded). The identity pointers
    // stay matchable through the close dispatch so every close_scene
    // subscriber's defining-scene lookups keep working regardless of
    // subscription order; the Editor severs them via detach_scene_record.
    record->scene_open = false;
    std::size_t asset_count = 0;
    for (const Asset_type_info& info : get_asset_type_infos()) {
        const Asset_container_record::Scene_entries* entries = record->get_scene_entries(info.type);
        if (entries != nullptr) {
            asset_count += entries->items.size();
        }
    }
    log_asset->info(
        "scene '{}' container record {} closed ({}; keeps {} assets until the courtesy unload)",
        record->scene_name, record->id,
        record->canonical_path.empty() ? std::string{"session"} : record->display_path,
        asset_count
    );
}

auto Asset_manager::detach_scene_record(Scene_root* const scene_root) -> std::uint64_t
{
    verify_main_thread();
    const std::shared_ptr<Asset_container_record> record = find_scene_record(scene_root);
    if (!record) {
        return 0;
    }
    record->scene_open          = false;
    record->scene_root_identity = nullptr;
    record->scene_root.reset();
    return record->id;
}

void Asset_manager::on_scene_source_path_changed(Scene_root& scene_root)
{
    verify_main_thread();
    const std::shared_ptr<Asset_container_record> record = find_scene_record(&scene_root);
    if (!record) {
        log_asset->warn("scene '{}' changed source path but has no container record", scene_root.get_name());
        return;
    }
    const bool was_session = record->canonical_path.empty();
    bind_record_path(*record, scene_root.get_source_path());
    if (record->canonical_path.empty()) {
        log_asset->info("scene '{}' container record {} unbound (session)", scene_root.get_name(), record->id);
    } else {
        log_asset->info(
            "scene '{}' container record {} {} '{}'",
            scene_root.get_name(), record->id,
            was_session ? "bound to" : "re-homed to",
            record->display_path
        );
    }
}

void Asset_manager::on_close_scene(Scene_root* scene_root)
{
    verify_main_thread();
    if (scene_root == nullptr) {
        return;
    }
    // Resolution 5: a surviving animation must not pin the closing scene's
    // nodes (nodes must die with their scene; the watchdog's must-die half
    // would flag them). Reset every channel target pointing into the
    // closing scene on every manager-known animation - channels keep their
    // sampler data, the target becomes null and Animation::apply skips it.
    erhe::Item_host* const closing_host = static_cast<erhe::Item_host*>(scene_root);
    std::size_t dropped_target_count = 0;
    for (const auto& [container_id, record] : m_containers) {
        visit_record_assets(
            *record,
            [closing_host, &dropped_target_count](const Asset_type_info& info, const std::shared_ptr<erhe::Item_base>& item) {
                if (info.type != Asset_type::animation) {
                    return;
                }
                erhe::scene::Animation* const animation = dynamic_cast<erhe::scene::Animation*>(item.get());
                if (animation == nullptr) {
                    return;
                }
                for (erhe::scene::Animation_channel& channel : animation->channels) {
                    if (channel.target && (channel.target->get_item_host() == closing_host)) {
                        channel.target.reset();
                        ++dropped_target_count;
                    }
                }
            }
        );
    }
    if (dropped_target_count > 0) {
        log_asset->info(
            "scene '{}' closing: dropped {} animation channel node target(s)",
            scene_root->get_name(), dropped_target_count
        );
    }
}

auto Asset_manager::find_record_by_owner(const erhe::Item_host* owner) const -> std::shared_ptr<Asset_container_record>
{
    if (owner == nullptr) {
        return {};
    }
    for (const auto& [container_id, record] : m_containers) {
        if (
            (record->scene_root_identity != nullptr) &&
            (static_cast<const erhe::Item_host*>(record->scene_root_identity) == owner)
        ) {
            return record;
        }
    }
    return {};
}

void Asset_manager::on_library_node_attached(erhe::Item_host* const owner, Content_library_node& node)
{
    verify_main_thread();
    if (!node.item) {
        return;
    }
    const Asset_type type = asset_type_from_item(*node.item);
    if (!is_manager_owned_asset_type(type)) {
        return;
    }
    const std::shared_ptr<Asset_container_record> record = find_record_by_owner(owner);
    if (!record) {
        return;
    }
    if (!node.is_reference) {
        Asset_container_record::Scene_entries* entries = record->get_scene_entries(type);
        ERHE_VERIFY(entries != nullptr);
        // Idempotent: handle_add_child claims whole subtrees and can
        // re-visit an already-listed item; one object is one entry.
        const auto i = std::find(entries->items.begin(), entries->items.end(), node.item);
        if (i == entries->items.end()) {
            entries->items.push_back(node.item);
            log_asset->trace("scene container record {}: registered {} '{}'", record->id, c_str(type), node.item->get_name());
        }
    }
    // Every asset-typed library entry is a declared user (R5.6): definition
    // entries pin their own scene's asset; reference entries pin the listed
    // asset of its defining container, converting the former undeclared
    // library pin into a named unload refusal.
    if (!node.asset_usership) {
        node.asset_usership = std::make_unique<Asset_reference>();
    }
    if (node.asset_usership->get() != node.item) {
        node.asset_usership->set_user_label(
            fmt::format(
                "scene '{}' library {} '{}'{}",
                record->scene_name, c_str(type), node.item->get_name(),
                node.is_reference ? " (reference)" : ""
            )
        );
        node.asset_usership->adopt(*this, node.item);
    }
}

void Asset_manager::on_library_node_detached(erhe::Item_host* const owner, Content_library_node& node)
{
    verify_main_thread();
    if (!node.item) {
        return;
    }
    const Asset_type type = asset_type_from_item(*node.item);
    if (!is_manager_owned_asset_type(type)) {
        return;
    }
    node.asset_usership.reset();
    if (node.is_reference) {
        return;
    }
    const std::shared_ptr<Asset_container_record> record = find_record_by_owner(owner);
    if (!record) {
        return;
    }
    Asset_container_record::Scene_entries* entries = record->get_scene_entries(type);
    ERHE_VERIFY(entries != nullptr);
    const auto i = std::remove(entries->items.begin(), entries->items.end(), node.item);
    if (i != entries->items.end()) {
        entries->items.erase(i, entries->items.end());
        log_asset->trace("scene container record {}: unregistered {} '{}'", record->id, c_str(type), node.item->get_name());
    }
}

void Asset_manager::register_created(const std::shared_ptr<erhe::Item_base>& item, Scene_root& defining_scene)
{
    verify_main_thread();
    ERHE_VERIFY(item);
    const Asset_type type = asset_type_from_item(*item);
    if (type == Asset_type::none) {
        log_asset->warn(
            "Asset_manager::create() called for '{}' which is not a managed asset type",
            item->get_name()
        );
        return;
    }
    const std::shared_ptr<Asset_container_record> record = find_scene_record(&defining_scene);
    if (!record) {
        // Scenes outside the App_scenes registry (previews, the tool scene)
        // have no record; their assets stay invisible to the manager.
        log_asset->debug(
            "created {} '{}' in unregistered scene '{}'",
            c_str(type), item->get_name(), defining_scene.get_name()
        );
        return;
    }
    log_asset->trace(
        "created {} '{}' in scene '{}' (container record {})",
        c_str(type), item->get_name(), defining_scene.get_name(), record->id
    );
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
    for (const auto& [container_id, record] : m_containers) {
        if (record->is_scene_record()) {
            // Scene records list their strong entries. Path-bound records
            // report file keys (acquirable through the record); session
            // records have no path, so their objects are reported with the
            // scene_local keys that resolve them.
            for (const Asset_type_info& type_info : get_asset_type_infos()) {
                const Asset_container_record::Scene_entries* entries = record->get_scene_entries(type_info.type);
                if (entries == nullptr) {
                    continue;
                }
                for (const std::shared_ptr<erhe::Item_base>& item : entries->items) {
                    Asset_info info;
                    info.key.scope   = record->canonical_path.empty() ? Asset_scope::scene_local : Asset_scope::file;
                    info.key.type    = type_info.type;
                    info.key.path    = record->display_path;
                    info.key.uid     = item->get_gltf_uid();
                    info.key.name    = item->get_name();
                    info.user_labels = user_labels_of(item.get());
                    covered.emplace(item.get(), true);
                    result.push_back(std::move(info));
                }
            }
            continue;
        }
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
    for (const auto& [container_id, record] : m_containers) {
        Asset_container_info info;
        info.id              = record->id;
        info.path            = record->display_path;
        if (record->is_scene_record()) {
            info.brush_count     = record->scene_brushes.items.size();
            info.material_count  = record->scene_materials.items.size();
            info.animation_count = record->scene_animations.items.size();
        } else {
            info.material_count  = record->materials.items.size();
            info.animation_count = record->animations.items.size();
        }
        info.errors          = record->errors;
        info.dirty           = record->dirty;
        info.open_as_scene   = record->is_open_as_scene();
        info.session         = record->canonical_path.empty();
        if (record->is_scene_record()) {
            info.scene_name = record->scene_name;
        }
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
    bool pinned = false;
    for (const auto& [container_id, record] : m_containers) {
        visit_record_assets(
            *record,
            [item, &pinned](const Asset_type_info& info, const std::shared_ptr<erhe::Item_base>& record_item) {
                if (pinned) {
                    return;
                }
                if (record_item.get() == item) {
                    pinned = true;
                    return;
                }
                // Resolution 6 (material -> scene-hosted texture seam):
                // textures referenced by a record-held material are blessed
                // as record-transitive pins - the surviving material keeps
                // them alive and rendering stays correct; the memory stays
                // resident until the container unloads. (The D5 end state
                // resolves textures within the defining container.)
                if (info.type != Asset_type::material) {
                    return;
                }
                const erhe::primitive::Material* const material = dynamic_cast<const erhe::primitive::Material*>(record_item.get());
                if (material == nullptr) {
                    return;
                }
                const erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
                for (const erhe::primitive::Material_texture_sampler* sampler :
                     {&samplers.base_color, &samplers.metallic_roughness, &samplers.normal, &samplers.occlusion, &samplers.emissive})
                {
                    const erhe::Item_base* const texture_item = dynamic_cast<const erhe::Item_base*>(sampler->texture_reference.get());
                    if ((texture_item != nullptr) && (texture_item == item)) {
                        pinned = true;
                        return;
                    }
                }
            }
        );
        if (pinned) {
            return true;
        }
    }
    return false;
}

auto Asset_manager::find_defining_record(const erhe::Item_base& item) const -> std::shared_ptr<Asset_container_record>
{
    const Asset_type type = asset_type_from_item(item);
    if (!is_manager_owned_asset_type(type)) {
        return {};
    }
    for (const auto& [container_id, record] : m_containers) {
        if (!record->is_scene_record()) {
            continue;
        }
        const Asset_container_record::Scene_entries* entries = record->get_scene_entries(type);
        if (entries == nullptr) {
            continue;
        }
        for (const std::shared_ptr<erhe::Item_base>& entry : entries->items) {
            if (entry.get() == &item) {
                return record;
            }
        }
    }
    return {};
}

auto Asset_manager::get_defining_scene_root(const erhe::Item_base& item) const -> std::shared_ptr<Scene_root>
{
    const std::shared_ptr<Asset_container_record> record = find_defining_record(item);
    if (!record) {
        return {};
    }
    return record->scene_root.lock();
}

auto Asset_manager::is_defined_by(const erhe::Item_base& item, const erhe::Item_host* const host) const -> bool
{
    if (host == nullptr) {
        return false;
    }
    const std::shared_ptr<Asset_container_record> record = find_defining_record(item);
    return record && (static_cast<const erhe::Item_host*>(record->scene_root_identity) == host);
}

auto Asset_manager::is_hosted_or_defined_by(const erhe::Item_base& item, const erhe::Item_host* const host) const -> bool
{
    const std::shared_ptr<Asset_container_record> record = find_defining_record(item);
    if (record) {
        // A detached record (scene closed, container surviving) has a null
        // identity: its assets belong to no host, matching a null host
        // exactly like an unhosted item does.
        return static_cast<const erhe::Item_host*>(record->scene_root_identity) == host;
    }
    return item.get_item_host() == host;
}

auto Asset_manager::is_managed(const erhe::Item_base& item) const -> bool
{
    for (const auto& [type, by_name] : m_builtins) {
        for (const auto& [name, builtin_item] : by_name) {
            if (builtin_item.get() == &item) {
                return true;
            }
        }
    }
    bool found = false;
    for (const auto& [container_id, record] : m_containers) {
        visit_record_assets(
            *record,
            [&item, &found](const Asset_type_info&, const std::shared_ptr<erhe::Item_base>& record_item) {
                if (record_item.get() == &item) {
                    found = true;
                }
            }
        );
        if (found) {
            return true;
        }
    }
    return false;
}

auto Asset_manager::is_cross_scene_referenceable(const erhe::Item_base& item) const -> bool
{
    // Resolution 11: accepted when the defining container is path-bound -
    // exactly the condition under which make_key can produce a durable
    // file-scope key, so the reference can always be serialized and
    // re-resolved. Session-only scene assets are rejected. Builtins and
    // items without a defining record (loaded containers' assets, shared
    // prefab template resources) are trivially referenceable.
    const std::shared_ptr<Asset_container_record> record = find_defining_record(item);
    if (record) {
        return !record->canonical_path.empty();
    }
    return true;
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
