#include "windows/pvs_window.hpp"

#include "app_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/pvs_database.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <mutex>
#include <optional>

namespace editor {

auto find_pvs_viewport_camera(App_context& context, const Scene_root& scene_root) -> std::shared_ptr<erhe::scene::Camera>
{
    if (context.scene_views == nullptr) {
        return {};
    }
    for (const std::shared_ptr<Viewport_window>& viewport_window : context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
        if (scene_view && (scene_view->get_scene_root().get() == &scene_root) && scene_view->get_camera()) {
            return scene_view->get_camera();
        }
    }
    return {};
}

Pvs_window::Pvs_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "PVS", "pvs"}
    , m_context{app_context}
{
    // The window bakes with the conservative method at the plan's defaults
    // (doc/plans/pvs.md "Bake" 3).
    m_request.method          = erhe::scene::Pvs_bake_method::disocclusion;
    m_request.face_resolution = 1024;
    set_min_size(320.0f, 240.0f);
}

void Pvs_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Scene_root> scene_root = (m_context.selection != nullptr) ? m_context.selection->get_active_scene_root() : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        ImGui::TextUnformatted("No active scene");
        return;
    }
    ImGui::Text("Scene: %s", scene_root->get_name().c_str());
    if (ImGui::CollapsingHeader("Region", ImGuiTreeNodeFlags_DefaultOpen)) {
        region_imgui(*scene_root);
    }
    if (ImGui::CollapsingHeader("Bake parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        parameters_imgui();
    }
    if (ImGui::CollapsingHeader("Bake", ImGuiTreeNodeFlags_DefaultOpen)) {
        bake_imgui(scene_root);
    }
    if (ImGui::CollapsingHeader("Database", ImGuiTreeNodeFlags_DefaultOpen)) {
        database_imgui(*scene_root);
    }
}

void Pvs_window::region_imgui(Scene_root& scene_root)
{
    // Scene settings (doc/plans/pvs.md D2): no render phase reads them, so
    // the fields edit them in place.
    Scene_settings& settings = scene_root.get_scene_settings();
    ImGui::DragFloat3("Region min", glm::value_ptr(settings.pvs_region_min), 0.1f);
    ImGui::DragFloat3("Region max", glm::value_ptr(settings.pvs_region_max), 0.1f);
    ImGui::DragFloat ("Cell size",  &settings.pvs_cell_size, 0.05f, 0.05f, 100.0f, "%.2f m");
    if (ImGui::Button("Fit to content bounds")) {
        erhe::math::Aabb bounds{};
        {
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root.item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
                const uint64_t flags = mesh->get_flag_bits();
                if (((flags & erhe::Item_flags::content) == 0) || ((flags & erhe::Item_flags::visible) == 0) || mesh->skin) {
                    continue;
                }
                const erhe::math::Aabb world_aabb = mesh->get_aabb_world();
                if (world_aabb.is_valid_3d()) {
                    bounds.include(world_aabb);
                }
            }
        }
        if (bounds.is_valid_3d()) {
            settings.pvs_region_min = bounds.min;
            settings.pvs_region_max = bounds.max;
        }
    }
    const glm::vec3 extent = settings.pvs_region_max - settings.pvs_region_min;
    if ((settings.pvs_cell_size > 0.0f) && glm::all(glm::greaterThan(extent, glm::vec3{0.0f}))) {
        const glm::uvec3 counts{
            static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.x / settings.pvs_cell_size))),
            static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.y / settings.pvs_cell_size))),
            static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.z / settings.pvs_cell_size)))
        };
        ImGui::Text("Grid: %u x %u x %u = %llu cells", counts.x, counts.y, counts.z, static_cast<unsigned long long>(uint64_t{counts.x} * counts.y * counts.z));
    } else {
        ImGui::TextUnformatted("Region unset: the bake covers the static entries' bounds");
    }
}

void Pvs_window::parameters_imgui()
{
    const char* methods[] = { "sampled", "disocclusion" };
    int method = (m_request.method == erhe::scene::Pvs_bake_method::disocclusion) ? 1 : 0;
    if (ImGui::Combo("Method", &method, methods, IM_ARRAYSIZE(methods))) {
        m_request.method = (method == 1) ? erhe::scene::Pvs_bake_method::disocclusion : erhe::scene::Pvs_bake_method::sampled;
    }
    const auto drag_uint = [](const char* label, uint32_t& value, const int minimum, const int maximum) {
        int as_int = static_cast<int>(value);
        if (ImGui::DragInt(label, &as_int, 1.0f, minimum, maximum)) {
            value = static_cast<uint32_t>(std::clamp(as_int, minimum, maximum));
        }
    };
    drag_uint("Face resolution", m_request.face_resolution, 8, 8192);
    float fov_degrees = glm::degrees(m_request.face_fov);
    if (ImGui::DragFloat("Face FOV", &fov_degrees, 0.5f, 90.0f, 170.0f, "%.1f deg")) {
        m_request.face_fov = glm::radians(fov_degrees);
    }
    if (m_request.method == erhe::scene::Pvs_bake_method::disocclusion) {
        drag_uint("Tile size",        m_request.tile_size,        1, 64);
        drag_uint("Layer count",      m_request.layer_count,      1, 64);
        drag_uint("Samples per face", m_request.samples_per_face, 1, 64);
        bool silhouette_tests = (m_request.silhouette_tests == erhe::scene_renderer::Pvs_silhouette_tests::on);
        if (ImGui::Checkbox("Silhouette tests (rule 3)", &silhouette_tests)) {
            m_request.silhouette_tests = silhouette_tests ? erhe::scene_renderer::Pvs_silhouette_tests::on : erhe::scene_renderer::Pvs_silhouette_tests::off;
        }
    } else {
        drag_uint("Samples per cell",   m_request.samples_per_cell,   1, 1024);
        drag_uint("Captures in flight", m_request.captures_in_flight, 1, 64);
    }
}

void Pvs_window::bake_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    Pvs_baker* const baker = m_context.pvs_baker;
    if (baker == nullptr) {
        ImGui::TextUnformatted("The PVS baker is not available");
        return;
    }
    // Scalars only, per frame (no container copied).
    const Pvs_bake_summary status = baker->get_summary();
    const bool running = (status.state == Pvs_bake_state::running);
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Bake")) {
        // Started at the next prepare phase (Pvs_baker::start_bake() runs
        // in M2, outside the ImGui iteration).
        const std::weak_ptr<Scene_root> weak_scene_root = scene_root;
        const Pvs_bake_request          request         = m_request;
        m_context.imgui_windows->queue(
            [this, weak_scene_root, request]() {
                const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
                if (!target || (m_context.pvs_baker == nullptr)) {
                    return;
                }
                m_last_error = m_context.pvs_baker->start_bake(target, request);
            }
        );
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Cancel")) {
        m_context.imgui_windows->queue(
            [this]() {
                if (m_context.pvs_baker != nullptr) {
                    m_context.pvs_baker->cancel();
                }
            }
        );
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(running || !scene_root->get_pvs_database());
    if (ImGui::Button("Clear")) {
        const std::weak_ptr<Scene_root> weak_scene_root = scene_root;
        m_context.imgui_windows->queue(
            [weak_scene_root]() {
                const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
                if (target) {
                    target->set_pvs_database({});
                }
            }
        );
    }
    ImGui::EndDisabled();
    if (!m_last_error.empty()) {
        ImGui::TextWrapped("Refused: %s", m_last_error.c_str());
    }

    const std::string& scene_name = baker->get_scene_name();
    ImGui::Text("State: %s%s%s", c_str(status.state), scene_name.empty() ? "" : " - ", scene_name.c_str());
    if (status.cell_count > 0) {
        const float fraction = static_cast<float>(status.cells_done) / static_cast<float>(status.cell_count);
        // Formatted only when the counts change.
        if ((m_overlay_cells_done != status.cells_done) || (m_overlay_cell_count != status.cell_count)) {
            m_overlay_cells_done = status.cells_done;
            m_overlay_cell_count = status.cell_count;
            m_overlay            = fmt::format("{} / {} cells", status.cells_done, status.cell_count);
        }
        ImGui::ProgressBar(fraction, ImVec2{-1.0f, 0.0f}, m_overlay.c_str());
        ImGui::Text("Elapsed %.1f s, remaining about %.1f s", status.seconds_elapsed, status.seconds_remaining);
        ImGui::Text("Columns %u, cells with a PVS %u", status.column_count, status.cells_with_pvs);
    }
    if (status.method == erhe::scene::Pvs_bake_method::disocclusion) {
        ImGui::Text("Cells without a sample point %u%s", status.cells_without_sample_point, status.sample_point_test ? "" : " (no free point test)");
        const Pvs_bake_gpu_times& times = status.gpu_times;
        ImGui::Text("GPU per cell %.2f ms", static_cast<double>(times.cell) * 1.0e-6);
        ImGui::Text(
            "first face: depth %.3f id %.3f tiles %.3f layer loop %.3f map %.3f vote %.3f ms",
            static_cast<double>(times.depth_pass)   * 1.0e-6,
            static_cast<double>(times.id_pass)      * 1.0e-6,
            static_cast<double>(times.tiles)        * 1.0e-6,
            static_cast<double>(times.layer_loop)   * 1.0e-6,
            static_cast<double>(times.map)          * 1.0e-6,
            static_cast<double>(times.vote)         * 1.0e-6
        );
    }
    const std::string& error = baker->get_error();
    if (!error.empty()) {
        ImGui::TextWrapped("Error: %s", error.c_str());
    }
}

void Pvs_window::database_imgui(Scene_root& scene_root)
{
    const std::shared_ptr<erhe::scene::Pvs_database> database = scene_root.get_pvs_database();
    const erhe::scene::Pvs_state                     state    = scene_root.get_pvs_state();
    ImGui::Text("State: %s", erhe::scene::c_str(state));
    if (state == erhe::scene::Pvs_state::stale) {
        // The stale mark (doc/plans/pvs.md D4): PVS culling is off for the
        // scene until the next bake.
        ImGui::TextColored(ImVec4{1.0f, 0.6f, 0.2f, 1.0f}, "Stale: a baked entry moved or was removed; PVS culling is off until a rebake");
    }
    if (!database) {
        ImGui::Checkbox("Debug overlay", &m_show_overlay);
        return;
    }
    const erhe::scene::Pvs_header& header = database->get_header();
    ImGui::Text("Grid %u x %u x %u of %.2f m, %u cells, %u with a PVS", header.cell_counts.x, header.cell_counts.y, header.cell_counts.z, header.cell_size, database->get_cell_count(), database->get_cells_with_pvs());
    ImGui::Text("Columns %u, stored pages %u, bits %.1f KB", database->get_column_count(), database->get_stored_page_count(), static_cast<double>(static_cast<std::size_t>(database->get_stored_page_count()) * database->get_words_per_page() * sizeof(uint32_t)) / 1024.0);
    ImGui::Text("Method %s, baked in %.1f s", erhe::scene::c_str(header.bake_parameters.method), header.bake_duration_seconds);
    // The page streaming (doc/plans/pvs.md "Residency and streaming").
    const erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root.get_draw_list_scene();
    if (draw_list_scene != nullptr) {
        draw_list_scene->get_pvs_streaming_status(m_streaming_status);
        ImGui::Text(
            "Resident pages %u of %u slots (%.1f KB), uploads %llu, evictions %llu%s",
            m_streaming_status.resident_pages, m_streaming_status.capacity,
            static_cast<double>(m_streaming_status.slot_bytes) / 1024.0,
            static_cast<unsigned long long>(m_streaming_status.uploads),
            static_cast<unsigned long long>(m_streaming_status.evictions),
            m_streaming_status.bits_in_memory ? ", bits in memory" : ", bits stream from the scene file"
        );
    }

    const std::shared_ptr<erhe::scene::Camera> camera = find_pvs_viewport_camera(m_context, scene_root);
    if (camera) {
        const glm::vec3 position = glm::vec3{camera->world_from_node()[3]};
        const std::optional<uint32_t> cell = database->find_cell(position);
        if (cell.has_value()) {
            if (database->has_cell_pvs(cell.value()) && database->has_resident_bits()) {
                ImGui::Text("Camera cell %u: PVS %u of %u columns", cell.value(), database->get_cell_pvs_size(cell.value()), database->get_column_count());
            } else if (database->has_cell_pvs(cell.value())) {
                // The bits stream from the scene file (doc/plans/pvs.md
                // "Residency and streaming"); get_pvs_cell reads a cell's.
                ImGui::Text("Camera cell %u: has a PVS (the bits stream from the scene file)", cell.value());
            } else {
                ImGui::Text("Camera cell %u: no PVS (everything visible)", cell.value());
            }
        } else {
            ImGui::TextUnformatted("Camera outside the grid");
        }
    }
    ImGui::Checkbox("Debug overlay", &m_show_overlay);
}

} // namespace editor
