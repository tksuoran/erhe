#pragma once

#include "app_message.hpp"

#include "windows/item_tree_window.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <functional>
#include <optional>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

// An asynchronous glTF scan in flight (doc/async-asset-loading-plan.md
// step 8). scan_gltf is a whole-file read plus a full JSON parse, so it must
// not run inside ImGui iteration - hovering a large .glb in the asset browser
// used to freeze the editor for as long as the read took.
class Gltf_scan_request
{
public:
    std::atomic<bool>        finished{false};
    std::vector<std::string> contents;
    std::vector<std::string> extensions_used;
    std::optional<erhe::math::Aabb> bounding_box;
    std::vector<std::string> material_names;
    std::vector<std::string> material_uids;
};

class Asset_node
    : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Asset_node>
{
public:
    explicit Asset_node(const Asset_node&);
    Asset_node& operator=(const Asset_node&);
    ~Asset_node() noexcept override;
    explicit Asset_node(const std::filesystem::path& path);

    static constexpr std::string_view static_type_name{"Asset_node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return 0; }
};

class Asset_folder : public erhe::Item<erhe::Item_base, Asset_node, Asset_folder>
{
public:
    explicit Asset_folder(const Asset_folder&);
    Asset_folder& operator=(const Asset_folder&);
    ~Asset_folder() noexcept override;

    explicit Asset_folder(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_folder"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_folder; }
};

class Asset_file_gltf : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_gltf>
{
public:
    explicit Asset_file_gltf(const Asset_file_gltf& src);
    Asset_file_gltf& operator=(const Asset_file_gltf& src);
    ~Asset_file_gltf() noexcept override;

    explicit Asset_file_gltf(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_gltf"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_gltf; }

    bool                            is_scanned{false};
    std::vector<std::string>        contents;
    // Structured extensionsUsed from the file's asset; filled by
    // ensure_scanned(). The asset browser branches on ERHE_scene
    // (is_erhe_scene): an erhe-authored scene file loads as a full scene,
    // any other glTF keeps the import-as-asset flow.
    std::vector<std::string>        extensions_used;
    // Combined default-scene AABB from the file's accessor bounds (JSON
    // only, no buffer data); filled by ensure_scanned(). Used for the
    // viewport drag-and-drop preview box and bottom-snap placement.
    std::optional<erhe::math::Aabb> bounding_box;
    // Scanned material names with their glTF 2.1 uids (parallel vectors;
    // empty uid when the file declares none), for the R7 "Reference
    // Material into Scene" context menu on scanned sub-assets.
    std::vector<std::string>        material_names;
    std::vector<std::string>        material_uids;

    // Non-null while an asynchronous scan is in flight. Callers that need the
    // scan results this frame should check is_scanned first and show a
    // placeholder otherwise.
    std::shared_ptr<Gltf_scan_request> scan_request;
};

// Lazily populate contents + bounding_box, by scanning on a WORKER: the first
// call spawns the scan and returns immediately with is_scanned still false;
// a later call picks the result up. No-op once scanned. Shared by the asset
// browser tooltip / context menu and the viewport drop target.
void ensure_scanned(App_context& context, Asset_file_gltf& gltf);

// Blocking variant, for the few callers that genuinely cannot proceed without
// the scan (a drag-and-drop drop that must decide right now).
void ensure_scanned_blocking(Asset_file_gltf& gltf);

class Asset_file_geogram : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_geogram>
{
public:
    explicit Asset_file_geogram(const Asset_file_geogram& src);
    Asset_file_geogram& operator=(const Asset_file_geogram& src);
    ~Asset_file_geogram() noexcept override;

    explicit Asset_file_geogram(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_geogram"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_geogram; }

    std::vector<std::string> contents;
};

// A USD file (.usd / .usda / .usdc / .usdz - see is_usd_file_extension). Its
// own item type so the browser can offer Import on it and so a drag payload
// can be recognized by type name, the way Asset_file_gltf is.
class Asset_file_usd : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_usd>
{
public:
    explicit Asset_file_usd(const Asset_file_usd& src);
    Asset_file_usd& operator=(const Asset_file_usd& src);
    ~Asset_file_usd() noexcept override;

    explicit Asset_file_usd(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_usd"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_usd; }
};

// An image file the editor can decode (PNG / JPEG / KTX2 / DDS - see
// is_texture_file_extension). Its own item type so the browser can offer the
// texture verbs on it and so a drag payload can be recognized by type name.
class Asset_file_texture : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_texture>
{
public:
    explicit Asset_file_texture(const Asset_file_texture& src);
    Asset_file_texture& operator=(const Asset_file_texture& src);
    ~Asset_file_texture() noexcept override;

    explicit Asset_file_texture(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_texture"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_texture; }
};

class Asset_file_other : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_other>
{
public:
    explicit Asset_file_other(const Asset_file_other& src);
    Asset_file_other& operator=(const Asset_file_other& src);
    ~Asset_file_other() noexcept override;

    explicit Asset_file_other(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_other"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_other; }
};

class Asset_browser;

class Asset_browser_window : public Item_tree_window
{
public:
    Asset_browser_window(
        Asset_browser&               asset_browser,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        std::string_view             window_title,
        std::string_view             ini_label
    );

    void imgui() override;

private:
    Asset_browser& m_asset_browser;
};

class Asset_browser
{
public:
    Asset_browser(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 context,
        App_message_bus&             app_message_bus
    );

    void scan();

private:
    void scan(const std::filesystem::path& path, Asset_node* parent);

    // Change-driven refresh for one written file (#256): the freshly saved
    // scene file must appear in the browser without a manual Scan, and a full
    // scan() of both asset roots is far too slow to do on every save. Adds the
    // one node when the file is new, replaces it when it already had one (the
    // node caches scanned glTF contents, which the write invalidates), and
    // falls back to scan() only when the containing directory has no node yet.
    // A path outside the browser's roots refreshes nothing.
    void refresh_file(const std::filesystem::path& path);

    // Key under which a node is registered in m_nodes_by_path: the path made
    // absolute against the working directory the scan roots are relative to,
    // lexically normalized, with forward slashes. Both a repo-relative and an
    // absolute saved path map onto the same key.
    [[nodiscard]] auto make_path_key(const std::filesystem::path& path) const -> std::string;
    [[nodiscard]] auto find_node    (const std::string& path_key) const -> std::shared_ptr<Asset_node>;

    // Creates the node for one directory entry, registers it in
    // m_nodes_by_path and attaches it to parent. position selects the sibling
    // slot; an empty position appends, which is what a scan does.
    auto make_node    (const std::filesystem::path& path, Asset_node* parent, std::optional<std::size_t> position = {}) -> std::shared_ptr<Asset_node>;
    auto item_callback(const std::shared_ptr<erhe::Item_base>& item) -> bool;

    // Scene that imports go into: the last hovered viewport's scene, falling
    // back to the only open scene. Null when no scene is open.
    [[nodiscard]] auto get_target_scene_root() -> std::shared_ptr<Scene_root>;

    auto try_import     (const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;
    // Instantiate the glTF as a prefab: parsed once via Prefab_library, the
    // inserted subtree is a clone referencing the source file (import copies
    // the content in instead).
    auto try_instantiate(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;
    auto try_open       (const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;
    // Load an erhe-authored glTF scene file (ERHE_scene in extensionsUsed)
    // as a full scene via the File > Load Scene message path.
    auto try_load       (const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;

    auto try_import(const std::shared_ptr<Asset_file_geogram>& geogram) -> bool;

    auto try_import(const std::shared_ptr<Asset_file_usd>& usd) -> bool;
    auto try_load  (const std::shared_ptr<Asset_file_usd>& usd) -> bool;

    // "Import to content library texture": one menu item when a single scene
    // is open, a submenu of scenes to choose the target content library from
    // when several are.
    void add_import_texture_menu_items(
        const std::shared_ptr<Asset_file_texture>& texture,
        std::vector<std::function<void()>>&        deferred_operations,
        bool&                                      close
    );

    // Adds "Copy path to clipboard" / "Copy relative path to clipboard" context
    // menu items for any asset that has a source path (folders and all file-based
    // assets: gltf/glb, geogram, other).
    void add_copy_path_menu_items(const std::shared_ptr<erhe::Item_base>& item, bool& close);

    // R7 reference-into-scene: a "Reference Material into Scene" submenu
    // listing the file's scanned materials (per open scene when several are
    // open); each entry lists the material in the scene's library as a
    // reference entry (undoable attach) via reference_material_into_scene.
    void add_reference_material_menu_items(
        Asset_file_gltf&                    gltf,
        std::vector<std::function<void()>>& deferred_operations,
        bool&                               close
    );

    App_context& m_context;

    std::shared_ptr<Asset_node>           m_root;
    std::shared_ptr<Asset_browser_window> m_node_tree_window;

    // Working directory the (repo-relative) scan roots are resolved against,
    // sampled once per scan() so the path keys of one tree are all built alike.
    std::filesystem::path m_working_directory;
    // Path key of the synthetic root (res/editor): a saved file outside it is
    // not shown by the browser and needs no refresh.
    std::string           m_root_path_key;
    // Every node of the current tree by path key, so one saved file is found
    // without walking the tree. Weak, so a replaced or removed node dies.
    std::map<std::string, std::weak_ptr<Asset_node>> m_nodes_by_path;

    erhe::message_bus::Subscription<Scene_saved_message> m_scene_saved_subscription;
};

}
