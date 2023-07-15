#pragma once

#include "erhe/imgui/imgui_window.hpp"

#include <filesystem>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_context;

class Asset_node
{
public:
    Asset_node();
    Asset_node(const std::filesystem::path& path, std::size_t depth);

    std::filesystem::path   path;
    std::vector<Asset_node> children;
    std::size_t             depth;
};

class Asset_browser
    : public erhe::imgui::Imgui_window
{
public:
    Asset_browser(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void scan();
    void scan(const std::filesystem::path& path, Asset_node& parent);
    void imgui(Asset_node& node);

    Editor_context& m_context;
    Asset_node      m_root_node;
    Asset_node*     m_popup_node{nullptr};
};

} // namespace editor
