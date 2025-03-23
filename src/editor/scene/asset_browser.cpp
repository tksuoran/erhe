#include "scene/asset_browser.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/ioperation.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/geogram.hpp"
#include "parsers/gltf.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "windows/item_tree_window.hpp"

#include "erhe_file/file.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene_message_bus.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace editor {

class Scene_open_operation : public Operation
{
public:
    explicit Scene_open_operation(const std::filesystem::path& path);

    // Implements Operation
    auto describe() const -> std::string   override;
    void execute (Editor_context& context) override;
    void undo    (Editor_context& context) override;

private:
    std::filesystem::path            m_path;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Content_library> m_content_library;
};

Scene_open_operation::Scene_open_operation(const std::filesystem::path& path)
    : m_path{path}
{
}

auto Scene_open_operation::describe() const -> std::string
{
    return fmt::format("[{}] Scene_open_operation(path = {})", get_serial(), m_path.string());
}

void Scene_open_operation::execute(Editor_context& context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_scene_root) {
        m_content_library = std::make_shared<Content_library>();

        m_scene_root = std::make_shared<Scene_root>(
            context.imgui_renderer,
            context.imgui_windows,
            *context.scene_message_bus,
            &context,
            context.editor_message_bus,
            context.editor_scenes, // registers into Editor_scenes
            m_content_library,
            erhe::file::to_string(m_path.filename())
        );

        auto browser_window = m_scene_root->make_browser_window(
            *context.imgui_renderer,
            *context.imgui_windows,
            context,
            *context.editor_settings
        );
        browser_window->show_window();

        import_gltf(
            *context.graphics_instance,
            erhe::primitive::Build_info{
                .primitive_types = {
                    .fill_triangles  = true,
                    .edge_lines      = true,
                    .corner_points   = true,
                    .centroid_points = true
                },
                .buffer_info = context.mesh_memory->buffer_info
            },
            *m_scene_root.get(),
            m_path
        );
    } else {
        // Re-register
        m_scene_root->register_to_editor_scenes(*context.editor_scenes);
    }
}

void Scene_open_operation::undo(Editor_context& context)
{
    ERHE_VERIFY(m_scene_root);
    context.editor_scenes->unregister_scene_root(m_scene_root.get());
    m_scene_root->remove_browser_window();
}

//
Asset_node::Asset_node(const Asset_node&)            = default;
Asset_node& Asset_node::operator=(const Asset_node&) = default;
Asset_node::~Asset_node() noexcept                   = default;

Asset_node::Asset_node(const std::filesystem::path& path) 
    : Item{erhe::file::to_string(path.filename())}
{
    set_source_path(path);
}


auto Asset_folder::get_static_type()       -> uint64_t        { return erhe::Item_type::asset_folder; }
auto Asset_folder::get_type       () const -> uint64_t        { return get_static_type(); }
auto Asset_folder::get_type_name  () const -> std::string_view{ return static_type_name; }
Asset_folder::Asset_folder(const Asset_folder&)            = default;
Asset_folder& Asset_folder::operator=(const Asset_folder&) = default;
Asset_folder::~Asset_folder() noexcept                     = default;
Asset_folder::Asset_folder(const std::filesystem::path& path) : Item{path} {}

auto Asset_file_gltf::get_static_type()       -> uint64_t        { return erhe::Item_type::asset_file_gltf; }
auto Asset_file_gltf::get_type       () const -> uint64_t        { return get_static_type(); }
auto Asset_file_gltf::get_type_name  () const -> std::string_view{ return static_type_name; }
Asset_file_gltf::Asset_file_gltf(const Asset_file_gltf&)            = default;
Asset_file_gltf& Asset_file_gltf::operator=(const Asset_file_gltf&) = default;
Asset_file_gltf::~Asset_file_gltf() noexcept                        = default;
Asset_file_gltf::Asset_file_gltf(const std::filesystem::path& path) : Item{path} {}

auto Asset_file_geogram::get_static_type()       -> uint64_t        { return erhe::Item_type::asset_file_geogram; }
auto Asset_file_geogram::get_type       () const -> uint64_t        { return get_static_type(); }
auto Asset_file_geogram::get_type_name  () const -> std::string_view{ return static_type_name; }
Asset_file_geogram::Asset_file_geogram(const Asset_file_geogram&)            = default;
Asset_file_geogram& Asset_file_geogram::operator=(const Asset_file_geogram&) = default;
Asset_file_geogram::~Asset_file_geogram() noexcept                        = default;
Asset_file_geogram::Asset_file_geogram(const std::filesystem::path& path) : Item{path} {}

auto Asset_file_other::get_static_type()       -> uint64_t        { return erhe::Item_type::asset_file_other; }
auto Asset_file_other::get_type       () const -> uint64_t        { return get_static_type(); }
auto Asset_file_other::get_type_name  () const -> std::string_view{ return static_type_name; }
Asset_file_other::Asset_file_other(const Asset_file_other&)            = default;
Asset_file_other& Asset_file_other::operator=(const Asset_file_other&) = default;
Asset_file_other::~Asset_file_other() noexcept                         = default;
Asset_file_other::Asset_file_other(const std::filesystem::path& path) : Item{path} {}

auto Asset_browser::make_node(const std::filesystem::path& path, Asset_node* const parent) -> std::shared_ptr<Asset_node>
{
    std::error_code error_code;
    bool is_directory{false};
    const bool is_directory_test = std::filesystem::is_directory(path, error_code);
    if (!error_code) {
        is_directory = is_directory_test;
    }

    const bool is_gltf = 
        path.extension() == std::filesystem::path{".gltf"} ||
        path.extension() == std::filesystem::path{".glb"};

    const bool is_geogram = path.extension() == std::filesystem::path{".geogram"};

    std::shared_ptr<Asset_node> new_node;
    if (is_directory) {
        new_node = std::make_shared<Asset_folder>(path);
    } else if (is_gltf) {
        new_node = std::make_shared<Asset_file_gltf>(path);
    } else if (is_geogram) {
        new_node = std::make_shared<Asset_file_geogram>(path);
    } else {
        new_node = std::make_shared<Asset_file_other>(path);
    }
    new_node->enable_flag_bits(erhe::Item_flags::visible);
    if (parent) {
        new_node->set_parent(parent);
    }
    return new_node;
}

Asset_browser_window::Asset_browser_window(
    Asset_browser&               asset_browser,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              context,
    const std::string_view       window_title,
    const std::string_view       ini_label
)
    : Item_tree_window{imgui_renderer, imgui_windows, context, window_title, ini_label}
    , m_asset_browser{asset_browser}
{
}

void Asset_browser_window::imgui()
{
    if (ImGui::Button("Scan")) {
        m_asset_browser.scan();
    }
    Item_tree_window::imgui();
}

Asset_browser::Asset_browser(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : m_context{editor_context}
{
    ERHE_PROFILE_FUNCTION();
    scan();

    m_node_tree_window = std::make_shared<Asset_browser_window>(
        *this,
        imgui_renderer, 
        imgui_windows, 
        editor_context,
        "Asset Browser",
        "asset_browser"
    );
    m_node_tree_window->set_root(m_root);
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
}

void Asset_browser::scan(const std::filesystem::path& path, Asset_node* parent)
{
    log_asset_browser->trace("Scanning {}", erhe::file::to_string(path));

    std::error_code error_code;
    auto directory_iterator = std::filesystem::directory_iterator{path, error_code};
    if (error_code) {
        log_asset_browser->warn(
            "Scanning {}: directory_iterator() failed with error {} - {}",
            erhe::file::to_string(path), error_code.value(), error_code.message()
        );
        return;
    }
    for (const auto& entry : directory_iterator) {
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

        auto asset_node = make_node(entry, parent);
        if (is_directory) {
            scan(entry, asset_node.get());
        }
    }
}

void Asset_browser::scan()
{
    std::filesystem::path assets_root = std::filesystem::path("res") / std::filesystem::path("assets");

    m_root = make_node(assets_root, nullptr);
    scan(assets_root, m_root.get());
}

auto Asset_browser::try_import(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    std::string import_label = fmt::format("Import '{}'", erhe::file::to_string(gltf->get_source_path()));
    if (ImGui::MenuItem(import_label.c_str())) {
        import_gltf(
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
            gltf->get_source_path()
        );
        ImGui::CloseCurrentPopup();
        return true;
    }
    return false;
}

auto Asset_browser::try_import(const std::shared_ptr<Asset_file_geogram>& geogram) -> bool
{
    std::string import_label = fmt::format("Import '{}'", erhe::file::to_string(geogram->get_source_path()));
    if (ImGui::MenuItem(import_label.c_str())) {
        import_geogram(
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
            geogram->get_source_path()
        );
        ImGui::CloseCurrentPopup();
        return true;
    }
    return false;
}

auto Asset_browser::try_open(const std::shared_ptr<Asset_file_gltf>& gltf) -> bool
{
    std::string open_label = fmt::format("Open '{}'", erhe::file::to_string(gltf->get_source_path()));
    if (ImGui::MenuItem(open_label.c_str())) {
        //////
        m_context.operation_stack->queue(
            std::make_shared<Scene_open_operation>(gltf->get_source_path())
        );

        m_popup_node = nullptr;
        ImGui::CloseCurrentPopup();
        return true;
    }
    return false;
}

auto Asset_browser::item_callback(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    const auto geogram = std::dynamic_pointer_cast<Asset_file_geogram>(item);
    if (geogram) {
        const ImGuiID popup_id{ImGui::GetID("asset_browser_node_popup")};

        if (
            ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            m_popup_node == nullptr
        ) {
            m_popup_node = geogram.get();
            ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_MouseButtonRight);
        }

        if (m_popup_node == geogram.get()) {
            ERHE_PROFILE_SCOPE("popup");
            if (ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None)) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
                const bool begin_popup_context_item = ImGui::BeginPopupEx(
                    popup_id,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
                );
                if (begin_popup_context_item) {
                    if (try_import(geogram)) {
                        m_popup_node = nullptr;
                    }

                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
            } else {
                m_popup_node = nullptr;
            }
        }
        return false;
    }

    const auto gltf = std::dynamic_pointer_cast<Asset_file_gltf>(item);
    if (gltf) {
        if (!gltf->is_scanned) {
            gltf->contents = scan_gltf(gltf->get_source_path());
            gltf->is_scanned = true;
        }

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::BeginTooltip();
            for (const auto& line : gltf->contents) {
                ImGui::TextUnformatted(line.c_str());
            }
            ImGui::EndTooltip();
        }

        const ImGuiID popup_id{ImGui::GetID("asset_browser_node_popup")};

        if (
            ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            m_popup_node == nullptr
        ) {
            m_popup_node = gltf.get();
            ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_MouseButtonRight);
        }

        if (m_popup_node == gltf.get()) {
            ERHE_PROFILE_SCOPE("popup");
            if (ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None)) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 10.0f});
                const bool begin_popup_context_item = ImGui::BeginPopupEx(
                    popup_id,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings
                );
                if (begin_popup_context_item) {
                    if (try_import(gltf) || try_open(gltf)) {
                        m_popup_node = nullptr;
                    }

                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
            } else {
                m_popup_node = nullptr;
            }
        }
    }

    return false;
}

} // namespace editor
