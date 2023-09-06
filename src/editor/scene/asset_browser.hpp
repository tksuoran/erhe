#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_item/hierarchy.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;
class Item_tree_window;

class Asset_node
    : public erhe::Item<erhe::Item_base, erhe::Hierarchy, Asset_node>
{
public:
    explicit Asset_node(const Asset_node&);
    Asset_node& operator=(const Asset_node&);
    ~Asset_node() noexcept override;

    Asset_node(const std::filesystem::path& path);

    glm::vec2 icon      {0.0f, 0.0f};
    glm::vec4 icon_color{1.0f, 1.0f, 1.0f, 1.0f};
};

class Asset_folder
    : public erhe::Item<erhe::Item_base, Asset_node, Asset_folder>
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

class Asset_file_gltf
    : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_gltf>
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

class Asset_file_other
    : public erhe::Item<erhe::Item_base, Asset_node, Asset_file_other>
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

class Asset_browser
{
public:
    Asset_browser(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

private:
    void scan();
    void scan(const std::filesystem::path& path, Asset_node* parent);

    auto make_node    (const std::filesystem::path& path, Asset_node* parent) -> std::shared_ptr<Asset_node>;
    auto item_callback(const std::shared_ptr<erhe::Item_base>& item) -> bool;

    Editor_context& m_context;
    Asset_node*     m_popup_node{nullptr};

    std::shared_ptr<Asset_node>       m_root;
    std::shared_ptr<Item_tree_window> m_node_tree_window;
};

} // namespace editor
