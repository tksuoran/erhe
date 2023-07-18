#pragma once

#include "erhe/imgui/imgui_window.hpp"

#include <glm/glm.hpp>

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
    Asset_node(Editor_context& context, const std::filesystem::path& path, std::size_t depth);

    bool                     is_directory{false};
    bool                     is_gltf     {false};
    bool                     is_scanned  {false};
    glm::vec2                icon        {0.0f, 0.0f};
    glm::vec4                icon_color  {1.0f, 1.0f, 1.0f, 1.0f};
    std::string              label;
    std::vector<std::string> contents;
    std::filesystem::path    path;
    std::vector<Asset_node>  children;
    std::size_t              depth;
    static int s_counter;
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
