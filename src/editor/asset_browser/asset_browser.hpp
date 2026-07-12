#pragma once

#include "app_message.hpp"

#include "windows/item_tree_window.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class App_message_bus;
class Scene_root;

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
};

// Lazily populate contents + bounding_box (scan_gltf); no-op when already
// scanned. Shared by the asset browser tooltip and the viewport drop target.
void ensure_scanned(Asset_file_gltf& gltf);

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

// An erhe scene directory bundle (#241): a directory whose name ends in
// .erhescene, containing scene.json and its referenced assets. Shown as a single
// leaf asset (not descended into) that can be loaded.
class Asset_file_scene : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_scene>
{
public:
    explicit Asset_file_scene(const Asset_file_scene& src);
    Asset_file_scene& operator=(const Asset_file_scene& src);
    ~Asset_file_scene() noexcept override;

    explicit Asset_file_scene(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_scene"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::asset_file_scene; }
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

    auto make_node    (const std::filesystem::path& path, Asset_node* parent) -> std::shared_ptr<Asset_node>;
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

    auto try_load  (const std::shared_ptr<Asset_file_scene>& scene) -> bool;

    // Adds "Copy path to clipboard" / "Copy relative path to clipboard" context
    // menu items for any asset that has a source path (folders and all file-based
    // assets: gltf/glb, geogram, scene bundles, other).
    void add_copy_path_menu_items(const std::shared_ptr<erhe::Item_base>& item, bool& close);

    App_context& m_context;

    std::shared_ptr<Asset_node>           m_root;
    std::shared_ptr<Asset_browser_window> m_node_tree_window;

    erhe::message_bus::Subscription<Scene_saved_message> m_scene_saved_subscription;
};

}
