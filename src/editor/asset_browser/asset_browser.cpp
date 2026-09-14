#include "asset_browser/asset_browser.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_message.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "assets/asset_key.hpp"
#include "assets/asset_workflow.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "graphics/texture_file_loader.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_open_operation.hpp"
#include "parsers/geogram.hpp"
#include "parsers/gltf.hpp"
#include "parsers/usd.hpp"
#include "prefabs/prefab_library.hpp"
#include <taskflow/taskflow.hpp>

#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_file/file.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_task/task.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <chrono>
#include <functional>

namespace editor {

Asset_node::Asset_node(const Asset_node&)            = default;
Asset_node& Asset_node::operator=(const Asset_node&) = default;
Asset_node::~Asset_node() noexcept                   = default;

Asset_node::Asset_node(const std::filesystem::path& path) 
    : Item{erhe::file::to_string(path.filename())}
{
    set_source_path(path);
}


Asset_folder::Asset_folder(const Asset_folder&)            = default;
Asset_folder& Asset_folder::operator=(const Asset_folder&) = default;
Asset_folder::~Asset_folder() noexcept                     = default;
Asset_folder::Asset_folder(const std::filesystem::path& path) : Item{path} {}

Asset_file_gltf::Asset_file_gltf(const Asset_file_gltf&)            = default;
Asset_file_gltf& Asset_file_gltf::operator=(const Asset_file_gltf&) = default;
Asset_file_gltf::~Asset_file_gltf() noexcept                        = default;
Asset_file_gltf::Asset_file_gltf(const std::filesystem::path& path) : Item{path} {}

Asset_file_geogram::Asset_file_geogram(const Asset_file_geogram&)            = default;
Asset_file_geogram& Asset_file_geogram::operator=(const Asset_file_geogram&) = default;
Asset_file_geogram::~Asset_file_geogram() noexcept                        = default;
Asset_file_geogram::Asset_file_geogram(const std::filesystem::path& path) : Item{path} {}

Asset_file_usd::Asset_file_usd(const Asset_file_usd&)            = default;
Asset_file_usd& Asset_file_usd::operator=(const Asset_file_usd&) = default;
Asset_file_usd::~Asset_file_usd() noexcept                       = default;
Asset_file_usd::Asset_file_usd(const std::filesystem::path& path) : Item{path} {}

Asset_file_texture::Asset_file_texture(const Asset_file_texture&)            = default;
Asset_file_texture& Asset_file_texture::operator=(const Asset_file_texture&) = default;
Asset_file_texture::~Asset_file_texture() noexcept                           = default;
Asset_file_texture::Asset_file_texture(const std::filesystem::path& path) : Item{path} {}

Asset_file_other::Asset_file_other(const Asset_file_other&)            = default;
Asset_file_other& Asset_file_other::operator=(const Asset_file_other&) = default;
Asset_file_other::~Asset_file_other() noexcept                         = default;
Asset_file_other::Asset_file_other(const std::filesystem::path& path) : Item{path} {}

namespace {

// The path key one node is registered under: the path made absolute against
// the working directory the scan roots are relative to, lexically normalized,
// with forward slashes.
[[nodiscard]] auto make_path_key(const std::filesystem::path& working_directory, const std::filesystem::path& path) -> std::string
{
    return (working_directory / path).lexically_normal().generic_string();
}

[[nodiscard]] auto make_path_key(const Asset_tree& tree, const std::filesystem::path& path) -> std::string
{
    return make_path_key(tree.working_directory, path);
}

// Classification of a non-directory entry, by extension alone.
[[nodiscard]] auto classify_file(const std::filesystem::path& path) -> Asset_node_kind
{
    const bool is_gltf =
        path.extension() == std::filesystem::path{".gltf"} ||
        path.extension() == std::filesystem::path{".glb"};
    if (is_gltf) {
        return Asset_node_kind::gltf;
    }
    if (path.extension() == std::filesystem::path{".geogram"}) {
        return Asset_node_kind::geogram;
    }
    if (is_usd_file_extension(path)) {
        return Asset_node_kind::usd;
    }
    if (is_texture_file_extension(path)) {
        return Asset_node_kind::texture;
    }
    return Asset_node_kind::other;
}

// Classification with one filesystem call, for the single-file refresh: the
// walk knows already which entries are directories and pays no second stat.
[[nodiscard]] auto classify_path(const std::filesystem::path& path) -> Asset_node_kind
{
    std::error_code error_code;
    const bool is_directory_test = std::filesystem::is_directory(path, error_code);
    if (!error_code && is_directory_test) {
        return Asset_node_kind::folder;
    }
    return classify_file(path);
}

// The node construction a walk publication and a single-file refresh share.
// Written against an Asset_tree rather than the browser's members, and it makes
// no filesystem call: the kind is decided before it is reached (R4).
auto make_node(
    Asset_tree&                      tree,
    const std::filesystem::path&     path,
    const Asset_node_kind            kind,
    Asset_node* const                parent,
    const std::optional<std::size_t> position
) -> std::shared_ptr<Asset_node>
{
    std::shared_ptr<Asset_node> new_node;
    switch (kind) {
        case Asset_node_kind::folder:  new_node = std::make_shared<Asset_folder>      (path); break;
        case Asset_node_kind::gltf:    new_node = std::make_shared<Asset_file_gltf>   (path); break;
        case Asset_node_kind::geogram: new_node = std::make_shared<Asset_file_geogram>(path); break;
        case Asset_node_kind::usd:     new_node = std::make_shared<Asset_file_usd>    (path); break;
        case Asset_node_kind::texture: new_node = std::make_shared<Asset_file_texture>(path); break;
        case Asset_node_kind::other:
        default:                       new_node = std::make_shared<Asset_file_other>  (path); break;
    }
    new_node->show();
    tree.nodes_by_path[make_path_key(tree, path)] = new_node;
    if (parent) {
        if (position.has_value()) {
            new_node->set_parent(parent, position.value());
        } else {
            new_node->set_parent(parent);
        }
    }
    return new_node;
}

// The whole walk of both asset roots. Runs on an executor worker: it reads the
// filesystem, classifies what it finds and publishes ordered entry batches; it
// holds no node and touches nothing the main thread reads (R1).
class Asset_walk
{
public:
    explicit Asset_walk(Asset_scan_request& request)
        : m_request{request}
    {
    }

    void run();

private:
    void walk_directory(const std::filesystem::path& path, const std::string& parent_path_key);

    // Records one found entry and publishes the batch when the interval is up.
    // Returns the entry's path key, which is the parent key of its children.
    auto add_entry(const std::filesystem::path& path, const std::string& parent_path_key, Asset_node_kind kind) -> std::string;

    void publish();

    static constexpr std::chrono::milliseconds c_publish_interval{50};

    Asset_scan_request&                         m_request;
    std::filesystem::path                       m_working_directory;
    std::vector<Asset_scan_entry>               m_batch;
    std::size_t                                 m_published_entry_count{0};
    std::chrono::steady_clock::time_point       m_start_time{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point       m_last_publish_time{m_start_time};
};

auto Asset_walk::add_entry(
    const std::filesystem::path& path,
    const std::string&           parent_path_key,
    const Asset_node_kind        kind
) -> std::string
{
    std::string path_key = make_path_key(m_working_directory, path);
    m_batch.push_back(Asset_scan_entry{.path = path, .parent_path_key = parent_path_key, .kind = kind});
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - m_last_publish_time >= c_publish_interval) {
        publish();
    }
    return path_key;
}

void Asset_walk::publish()
{
    const std::size_t batch_size = m_batch.size();
    {
        std::lock_guard<std::mutex> lock{m_request.mutex};
        m_request.published_entries.insert(
            m_request.published_entries.end(),
            std::make_move_iterator(m_batch.begin()),
            std::make_move_iterator(m_batch.end())
        );
    }
    m_batch.clear(); // capacity kept
    m_published_entry_count += batch_size;
    m_last_publish_time = std::chrono::steady_clock::now();
    const std::chrono::duration<float, std::milli> elapsed = m_last_publish_time - m_start_time;
    log_asset_browser->info(
        "Asset browser: walk published {} entries ({} total, {:.2f} ms)",
        batch_size, m_published_entry_count, elapsed.count()
    );
}

void Asset_walk::walk_directory(const std::filesystem::path& path, const std::string& parent_path_key)
{
    log_asset_browser->trace("Scanning {}", erhe::file::to_string(path));

    std::error_code error_code;
    std::filesystem::directory_iterator directory_iterator{path, error_code};
    if (error_code) {
        log_asset_browser->warn(
            "Scanning {}: directory_iterator() failed with error {} - {}",
            erhe::file::to_string(path), error_code.value(), error_code.message()
        );
        return;
    }
    for (const std::filesystem::directory_entry& entry : directory_iterator) {
        const bool is_directory = std::filesystem::is_directory(entry, error_code);
        if (error_code) {
            log_asset_browser->warn(
                "Scanning {}: is_directory() failed with error {} - {}",
                erhe::file::to_string(path), error_code.value(), error_code.message()
            );
            continue;
        }

        const bool is_regular_file = std::filesystem::is_regular_file(entry, error_code);
        if (error_code) {
            log_asset_browser->warn(
                "Scanning {}: is_regular_file() failed with error {} - {}",
                erhe::file::to_string(path), error_code.value(), error_code.message()
            );
            continue;
        }
        if (!is_directory && !is_regular_file) {
            log_asset_browser->warn(
                "Scanning {}: is neither regular file nor directory",
                erhe::file::to_string(path)
            );
            continue;
        }

        const std::filesystem::path& entry_path = entry.path();
        const Asset_node_kind kind = is_directory ? Asset_node_kind::folder : classify_file(entry_path);
        const std::string entry_path_key = add_entry(entry_path, parent_path_key, kind);
        if (is_directory) {
            walk_directory(entry_path, entry_path_key);
        }
    }
}

void Asset_walk::run()
{
    ERHE_PROFILE_SCOPE("editor::Asset_browser scan walk");

    std::error_code working_directory_error_code;
    const std::filesystem::path working_directory = std::filesystem::current_path(working_directory_error_code);
    m_working_directory = working_directory_error_code ? std::filesystem::path{} : working_directory;
    {
        // Published before the first batch, so the main thread builds the new
        // tree's path keys the way the walk built the parent keys it carries.
        std::lock_guard<std::mutex> lock{m_request.mutex};
        m_request.working_directory = m_working_directory;
    }

    const std::filesystem::path editor_root = std::filesystem::path{"res"} / std::filesystem::path{"editor"};
    const std::filesystem::path assets_root = editor_root / std::filesystem::path{"assets"};
    const std::filesystem::path scenes_root = editor_root / std::filesystem::path{"scenes"};

    // Ensure the scenes directory exists so a fresh checkout does not log a scan
    // warning and so saved scene files have a home (#241).
    static_cast<void>(erhe::file::ensure_directory_exists(scenes_root));

    // Synthetic root under which both the read-only glTF/geogram assets and the
    // saved scene files are shown. Its empty parent key names it as the root.
    const std::string root_path_key = add_entry(editor_root, std::string{}, Asset_node_kind::folder);

    const std::string assets_path_key = add_entry(assets_root, root_path_key, Asset_node_kind::folder);
    walk_directory(assets_root, assets_path_key);

    const std::string scenes_path_key = add_entry(scenes_root, root_path_key, Asset_node_kind::folder);
    walk_directory(scenes_root, scenes_path_key);

    publish();
    const std::chrono::duration<float, std::milli> elapsed = std::chrono::steady_clock::now() - m_start_time;
    log_asset_browser->info(
        "Asset browser: walk finished, {} entries, {:.2f} ms",
        m_published_entry_count, elapsed.count()
    );
}

} // anonymous namespace

auto Asset_browser::make_path_key(const std::filesystem::path& path) const -> std::string
{
    return editor::make_path_key(m_tree, path);
}

auto Asset_browser::find_node(const std::string& path_key) const -> std::shared_ptr<Asset_node>
{
    const std::map<std::string, std::weak_ptr<Asset_node>>::const_iterator i = m_tree.nodes_by_path.find(path_key);
    if (i == m_tree.nodes_by_path.end()) {
        return {};
    }
    return i->second.lock();
}

auto Asset_browser::make_node(
    const std::filesystem::path&     path,
    const Asset_node_kind            kind,
    Asset_node* const                parent,
    const std::optional<std::size_t> position
) -> std::shared_ptr<Asset_node>
{
    return editor::make_node(m_tree, path, kind, parent, position);
}

Asset_browser_window::Asset_browser_window(
    Asset_browser&               asset_browser,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    const std::string_view       window_title,
    const std::string_view       ini_label
)
    : Item_tree_window{imgui_renderer, imgui_windows, context, window_title, ini_label}
    , m_asset_browser{asset_browser}
{
}

void Asset_browser_window::imgui()
{
    m_asset_browser.apply_scan_progress();
    if (ImGui::Button("Scan")) {
        m_asset_browser.scan();
    }
    if (m_asset_browser.is_scan_in_flight()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Scanning...");
    }
    Item_tree_window::imgui();
}

Asset_browser::Asset_browser(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    App_message_bus&             app_message_bus,
    tf::Executor&                executor
)
    : m_context {context}
    , m_executor{executor}
{
    ERHE_PROFILE_FUNCTION();

    // A freshly saved scene file (#256) must appear in the browser without a
    // manual Scan. The save is the change site, and it names the written file,
    // so only that file's node is refreshed - a full scan() of both asset
    // roots takes seconds.
    m_scene_saved_subscription = app_message_bus.scene_saved.subscribe(
        [this](Scene_saved_message& message) {
            refresh_file(message.path);
        }
    );

    m_node_tree_window = std::make_shared<Asset_browser_window>(
        *this,
        imgui_renderer, 
        imgui_windows, 
        context,
        "Asset Browser",
        "asset_browser"
    );
    m_node_tree_window->set_item_filter(
        erhe::Item_filter{
            .require_all_bits_set           = 0,
            .require_at_least_one_bit_set   = 0,
            .require_all_bits_clear         = 0,
            .require_at_least_one_bit_clear = 0
        }
    );
    m_node_tree_window->set_item_callback(
        [&](const std::shared_ptr<erhe::Item_base>& item) -> bool {
            return item_callback(item);
        }
    );
    m_node_tree_window->add_item_context_menu_callback(
        [this](
            const std::shared_ptr<erhe::Item_base>& item,
            std::vector<std::function<void()>>&     deferred_operations,
            bool&                                   close
        ) {
            // Primary type-specific actions.
            const std::shared_ptr<Asset_file_geogram> geogram = std::dynamic_pointer_cast<Asset_file_geogram>(item);
            if (geogram) {
                if (try_import(geogram)) {
                    close = true;
                }
            }
            const std::shared_ptr<Asset_file_gltf> gltf = std::dynamic_pointer_cast<Asset_file_gltf>(item);
            if (gltf) {
                // Open-vs-import branch (doc/gltf-scene-roundtrip-plan.md
                // phase 4): an erhe-authored scene file (ERHE_scene in
                // extensionsUsed) loads as a full scene; any other glTF keeps
                // the import-as-asset flow (Load = foreign glTF as new scene).
                ensure_scanned(m_context, *gltf);
                if (!gltf->is_scanned) {
                    // Scan still on a worker; the open-vs-import branch below
                    // needs its result, so offer nothing this frame rather
                    // than the wrong thing. The menu re-evaluates every frame.
                    ImGui::TextUnformatted("Scanning...");
                } else {
                const bool erhe_scene = is_erhe_scene(gltf->extensions_used);
                if (erhe_scene && try_load(gltf)) {
                    close = true;
                }
                if (try_import(gltf) || try_instantiate(gltf)) {
                    close = true;
                }
                if (!erhe_scene && try_open(gltf)) {
                    close = true;
                }
                add_reference_material_menu_items(*gltf, deferred_operations, close);
                }
            }
            const std::shared_ptr<Asset_file_usd> usd = std::dynamic_pointer_cast<Asset_file_usd>(item);
            if (usd) {
                if (try_load(usd) || try_import(usd)) {
                    close = true;
                }
            }
            const std::shared_ptr<Asset_file_texture> texture = std::dynamic_pointer_cast<Asset_file_texture>(item);
            if (texture) {
                add_import_texture_menu_items(texture, deferred_operations, close);
            }
            // Copy-path items for every asset that has a source path: folders and
            // all file-based assets (gltf/glb, geogram, other).
            add_copy_path_menu_items(item, close);
        }
    );
    scan();
}

void Asset_browser::scan()
{
    if (m_scan_request) {
        // A walk started moments ago reads the same directories this one would,
        // so it is left to finish and its tree is the one that lands.
        log_asset_browser->info("Asset browser: a scan is already in flight");
        return;
    }
    std::shared_ptr<Asset_scan_request> request = std::make_shared<Asset_scan_request>();
    m_scan_request = request;
    erhe::task::spawn(
        m_executor,
        [request]() {
            Asset_walk walk{*request};
            walk.run();
            request->finished.store(true, std::memory_order_release);
        }
    );
}

void Asset_browser::apply_scan_progress()
{
    if (!m_scan_request) {
        return;
    }
    Asset_scan_request& request = *m_scan_request;

    // Read before draining: the worker sets the flag after its last batch is
    // in, so a flag seen set means everything the walk found is now published.
    const bool finished = request.finished.load(std::memory_order_acquire);

    std::filesystem::path working_directory;
    {
        std::lock_guard<std::mutex> lock{request.mutex};
        working_directory = request.working_directory;
        m_scan_entry_scratch.clear(); // capacity kept
        m_scan_entry_scratch.insert(
            m_scan_entry_scratch.end(),
            std::make_move_iterator(request.published_entries.begin()),
            std::make_move_iterator(request.published_entries.end())
        );
        request.published_entries.clear();
    }

    for (const Asset_scan_entry& entry : m_scan_entry_scratch) {
        if (entry.parent_path_key.empty()) {
            // The walk's synthetic root: the new tree replaces the shown one
            // here and grows in place from now on (D3).
            m_tree = Asset_tree{};
            m_tree.working_directory = working_directory;
            m_tree.root_path_key     = editor::make_path_key(m_tree, entry.path);
            m_tree.root              = editor::make_node(m_tree, entry.path, entry.kind, nullptr, {});
            m_node_tree_window->set_root(m_tree.root);
            continue;
        }
        const std::shared_ptr<Asset_node> parent_node = find_node(entry.parent_path_key);
        if (!parent_node) {
            // A parent always precedes its children in the walk's order (R2),
            // so this is a defect rather than a state to tolerate.
            log_asset_browser->warn(
                "Asset browser: scan entry '{}' has no node for its parent '{}'",
                erhe::file::to_string(entry.path), entry.parent_path_key
            );
            continue;
        }
        editor::make_node(m_tree, entry.path, entry.kind, parent_node.get(), {});
    }
    m_scan_entry_scratch.clear(); // capacity kept

    if (!finished) {
        return;
    }
    m_scan_request.reset();

    std::vector<std::filesystem::path> pending_refresh_paths = std::move(m_pending_refresh_paths);
    m_pending_refresh_paths.clear();
    for (const std::filesystem::path& path : pending_refresh_paths) {
        // Through refresh_file(), so a path is re-queued when one of these
        // refreshes starts a new walk.
        refresh_file(path);
    }
}

void Asset_browser::refresh_file(const std::filesystem::path& path)
{
    apply_scan_progress();
    if (m_scan_request) {
        // A walk in flight replaces the whole tree, including anything a
        // refresh would add to the current one; refresh against the tree that
        // lands instead.
        m_pending_refresh_paths.push_back(path);
        return;
    }
    refresh_file_now(path);
}

void Asset_browser::refresh_file_now(const std::filesystem::path& path)
{
    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    const std::string   path_key   = make_path_key(path);
    const std::string   parent_key = make_path_key(path.parent_path());
    std::string_view    outcome{};

    if (!m_tree.root || m_tree.root_path_key.empty() || !path_key.starts_with(m_tree.root_path_key + "/")) {
        // The browser shows res/editor only; a scene saved elsewhere has no
        // node to add or refresh.
        outcome = "outside the asset browser roots, nothing to refresh";
    } else {
        const std::shared_ptr<Asset_node> parent_node   = find_node(parent_key);
        const std::shared_ptr<Asset_node> existing_node = find_node(path_key);
        if (!parent_node) {
            // A save into a directory the tree does not have a node for (a
            // directory created since the last scan): only a scan finds it.
            log_asset_browser->info(
                "Asset browser: '{}' was saved into '{}', which has no node yet - rescanning",
                erhe::file::to_string(path), erhe::file::to_string(path.parent_path())
            );
            scan();
            outcome = "full scan submitted";
        } else if (existing_node) {
            // Replaced rather than mutated: the node caches the file's scanned
            // glTF contents, which this write invalidates, and the replacement
            // goes through the one construction path a scan uses. Removed
            // first, so the new node's name does not collide with its own
            // predecessor's.
            const std::optional<std::size_t> position = parent_node->get_index_of_child(existing_node.get());
            existing_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
            make_node(path, classify_path(path), parent_node.get(), position);
            outcome = "node replaced";
        } else {
            make_node(path, classify_path(path), parent_node.get());
            outcome = "node added";
        }
    }

    const std::chrono::duration<float, std::milli> duration = std::chrono::steady_clock::now() - start_time;
    log_asset_browser->info(
        "Asset browser refreshed '{}' after scene save: {} ({:.2f} ms)",
        erhe::file::to_string(path), outcome, duration.count()
    );
}

namespace {

void apply_scan_summary(Asset_file_gltf& gltf, Gltf_scan_summary&& summary)
{
    gltf.contents        = std::move(summary.contents);
    gltf.extensions_used = std::move(summary.extensions_used);
    gltf.bounding_box    = summary.bounding_box;
    gltf.material_names  = std::move(summary.material_names);
    gltf.material_uids   = std::move(summary.material_uids);
    gltf.is_scanned      = true;
}

} // anonymous namespace

void ensure_scanned_blocking(Asset_file_gltf& gltf)
{
    if (gltf.is_scanned) {
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if (source_path == nullptr) {
        return;
    }
    apply_scan_summary(gltf, scan_gltf(*source_path));
    gltf.scan_request.reset();
}

void ensure_scanned(App_context& context, Asset_file_gltf& gltf)
{
    if (gltf.is_scanned) {
        return;
    }
    // A scan already finished on a worker: pick it up. This runs on the main
    // thread from ImGui iteration, so nothing else touches the item.
    if (gltf.scan_request) {
        if (!gltf.scan_request->finished.load(std::memory_order_acquire)) {
            return; // still scanning; the caller shows a placeholder
        }
        Gltf_scan_request& request = *gltf.scan_request;
        gltf.contents        = std::move(request.contents);
        gltf.extensions_used = std::move(request.extensions_used);
        gltf.bounding_box    = request.bounding_box;
        gltf.material_names  = std::move(request.material_names);
        gltf.material_uids   = std::move(request.material_uids);
        gltf.is_scanned      = true;
        gltf.scan_request.reset();
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if (source_path == nullptr) {
        return;
    }
    if (context.executor == nullptr) {
        ensure_scanned_blocking(gltf);
        return;
    }
    auto request = std::make_shared<Gltf_scan_request>();
    gltf.scan_request = request;
    const std::filesystem::path path = *source_path;
    erhe::task::spawn(
        *context.executor,
        [request, path]() {
            Gltf_scan_summary summary = scan_gltf(path);
            request->contents        = std::move(summary.contents);
            request->extensions_used = std::move(summary.extensions_used);
            request->bounding_box    = summary.bounding_box;
            request->material_names  = std::move(summary.material_names);
            request->material_uids   = std::move(summary.material_uids);
            request->finished.store(true, std::memory_order_release);
        }
    );
}

auto Asset_browser::get_target_scene_root() -> std::shared_ptr<Scene_root>
{
    const std::shared_ptr<Viewport_scene_view> scene_view = m_context.scene_views->last_scene_view();
    if (scene_view) {
        std::shared_ptr<Scene_root> scene_root = scene_view->get_scene_root();
        if (scene_root) {
            return scene_root;
        }
    }
    return m_context.app_scenes->get_single_scene_root();
}

auto Asset_browser::try_import(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    const std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    std::string import_label = fmt::format("Import '{}'", erhe::file::to_string(*gltf->get_source_path()));
    if (ImGui::MenuItem(import_label.c_str(), nullptr, false, static_cast<bool>(scene_root))) {
        import_gltf(m_context, make_import_build_info(m_context), scene_root, *gltf->get_source_path());
        return true;
    }
    return false;
}

auto Asset_browser::try_import(const std::shared_ptr<Asset_file_usd>& usd) -> bool
{
    const std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    std::string import_label = fmt::format("Import '{}'", erhe::file::to_string(*usd->get_source_path()));
    if (ImGui::MenuItem(import_label.c_str(), nullptr, false, static_cast<bool>(scene_root))) {
        import_usd(m_context, make_import_build_info(m_context), scene_root, *usd->get_source_path());
        return true;
    }
    return false;
}

auto Asset_browser::try_import(const std::shared_ptr<Asset_file_geogram>& geogram) -> bool
{
    const std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    std::string import_label = fmt::format("Import '{}'", erhe::file::to_string(*geogram->get_source_path()));
    if (ImGui::MenuItem(import_label.c_str(), nullptr, false, static_cast<bool>(scene_root))) {
        import_geogram(
            erhe::primitive::Build_info{
                .primitive_types = {
                    .fill_triangles  = true,
                    .fill_triangles_expanded = true,
                    .edge_lines      = true,
                    .corner_points   = true,
                    .centroid_points = true
                },
                .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
            },
            *scene_root.get(),
            *geogram->get_source_path()
        );
        return true;
    }
    return false;
}

auto Asset_browser::try_instantiate(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    const std::shared_ptr<Scene_root> scene_root = get_target_scene_root();
    std::string instantiate_label = fmt::format("Instantiate as prefab '{}'", erhe::file::to_string(*gltf->get_source_path()));
    if (ImGui::MenuItem(instantiate_label.c_str(), nullptr, false, static_cast<bool>(scene_root))) {
        // Asynchronous: the menu closes immediately and the instance appears
        // when the template is ready (inline when it is already cached).
        const std::weak_ptr<Scene_root> weak_scene_root = scene_root;
        const std::filesystem::path     path            = *gltf->get_source_path();
        App_context&                    context         = m_context;
        m_context.prefab_library->get_or_load_async(
            path,
            [&context, weak_scene_root, path](const std::shared_ptr<Prefab>& prefab) {
                const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
                if (prefab && target) {
                    instantiate_prefab(context, prefab, *target, glm::mat4{1.0f});
                }
            }
        );
        return true;
    }
    return false;
}

auto Asset_browser::try_open(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    std::string open_label = fmt::format("Load '{}'", erhe::file::to_string(*gltf->get_source_path()));
    if (ImGui::MenuItem(open_label.c_str())) {
        m_context.operation_stack->queue(
            std::make_shared<Scene_open_operation>(*gltf->get_source_path())
        );
        return true;
    }
    return false;
}

auto Asset_browser::try_load(const std::shared_ptr<Asset_file_usd>& usd) -> bool
{
    std::string load_label = fmt::format("Load scene '{}'", erhe::file::to_string(*usd->get_source_path()));
    if (ImGui::MenuItem(load_label.c_str())) {
        // Same File > Load Scene path a glTF scene takes; the handler routes
        // a USD file to open_scene_usd (new scene root + content library +
        // browser and viewport windows, the file's prims as the scene's
        // nodes, Save Scene writing USDA back).
        m_context.app_message_bus->load_scene_file.queue_message(
            Load_scene_file_message{
                .path = *usd->get_source_path()
            }
        );
        return true;
    }
    return false;
}

auto Asset_browser::try_load(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    std::string load_label = fmt::format("Load scene '{}'", erhe::file::to_string(*gltf->get_source_path()));
    if (ImGui::MenuItem(load_label.c_str())) {
        // Reuse the exact File > Load Scene path: the message handler routes
        // an erhe-authored glTF file to open_scene_gltf (full scene: new
        // content library, browser + viewport windows, editor state applied).
        m_context.app_message_bus->load_scene_file.queue_message(
            Load_scene_file_message{
                .path = *gltf->get_source_path()
            }
        );
        return true;
    }
    return false;
}

void Asset_browser::add_import_texture_menu_items(
    const std::shared_ptr<Asset_file_texture>& texture,
    std::vector<std::function<void()>>&        deferred_operations,
    bool&                                      close
)
{
    const std::filesystem::path* source_path = texture->get_source_path();
    if ((source_path == nullptr) || (m_context.app_scenes == nullptr)) {
        return;
    }
    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    App_context* const          context = &m_context;
    const std::filesystem::path path    = *source_path;
    const auto queue_import = [context, &deferred_operations, &close, path](const std::shared_ptr<Scene_root>& scene_root) {
        deferred_operations.push_back(
            [context, scene_root, path]() {
                import_texture_into_scene(*context, scene_root, path);
            }
        );
        close = true;
    };

    if (scene_roots.empty()) {
        // Shown disabled rather than hidden, so the verb is discoverable
        // before a scene is open.
        ImGui::MenuItem("Import to content library texture", nullptr, false, false);
        return;
    }
    // One scene: a plain item targeting it. Several: a submenu to pick which
    // scene's content library receives the texture.
    if (scene_roots.size() == 1) {
        if (ImGui::MenuItem("Import to content library texture")) {
            queue_import(scene_roots.front());
        }
        return;
    }
    if (ImGui::BeginMenu("Import to content library texture")) {
        for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
            if (scene_root && ImGui::MenuItem(scene_root->get_name().c_str())) {
                queue_import(scene_root);
            }
        }
        ImGui::EndMenu();
    }
}

void Asset_browser::add_copy_path_menu_items(const std::shared_ptr<erhe::Item_base>& item, bool& close)
{
    const std::filesystem::path* source_path = item->get_source_path();
    if (source_path == nullptr) {
        return;
    }
    if (ImGui::MenuItem("Copy path to clipboard")) {
        // Absolute path (resolved against the editor working directory / repo root).
        std::error_code error_code;
        const std::filesystem::path absolute_path = std::filesystem::absolute(*source_path, error_code);
        const std::string text = erhe::file::to_string(error_code ? *source_path : absolute_path);
        ImGui::SetClipboardText(text.c_str());
        close = true;
    }
    if (ImGui::MenuItem("Copy relative path to clipboard")) {
        // Path relative to the erhe repository root (the editor working directory).
        // Assets are scanned from repo-relative roots, so the stored source path is
        // already repo-relative; emit it with forward slashes (e.g.
        // "res/editor/assets/...").
        const std::string text = source_path->generic_string();
        ImGui::SetClipboardText(text.c_str());
        close = true;
    }
}

void Asset_browser::add_reference_material_menu_items(
    Asset_file_gltf&                    gltf,
    std::vector<std::function<void()>>& deferred_operations,
    bool&                               close
)
{
    if (gltf.material_names.empty() || (m_context.app_scenes == nullptr) || (m_context.asset_manager == nullptr)) {
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if ((source_path == nullptr) || source_path->empty()) {
        return;
    }
    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    if (scene_roots.empty()) {
        return;
    }
    if (!ImGui::BeginMenu("Reference Material into Scene")) {
        return;
    }
    App_context* const context = &m_context;
    const auto add_entry = [&](const std::size_t material_index, const std::shared_ptr<Scene_root>& scene_root) {
        const Asset_key key{
            .scope = Asset_scope::file,
            .type  = Asset_type::material,
            .path  = source_path->generic_string(),
            .uid   = gltf.material_uids[material_index],
            .name  = gltf.material_names[material_index],
        };
        deferred_operations.push_back(
            [context, key, scene_root]() {
                std::string error;
                if (!reference_material_into_scene(*context, *scene_root, key, error)) {
                    log_asset_browser->warn("Reference Material into Scene failed: {}", error);
                }
            }
        );
        close = true;
    };
    for (std::size_t i = 0; i < gltf.material_names.size(); ++i) {
        const std::string label = gltf.material_names[i].empty()
            ? fmt::format("(unnamed material {})", i)
            : gltf.material_names[i];
        if (scene_roots.size() == 1) {
            if (ImGui::MenuItem(label.c_str())) {
                add_entry(i, scene_roots.front());
            }
            continue;
        }
        if (ImGui::BeginMenu(label.c_str())) {
            for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
                if (scene_root && ImGui::MenuItem(scene_root->get_name().c_str())) {
                    add_entry(i, scene_root);
                }
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndMenu();
}

auto Asset_browser::item_callback(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    const std::shared_ptr<Asset_file_texture> texture_file = std::dynamic_pointer_cast<Asset_file_texture>(item);
    if (texture_file) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (m_context.texture_file_loader != nullptr)) {
            const std::filesystem::path* source_path = texture_file->get_source_path();
            if (source_path != nullptr) {
                // Only what the user actually hovers is decoded, and the
                // decode runs on a worker: the first hovered frame shows the
                // placeholder and the image appears a frame or two later.
                // The result is cached, so re-hovering is free.
                const Texture_file_loader::Preview preview = m_context.texture_file_loader->get_preview(*source_path);
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(texture_file->get_name().c_str());
                if (preview.texture) {
                    draw_texture_preview(m_context, preview.texture, 256.0f);
                } else if (preview.pending) {
                    ImGui::TextUnformatted("Loading...");
                } else {
                    ImGui::TextUnformatted(preview.error.empty() ? "Preview not available" : preview.error.c_str());
                }
                ImGui::EndTooltip();
            }
        }
        return false;
    }

    const std::shared_ptr<Asset_file_gltf> gltf = std::dynamic_pointer_cast<Asset_file_gltf>(item);
    if (!gltf) {
        return false;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        // Only scan what the user actually hovers, and only from here on -
        // the scan runs on a worker, so the first hovered frame shows the
        // placeholder and the contents appear a frame or two later.
        ensure_scanned(m_context, *gltf);
        ImGui::BeginTooltip();
        if (!gltf->is_scanned) {
            ImGui::TextUnformatted("Scanning...");
        }
        for (const std::string& line : gltf->contents) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndTooltip();
    }
    return false;
}

}
