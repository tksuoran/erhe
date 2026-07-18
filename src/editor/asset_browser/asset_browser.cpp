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
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_open_operation.hpp"
#include "parsers/geogram.hpp"
#include "parsers/gltf.hpp"
#include "prefabs/prefab_library.hpp"
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

#include <fmt/format.h>
#include <imgui/imgui.h>

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
    if (ImGui::Button("Scan")) {
        m_asset_browser.scan();
    }
    Item_tree_window::imgui();
}

Asset_browser::Asset_browser(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    App_message_bus&             app_message_bus
)
    : m_context{context}
{
    ERHE_PROFILE_FUNCTION();

    // A freshly saved scene file (#256) must appear in the browser without a
    // manual Scan; rescan whenever a scene is saved to disk.
    m_scene_saved_subscription = app_message_bus.scene_saved.subscribe(
        [this](Scene_saved_message&) {
            scan();
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
                ensure_scanned(*gltf);
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
            // Copy-path items for every asset that has a source path: folders and
            // all file-based assets (gltf/glb, geogram, other).
            add_copy_path_menu_items(item, close);
        }
    );
    scan();
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
    const std::filesystem::path editor_root = std::filesystem::path{"res"} / std::filesystem::path{"editor"};
    const std::filesystem::path assets_root = editor_root / std::filesystem::path{"assets"};
    const std::filesystem::path scenes_root = editor_root / std::filesystem::path{"scenes"};

    // Ensure the scenes directory exists so a fresh checkout does not log a scan
    // warning and so saved scene files have a home (#241).
    static_cast<void>(erhe::file::ensure_directory_exists(scenes_root));

    // Synthetic root under which both the read-only glTF/geogram assets and the
    // saved scene files are shown.
    m_root = make_node(editor_root, nullptr);

    std::shared_ptr<Asset_node> assets_node = make_node(assets_root, m_root.get());
    scan(assets_root, assets_node.get());

    std::shared_ptr<Asset_node> scenes_node = make_node(scenes_root, m_root.get());
    scan(scenes_root, scenes_node.get());

    m_node_tree_window->set_root(m_root);
}

void ensure_scanned(Asset_file_gltf& gltf)
{
    if (gltf.is_scanned) {
        return;
    }
    const std::filesystem::path* source_path = gltf.get_source_path();
    if (source_path == nullptr) {
        return;
    }
    Gltf_scan_summary summary = scan_gltf(*source_path);
    gltf.contents        = std::move(summary.contents);
    gltf.extensions_used = std::move(summary.extensions_used);
    gltf.bounding_box    = summary.bounding_box;
    gltf.material_names  = std::move(summary.material_names);
    gltf.material_uids   = std::move(summary.material_uids);
    gltf.is_scanned      = true;
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
        const std::shared_ptr<Prefab> prefab = m_context.prefab_library->get_or_load(*gltf->get_source_path());
        if (prefab) {
            instantiate_prefab(m_context, prefab, *scene_root, glm::mat4{1.0f});
        }
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
    const std::shared_ptr<Asset_file_gltf> gltf = std::dynamic_pointer_cast<Asset_file_gltf>(item);
    if (!gltf) {
        return false;
    }
    ensure_scanned(*gltf);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        ImGui::BeginTooltip();
        for (const std::string& line : gltf->contents) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndTooltip();
    }
    return false;
}

}
