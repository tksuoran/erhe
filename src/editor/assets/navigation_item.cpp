#include "assets/navigation_item.hpp"

#include "app_context.hpp"
#include "assets/editor_asset.hpp"
#include "assets/item_keys.hpp"
#include "assets/prepared_keys.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "navigation/navigation_mesh.hpp"
#include "navigation/navigation_mesh_io.hpp"
#include "scene/navigation_visualization.hpp"
#include "scene/scene_navigation.hpp"
#include "scene/scene_root.hpp"
#include "scene/static_mesh_set.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_file/file.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor {

using erhe::asset_store::Asset_store;
using erhe::asset_store::Item_kind;
using erhe::asset_store::Key;

namespace {

[[nodiscard]] auto make_navigation_mesh_stamp() -> std::string
{
    return fmt::format("erhe_navigation={}", c_navigation_mesh_format_version);
}

// The build statistics, which the key does not cover (timings differ between equal builds).
[[nodiscard]] auto make_meta(const Navigation_build_statistics& statistics) -> std::string
{
    return nlohmann::json{
        {"tile_count",           statistics.tile_count},
        {"input_mesh_count",     statistics.input_mesh_count},
        {"input_shape_count",    statistics.input_shape_count},
        {"input_triangle_count", statistics.input_triangle_count},
        {"skipped_mesh_count",   statistics.skipped_mesh_count},
        {"skipped_meshes",       statistics.skipped_meshes},
        {"build_ms",             statistics.build_ms},
        {"flood_ms",             statistics.flood_ms}
    }.dump();
}

[[nodiscard]] auto read_meta(const std::string& meta_text) -> Navigation_build_statistics
{
    Navigation_build_statistics statistics{};
    const nlohmann::json meta = nlohmann::json::parse(meta_text, nullptr, false);
    if (!meta.is_object()) {
        return statistics;
    }
    statistics.tile_count           = meta.value("tile_count",           std::size_t{0});
    statistics.input_mesh_count     = meta.value("input_mesh_count",     std::size_t{0});
    statistics.input_shape_count    = meta.value("input_shape_count",    std::size_t{0});
    statistics.input_triangle_count = meta.value("input_triangle_count", std::size_t{0});
    statistics.skipped_mesh_count   = meta.value("skipped_mesh_count",   std::size_t{0});
    statistics.skipped_meshes       = meta.value("skipped_meshes",       std::vector<std::string>{});
    statistics.build_ms             = meta.value("build_ms",             0.0);
    statistics.flood_ms             = meta.value("flood_ms",             0.0);
    return statistics;
}

[[nodiscard]] auto make_record(const Scene_navigation& navigation) -> Navigation_build_record
{
    Navigation_build_record             record{};
    std::vector<Navigation_seed>        seeds;
    std::vector<Navigation_seed_result> seed_results;
    std::vector<Navigation_input_entry> input;
    navigation.get_seeds(seeds);
    navigation.get_seed_results(seed_results);
    navigation.get_input(input);
    const Navigation_build_settings settings = navigation.get_settings();
    record.seed_search_radius = settings.seed_search_radius;
    record.seed_search_height = settings.seed_search_height;
    for (std::size_t i = 0, end = seeds.size(); i < end; ++i) {
        record.seeds.push_back(Navigation_record_seed{
            .uid    = seeds[i].uid,
            .name   = seeds[i].name,
            .point  = seeds[i].point,
            .result = (i < seed_results.size()) ? seed_results[i] : Navigation_seed_result{}
        });
    }
    for (const Navigation_input_entry& entry : input) {
        record.input.push_back(Navigation_record_input{
            .uid            = entry.uid,
            .root_from_node = entry.root_from_node,
            .shape_hashes   = entry.shape_hashes
        });
    }
    return record;
}

// The opened mesh's shapes against the saved hashes: same count, every one known and equal.
[[nodiscard]] auto are_shape_hashes_equal(const erhe::scene::Mesh& mesh, const std::vector<Navigation_shape_hash>& saved) -> bool
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.size() != saved.size()) {
        return false;
    }
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        const erhe::primitive::Primitive_render_shape* const shape = primitives[i].primitive ? primitives[i].primitive->render_shape.get() : nullptr;
        const std::optional<erhe::hash::Content_hash> content_hash = (shape != nullptr) ? shape->get_content_hash() : std::nullopt;
        if (!saved[i].known || !content_hash.has_value() || (content_hash->bytes != saved[i].bytes)) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

auto store_navigation_item(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& source_path,
    const std::filesystem::path& record_path
) -> bool
{
    ERHE_PROFILE_FUNCTION();
    Scene_navigation& navigation = scene_root.get_navigation();
    const std::shared_ptr<Navigation_mesh> mesh = navigation.get_mesh();
    if (!mesh || (context.editor_asset_registry == nullptr)) {
        return false;
    }
    if (source_path.empty()) {
        log_navigation->info("Navigation mesh item of '{}': the scene has no source file, so no editor asset - not stored", scene_root.get_name());
        return false;
    }
    std::error_code error_code;
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(source_path, error_code);
    const std::shared_ptr<Editor_asset> editor_asset = context.editor_asset_registry->get(error_code ? source_path : canonical_path, record_path);
    if (!editor_asset) {
        log_navigation->error("Navigation mesh item of '{}': '{}' has no writable editor asset - not stored", scene_root.get_name(), erhe::file::to_string(source_path));
        return false;
    }

    const std::vector<uint8_t>       bytes   = write_navigation_mesh(*mesh, make_record(navigation));
    const std::span<const std::byte> payload = std::as_bytes(std::span<const uint8_t>{bytes});
    const std::string                stamp   = make_navigation_mesh_stamp();
    const Key                        key     = make_navigation_mesh_key(stamp, payload);
    Asset_store&                     store   = editor_asset->get_store();
    const std::string                asset_path = erhe::file::to_string(editor_asset->get_asset_path());

    // At most one per editor asset, as the PVS item: every other navigation mesh item goes.
    std::vector<Key> previous;
    if (!store.for_each_item([&previous, &key](const erhe::asset_store::Item_info& info) {
        if ((info.kind == Item_kind::navigation_mesh) && (info.key != key)) {
            previous.push_back(info.key);
        }
    })) {
        log_navigation->error("Navigation mesh item of '{}': the items of '{}' cannot be listed", scene_root.get_name(), asset_path);
        return false;
    }
    const bool made     = !store.contains(key);
    const bool recorded = (get_recorded_key(store, c_navigation_key_record) == key);
    if (made || !previous.empty() || !recorded) {
        erhe::asset_store::Write_transaction transaction{store};
        bool ok = transaction.is_active();
        if (ok && made) {
            ok = transaction.put(erhe::asset_store::Item_data{
                .key     = key,
                .kind    = Item_kind::navigation_mesh,
                .target  = {},
                .stamp   = stamp,
                .meta    = make_meta(navigation.get_statistics()),
                .info    = {},
                .payload = payload
            });
        }
        for (const Key& old_key : previous) {
            ok = ok && transaction.remove(old_key);
        }
        ok = ok && transaction.set_record(c_navigation_key_record, key.to_hex());
        const bool committed = ok && transaction.commit();
        editor_asset->invalidate_item_catalog();
        if (!committed) {
            log_navigation->error("Navigation mesh item of '{}': '{}' took no write - not stored", scene_root.get_name(), asset_path);
            return false;
        }
        scene_root.get_prepared_keys().on_change(Prepared_key_change::stores);
    }
    log_navigation->info(
        "Navigation mesh item {} of '{}' {} '{}' ({} bytes; {} previous navigation mesh item(s) removed)",
        key.to_hex(), scene_root.get_name(), made ? "stored in" : "already in", asset_path, bytes.size(), previous.size()
    );
    const Navigation_item_state state{.key = key.to_hex(), .made = made, .bytes = bytes.size()};
    navigation.set_item(state);
    return true;
}

void remove_navigation_item(App_context& context, Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();
    scene_root.get_navigation().clear_item();
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if (source_path.empty() || (context.editor_asset_registry == nullptr)) {
        return;
    }
    const std::shared_ptr<Editor_asset> editor_asset = context.editor_asset_registry->get(source_path, scene_root.get_editor_asset_record());
    if (!editor_asset) {
        return;
    }
    Asset_store&      store      = editor_asset->get_store();
    const std::string asset_path = erhe::file::to_string(editor_asset->get_asset_path());
    std::vector<Key>  items;
    if (!store.for_each_item([&items](const erhe::asset_store::Item_info& info) {
        if (info.kind == Item_kind::navigation_mesh) {
            items.push_back(info.key);
        }
    })) {
        log_navigation->error("Navigation mesh item of '{}': the items of '{}' cannot be listed", scene_root.get_name(), asset_path);
        return;
    }
    if (items.empty() && !get_recorded_key(store, c_navigation_key_record).has_value()) {
        return;
    }
    {
        erhe::asset_store::Write_transaction transaction{store};
        bool ok = transaction.is_active();
        for (const Key& key : items) {
            ok = ok && transaction.remove(key);
        }
        ok = ok && transaction.set_record(c_navigation_key_record, std::string{});
        const bool committed = ok && transaction.commit();
        editor_asset->invalidate_item_catalog();
        if (!committed) {
            log_navigation->error("Navigation mesh item of '{}': '{}' took no write - the item stays", scene_root.get_name(), asset_path);
            return;
        }
    }
    scene_root.get_prepared_keys().on_change(Prepared_key_change::stores);
    log_navigation->info("Navigation mesh item of '{}': {} item(s) and the record removed from '{}'", scene_root.get_name(), items.size(), asset_path);
}

void clear_navigation(App_context& context, Scene_root& scene_root)
{
    scene_root.get_navigation().clear();
    remove_navigation_item(context, scene_root);
}

void load_navigation_item(App_context& context, Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();
    if ((context.editor_asset_registry == nullptr) || scene_root.get_source_path().empty()) {
        return;
    }
    const std::shared_ptr<Editor_asset> editor_asset = context.editor_asset_registry->find(scene_root.get_source_path());
    if (!editor_asset) {
        return;
    }
    const Asset_store&       store = editor_asset->get_store();
    const std::optional<Key> key   = get_recorded_key(store, c_navigation_key_record);
    if (!key.has_value()) {
        return;
    }
    const std::string asset_path = erhe::file::to_string(editor_asset->get_asset_path());
    const std::optional<erhe::asset_store::Item_info> info = store.get_info(key.value());
    const std::optional<std::string>                  meta = store.read_meta(key.value());
    std::vector<uint8_t> bytes(info.has_value() ? static_cast<std::size_t>(info->payload_size) : 0);
    if (!info.has_value() || !meta.has_value() || !store.read_payload(key.value(), 0, std::as_writable_bytes(std::span<uint8_t>{bytes}))) {
        log_navigation->warn("open_scene_gltf: '{}' records navigation mesh item {} but it cannot be read - no navigation mesh installed", asset_path, key->to_hex());
        return;
    }
    Navigation_build_record record;
    std::string             error;
    std::shared_ptr<Navigation_mesh> mesh = read_navigation_mesh(bytes, record, error);
    if (!mesh) {
        log_navigation->warn("open_scene_gltf: navigation mesh item {} of '{}' is not read ({}) - no navigation mesh installed", key->to_hex(), asset_path, error);
        return;
    }

    // The saved meshes and seeds by glTF uid, in the opened scene with its world transforms propagated.
    scene_root.get_scene().update_node_transforms();
    std::unordered_set<const erhe::scene::Node*> animated_nodes;
    const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    if (content_library) {
        collect_animated_nodes(*content_library, animated_nodes);
    }
    std::vector<Navigation_input_entry>             input;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> input_meshes;
    std::unordered_set<std::size_t>                 added_meshes;
    std::vector<Navigation_seed>                    seeds;
    std::vector<Navigation_seed_result>             seed_results;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root.item_host_mutex};
        std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Mesh>> meshes_by_uid;
        for (const std::shared_ptr<erhe::scene::Mesh>& scene_mesh : scene_root.layers().content()->meshes) {
            meshes_by_uid.emplace(scene_mesh->get_gltf_uid(), scene_mesh);
        }
        std::unordered_set<std::string_view> saved_uids;
        for (const Navigation_record_input& saved : record.input) {
            saved_uids.insert(saved.uid);
            Navigation_input_entry entry{.uid = saved.uid, .root_from_node = saved.root_from_node, .shape_hashes = saved.shape_hashes};
            const std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Mesh>>::const_iterator i = meshes_by_uid.find(saved.uid);
            const std::shared_ptr<erhe::scene::Mesh> scene_mesh = (i != meshes_by_uid.end()) ? i->second : std::shared_ptr<erhe::scene::Mesh>{};
            if (scene_mesh) {
                entry.mesh_id      = scene_mesh->get_id();
                entry.shapes_known = are_shape_hashes_equal(*scene_mesh, saved.shape_hashes);
                if (entry.shapes_known) {
                    for (const erhe::scene::Mesh_primitive& mesh_primitive : scene_mesh->get_primitives()) {
                        entry.shapes.push_back(mesh_primitive.primitive ? std::shared_ptr<const erhe::primitive::Primitive_render_shape>{mesh_primitive.primitive->render_shape} : nullptr);
                    }
                }
            } else {
                entry.shapes_known = false;
            }
            input.push_back(std::move(entry));
            input_meshes.push_back(scene_mesh);
        }
        for (const std::shared_ptr<erhe::scene::Mesh>& scene_mesh : scene_root.layers().content()->meshes) {
            if (!saved_uids.contains(scene_mesh->get_gltf_uid()) && is_navigation_input_mesh(*scene_mesh, animated_nodes)) {
                added_meshes.insert(scene_mesh->get_id());
            }
        }
        std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Node>> nodes_by_uid;
        scene_root.get_scene().for_each_node([&nodes_by_uid](const std::shared_ptr<erhe::scene::Node>& node) {
            nodes_by_uid.emplace(node->get_gltf_uid(), node);
            return true;
        });
        for (const Navigation_record_seed& saved : record.seeds) {
            const std::unordered_map<std::string_view, std::shared_ptr<erhe::scene::Node>>::const_iterator i = nodes_by_uid.find(saved.uid);
            const std::shared_ptr<erhe::scene::Node> node = (i != nodes_by_uid.end()) ? i->second : std::shared_ptr<erhe::scene::Node>{};
            seeds.push_back(Navigation_seed{
                .node    = node,
                .node_id = node ? node->get_id() : 0,
                .name    = saved.name,
                .uid     = saved.uid,
                .point   = saved.point
            });
            seed_results.push_back(saved.result);
        }
    }

    Navigation_build_settings settings = mesh->get_settings();
    settings.seed_search_radius = record.seed_search_radius;
    settings.seed_search_height = record.seed_search_height;
    std::vector<uint8_t> reachable;
    for (const Navigation_polygon& polygon : mesh->get_polygons()) {
        reachable.push_back(polygon.reachable ? 1 : 0);
    }
    std::shared_ptr<erhe::scene::Node> visualization = make_navigation_visualization(context, *mesh, reachable);
    Navigation_build_statistics statistics = read_meta(meta.value());
    statistics.tile_count = mesh->get_tile_count();
    Scene_navigation& navigation = scene_root.get_navigation();
    navigation.install_loaded(
        std::move(mesh), std::move(seed_results), settings, std::move(seeds), std::move(input), input_meshes,
        std::move(added_meshes), std::move(statistics),
        Navigation_item_state{.key = key->to_hex(), .made = false, .bytes = bytes.size()},
        std::move(visualization)
    );
    log_navigation->info("open_scene_gltf: navigation mesh item {} of '{}' installed: {}", key->to_hex(), asset_path, navigation.get_status_text());
}

}
