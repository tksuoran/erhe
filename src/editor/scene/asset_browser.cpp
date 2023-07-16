#include "scene/asset_browser.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "parsers/gltf.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/scene_builder.hpp"

#include "erhe/imgui/imgui_windows.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace editor
{

Asset_node::Asset_node() = default;

Asset_node::Asset_node(const std::filesystem::path& path, const std::size_t depth)
    : path {path}
    , depth{depth}
{
}

Asset_browser::Asset_browser(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Assets", "assets"}
    , m_context                {editor_context}
{
}

void Asset_browser::scan(const std::filesystem::path& path, Asset_node& parent)
{
    log_asset_browser->trace("Scanning {}", path.string());

    std::error_code error_code;
    auto directory_iterator = std::filesystem::directory_iterator{path, error_code};
    if (error_code) {
        log_asset_browser->warn(
            "Scanning {}: directory_iterator() failed with error {} - {}",
            path.string(), error_code.value(), error_code.message()
        );
        return;
    }
    for (const auto& entry : directory_iterator) {
        const bool is_directory = std::filesystem::is_directory(path, error_code);
        if (error_code) {
            log_asset_browser->warn(
                "Scanning {}: is_directory() failed with error {} - {}",
                path.string(), error_code.value(), error_code.message()
            );
            continue;
        }
        const bool is_regular_file = std::filesystem::is_regular_file(path, error_code);
        if (error_code) {
            log_asset_browser->warn(
                "Scanning {}: is_regular_file() failed with error {} - {}",
                path.string(), error_code.value(), error_code.message()
            );
            continue;
        }
        if (!is_directory && !is_regular_file) {
            log_asset_browser->warn(
                "Scanning {}: is neither regular file nor directory",
                path.string()
            );
            continue;
        }
        auto& asset_node = parent.children.emplace_back(entry, parent.depth + 1);
        if (is_directory) {
            scan(entry, asset_node);
        }
    }
}

void Asset_browser::scan()
{
    std::filesystem::path assets_root = std::filesystem::path("res") / std::filesystem::path("assets");

    m_root_node = Asset_node{assets_root, 0};
    scan(assets_root, m_root_node);
}

void Asset_browser::imgui(Asset_node& node)
{
    const ImGuiTreeNodeFlags parent_flags{
        ImGuiTreeNodeFlags_OpenOnArrow       |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth
    };

    auto& icons = m_context.icon_set->icons;

    std::error_code error_code;
    const bool is_directory = std::filesystem::is_directory(node.path, error_code);
    if (error_code) {
        return;
    }

    const glm::vec4 directory_color{1.0f, 0.5f, 0.0f, 1.0f};
    const glm::vec4 file_color     {0.5f, 0.5f, 0.5f, 1.0f};
    const glm::vec4 gltf_color     {0.0f, 1.0f, 0.0f, 1.0f};
    const bool      is_gltf = 
        node.path.extension() == std::filesystem::path{".gltf"} ||
        node.path.extension() == std::filesystem::path{".glb"};
    if (is_directory) {
        m_context.icon_set->get_small_rasterization().icon(icons.folder, directory_color);
        ImGui::SameLine();
        if (ImGui::TreeNodeEx(node.path.filename().string().c_str(), parent_flags)) {
            ImGui::Indent();
            for (auto& child_node : node.children) {
                imgui(child_node);
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }
    } else {
        if (is_gltf) {
            m_context.icon_set->get_small_rasterization().icon(icons.scene, gltf_color);
        } else {
            m_context.icon_set->get_small_rasterization().icon(icons.file, file_color);
        }
        ImGui::SameLine();
        bool selected = m_popup_node == &node;
        ImGui::Selectable(node.path.filename().string().c_str(), selected);
        if (is_gltf) {
            ImGuiID popup_id = ImGui::GetID("asset_browser_node_popup");
            if (
                ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                m_popup_node == nullptr
            ) {
                m_popup_node = &node;
                ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_MouseButtonRight);
            }
            if (m_popup_node == &node) {
                if (ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None)) {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
                    const bool begin_popup_context_item = ImGui::BeginPopupEx(
                        popup_id,
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
                    );
                    if (begin_popup_context_item) {
                        std::string label = fmt::format("Import '{}'", node.path.string());
                        if (ImGui::MenuItem(label.c_str())) {
                            parse_gltf(
                                *m_context.graphics_instance,
                                erhe::primitive::Build_info{
                                    .primitive_types = {
                                        .fill_triangles  = true,
                                        .edge_lines      = true,
                                        .corner_points   = true,
                                        .centroid_points = true
                                    },
                                    .buffer_info = m_context.mesh_memory->buffer_info
                                },
                                *m_context.scene_builder->get_scene_root().get(),
                                node.path
                            );

                            m_popup_node = nullptr;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopStyleVar();
                } else {
                    m_popup_node = nullptr;
                }
            }
        }
    }
}

void Asset_browser::imgui()
{
    if (ImGui::Button("Scan")) {
        scan();
    }

    imgui(m_root_node);
}

} // namespace editor
