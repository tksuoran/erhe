#pragma once

#include "windows/item_tree_window.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_item/hierarchy.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;

class Asset_node
    : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Asset_node>
{
public:
    explicit Asset_node(const Asset_node&);
    Asset_node& operator=(const Asset_node&);
    ~Asset_node() noexcept override;
    explicit Asset_node(const std::filesystem::path& path);
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
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;
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
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    bool                     is_scanned{false};
    std::vector<std::string> contents;
};

class Asset_file_geogram : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_geogram>
{
public:
    explicit Asset_file_geogram(const Asset_file_geogram& src);
    Asset_file_geogram& operator=(const Asset_file_geogram& src);
    ~Asset_file_geogram() noexcept override;

    explicit Asset_file_geogram(const std::filesystem::path& path);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Asset_file_geogram"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

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
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;
};

class Asset_browser;

class Asset_browser_window : public Item_tree_window
{
public:
    Asset_browser_window(
        Asset_browser&               asset_browser,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              context,
        const std::string_view       window_title,
        const std::string_view       ini_label
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
        Editor_context&              editor_context
    );

    void scan();

private:
    void scan(const std::filesystem::path& path, Asset_node* parent);

    auto make_node    (const std::filesystem::path& path, Asset_node* parent) -> std::shared_ptr<Asset_node>;
    auto item_callback(const std::shared_ptr<erhe::Item_base>& item) -> bool;

    auto try_import(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;
    auto try_open  (const std::shared_ptr<Asset_file_gltf>& gltf) -> bool;

    auto try_import(const std::shared_ptr<Asset_file_geogram>& geogram) -> bool;

    Editor_context& m_context;
    Asset_node*     m_popup_node{nullptr};

    std::shared_ptr<Asset_node>           m_root;
    std::shared_ptr<Asset_browser_window> m_node_tree_window;
};

} // namespace editor
