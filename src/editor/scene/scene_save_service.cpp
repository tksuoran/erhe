#include "scene/scene_save_service.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "assets/prepared_keys.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/usd.hpp"
#include "renderers/pvs_baker.hpp"
#include "scene/scene_root.hpp"
#include "time.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_file/file.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

namespace editor {

namespace {

[[nodiscard]] auto make_failed_result(std::string error) -> Scene_save_result
{
    Scene_save_result result{};
    result.error = std::move(error);
    return result;
}

} // anonymous namespace

auto get_export_action(const Scene_export_mode mode) -> const char*
{
    switch (mode) {
        case Scene_export_mode::status:            return "Status";
        case Scene_export_mode::bake_world_meshes: return "Bake World-Space Meshes";
        default:                                   return "Export";
    }
}

void request_world_space_bake(
    App_context&                                                    context,
    const std::shared_ptr<Scene_root>&                              scene_root,
    std::function<void(const std::shared_ptr<const Scene_export>&)> on_started,
    std::function<void(const Scene_save_result&)>                   on_finished
)
{
    ERHE_VERIFY(context.scene_save_service != nullptr);
    context.scene_save_service->request(
        Scene_save_request{
            .scene_root  = scene_root,
            .path        = {},
            .job         = Scene_save_job::store,
            .target      = Prepare_target::pc,
            .export_mode = Scene_export_mode::bake_world_meshes,
            .on_started  = std::move(on_started),
            .on_finished = std::move(on_finished)
        }
    );
}

void request_prepared_status(
    App_context&                                                    context,
    const std::shared_ptr<Scene_root>&                              scene_root,
    const Prepare_target                                            target,
    const std::filesystem::path&                                    prepared_asset_path,
    const Prepared_drop                                             drop,
    std::function<void(const std::shared_ptr<const Scene_export>&)> on_started,
    std::function<void(const Scene_save_result&)>                   on_finished,
    const bool                                                      automatic
)
{
    ERHE_VERIFY(context.scene_save_service != nullptr);
    scene_root->mark_prepared_status_pending();
    context.scene_save_service->request(
        Scene_save_request{
            .scene_root      = scene_root,
            .path            = prepared_asset_path,
            .job             = Scene_save_job::store,
            .target          = target,
            .export_mode     = Scene_export_mode::status,
            .drop            = drop,
            .automatic       = automatic,
            .on_started      = std::move(on_started),
            .on_finished     = [weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, on_finished = std::move(on_finished)](const Scene_save_result& result) {
                const std::shared_ptr<Scene_root> finished_scene_root = weak_scene_root.lock();
                if (finished_scene_root) {
                    finished_scene_root->set_prepared_status(
                        result.status_report.has_value() ? std::make_shared<const Prepared_status_report>(result.status_report.value()) : nullptr,
                        result.error
                    );
                }
                if (on_finished) {
                    on_finished(result);
                }
            }
        }
    );
}

void on_prefab_instances_reloaded(App_context& context, const std::shared_ptr<Scene_root>& scene_root)
{
    static_cast<void>(context);
    log_export->info("Prefab instances of '{}' reloaded: its status is out of date", scene_root->get_name());
    scene_root->get_prepared_keys().on_change(Prepared_key_change::prefab_reload);
}

Scene_save_service::Scene_save_service(App_context& context)
    : m_context{context}
{
}

Scene_save_service::~Scene_save_service() noexcept
{
    shutdown();
}

void Scene_save_service::request(Scene_save_request&& request)
{
    const std::shared_ptr<Scene_root> scene_root = request.scene_root.lock();
    log_operations->info(
        "Scene save queued: '{}' -> '{}'",
        scene_root ? scene_root->get_name() : std::string{"?"},
        request.path.empty() ? std::string{"<default prepared asset>"} : erhe::file::to_string(request.path)
    );
    m_requests.push_back(std::move(request));
}

auto Scene_save_service::can_start() const -> bool
{
    return is_scene_quiescent();
}

auto Scene_save_service::is_scene_quiescent() const -> bool
{
    // The scene must be quiescent: no operation queued or in flight (async
    // workers, scene commits, asset loads), no live edit that writes the
    // scene before it records its operation.
    if (m_context.is_scene_load_in_flight()) {
        return false;
    }
    if ((m_context.commands != nullptr) && (m_context.commands->get_active_mouse_command() != nullptr)) {
        return false;
    }
    if ((m_context.operation_stack != nullptr) && (m_context.operation_stack->get_undo_block_reason() != nullptr)) {
        return false;
    }
    if ((m_context.time != nullptr) && m_context.time->has_transform_animations()) {
        return false;
    }
    return true;
}

void Scene_save_service::update()
{
    update_requests();
    serve_prepared_keys();
}

void Scene_save_service::serve_prepared_keys()
{
    if (m_context.app_scenes == nullptr) {
        return;
    }
    m_prepared_key_roots = m_context.app_scenes->get_scene_roots();
    for (const std::shared_ptr<Scene_root>& scene_root : m_prepared_key_roots) {
        if (scene_root && !scene_root->is_close_requested()) {
            scene_root->get_prepared_keys().update(m_context, scene_root);
        }
    }
    m_prepared_key_roots.clear();
}

void Scene_save_service::notify_stores_changed()
{
    if (m_context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
        if (scene_root) {
            scene_root->get_prepared_keys().on_change(Prepared_key_change::stores);
        }
    }
}

void Scene_save_service::update_requests()
{
    if (m_active) {
        if (m_pvs_bake_stage) {
            update_pvs_bake_stage();
            return;
        }
        bool done = false;
        bool ok   = false;
        {
            std::lock_guard<std::mutex> lock{m_worker_mutex};
            done = m_worker_done;
            ok   = m_worker_ok;
        }
        if (!done) {
            return;
        }
        if (m_running_export && !is_scene_export_ready(m_context, *m_running_export)) {
            return;
        }
        if (ok && m_running_export && (get_scene_export_continuation(*m_running_export) == Scene_export_continuation::pvs_bake)) {
            m_worker.join();
            if (start_scene_export_pvs_bake(m_context, *m_running_export, m_running_scene_root)) {
                m_pvs_bake_stage = true;
                return;
            }
            {
                std::lock_guard<std::mutex> lock{m_worker_mutex};
                m_worker_ok = false;
            }
        }
        finish_running();
    }
    // A scene closed, or about to close, is not saved.
    while (!m_requests.empty()) {
        const std::shared_ptr<Scene_root> scene_root = m_requests.front().scene_root.lock();
        if (scene_root && !scene_root->is_close_requested()) {
            break;
        }
        Scene_save_request request = std::move(m_requests.front());
        m_requests.pop_front();
        log_operations->warn("Scene save of '{}' failed: the scene was closed before the save ran", erhe::file::to_string(request.path));
        if (request.on_finished) {
            request.on_finished(make_failed_result("the scene was closed before the save ran"));
        }
    }
    if (m_requests.empty()) {
        return;
    }
    if (!can_start()) {
        if (!m_waiting_logged) {
            m_waiting_logged = true;
            log_operations->info("Scene save waits for queued and running operations to finish");
        }
        return;
    }
    m_waiting_logged = false;
    Scene_save_request request = std::move(m_requests.front());
    m_requests.pop_front();
    start(std::move(request));
}

void Scene_save_service::start(Scene_save_request&& request)
{
    const std::shared_ptr<Scene_root> scene_root = request.scene_root.lock();
    if (!scene_root) {
        log_operations->warn("Scene save of '{}' failed: the scene was closed before the save ran", erhe::file::to_string(request.path));
        if (request.on_finished) {
            request.on_finished(make_failed_result("the scene was closed before the save ran"));
        }
        return;
    }
    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // A USD-backed scene saves through erhe::usd on the main thread.
    if ((request.job == Scene_save_job::save_scene) && (scene_root->get_source_format() == Scene_source_format::usd)) {
        if (request.on_started) {
            request.on_started(std::shared_ptr<const Scene_export>{});
        }
        Scene_save_result result{};
        result.usd = true;
        result.saved = save_scene_usd(m_context, *scene_root, request.path);
        if (result.saved && (request.source_binding == Scene_save_source_binding::set_source)) {
            scene_root->set_source_path(request.path, Scene_source_format::usd);
        }
        result.save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
        if (!result.saved) {
            result.error = "save failed: " + request.path.string() + " (details in logs/log.txt)";
        }
        if (request.on_finished) {
            request.on_finished(result);
        }
        return;
    }

    std::unique_ptr<Gltf_scene_save> save;
    std::unique_ptr<Scene_export>    scene_export;
    if (request.job == Scene_save_job::store) {
        // The scene's export settings as they are when the Export starts are its only settings input (R7.2).
        const std::string refusal = get_export_refusal(*scene_root, get_export_action(request.export_mode));
        if (!refusal.empty()) {
            log_operations->warn("{}", refusal);
            if (request.on_finished) {
                request.on_finished(make_failed_result(refusal));
            }
            return;
        }
        scene_export = begin_scene_export(m_context, *scene_root, c_str(request.target), request.path, request.export_mode, request.drop);
    } else {
        save = begin_scene_save_gltf(m_context, *scene_root, request.path, Gltf_scene_save_kind::scene, erhe::gltf::Gltf_texture_export::source, nullptr);
    }
    if (!save && !scene_export) {
        if (request.on_finished) {
            request.on_finished(make_failed_result("save refused: " + request.path.string() + " (details in logs/log.txt)"));
        }
        return;
    }

    m_running_request    = std::move(request);
    m_running_scene_root = scene_root;
    m_running_save       = std::move(save);
    m_running_export     = std::move(scene_export);
    m_running_start_time = start_time;
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        m_worker_done = false;
        m_worker_ok   = false;
    }
    m_running_scene_root->set_save_in_progress(true);
    set_active(true);
    log_operations->info("Scene save started: '{}' -> '{}' (scene is read-only until it ends)", scene_root->get_name(), erhe::file::to_string(get_running_path()));
    if (m_running_request.on_started) {
        m_running_request.on_started(m_running_export);
    }
    launch_worker();
}

void Scene_save_service::update_pvs_bake_stage()
{
    const Scene_export_pvs_bake_state state = update_scene_export_pvs_bake(m_context, *m_running_export);
    if (state == Scene_export_pvs_bake_state::running) {
        return;
    }
    m_pvs_bake_stage = false;
    if (state == Scene_export_pvs_bake_state::done) {
        launch_worker();
        return;
    }
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        m_worker_done = true;
        m_worker_ok   = false;
    }
    finish_running();
}

void Scene_save_service::launch_worker()
{
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        m_worker_done = false;
        m_worker_ok   = false;
    }
    Gltf_scene_save* const save_pointer   = m_running_save.get();
    Scene_export*    const export_pointer = m_running_export.get();
    m_worker = std::thread{
        [this, save_pointer, export_pointer]() {
            ERHE_PROFILE_THREAD_NAME("Scene save");
            bool ok = false;
            try {
                ok = (export_pointer != nullptr) ? run_scene_export(*export_pointer) : write_scene_save_gltf(*save_pointer);
            } catch (const std::exception& e) {
                log_operations->error("Scene save threw: {}", e.what());
            }
            std::lock_guard<std::mutex> lock{m_worker_mutex};
            m_worker_ok   = ok;
            m_worker_done = true;
        }
    };
}

void Scene_save_service::finish_running()
{
    ERHE_VERIFY(m_active);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        ok = m_worker_ok;
    }
    Scene_save_result result{};
    result.saved = ok;
    if (ok && m_running_export && (get_scene_export_mode(*m_running_export) == Scene_export_mode::status)) {
        result.status_report = finish_scene_status(*m_running_export);
    } else if (ok && m_running_export) {
        Scene_export_report export_report{};
        finish_scene_export(m_context, *m_running_export, &export_report);
        result.file_bytes    = export_report.prepared_asset_bytes;
        result.export_report = std::move(export_report);
    } else if (ok) {
        finish_scene_save_gltf(m_context, *m_running_save, &result.report);
        if (m_running_request.source_binding == Scene_save_source_binding::set_source) {
            m_running_scene_root->set_source_path(m_running_request.path, Scene_source_format::gltf);
        }
        result.file_bytes = result.report.file_bytes;
    } else if (m_running_export && !get_scene_export_error(*m_running_export).empty()) {
        result.error = get_scene_export_error(*m_running_export);
    } else {
        result.error = "save failed: " + get_running_path().string() + " (details in logs/log.txt)";
    }
    result.save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_running_start_time).count();
    log_operations->info(
        "Scene save {}: '{}' -> '{}' in {:.1f} s",
        ok ? "done" : "failed",
        m_running_scene_root->get_name(),
        erhe::file::to_string(get_running_path()),
        result.save_ms / 1000.0
    );

    // A job that writes the stores changes what every scene's status reads (R8.12).
    const bool wrote_stores = m_running_export && (
        (get_scene_export_mode(*m_running_export) != Scene_export_mode::status) || (m_running_request.drop != Prepared_drop::none)
    );
    if (wrote_stores) {
        invalidate_scene_export_catalogs(*m_running_export);
    }
    const std::shared_ptr<Scene_root> finished_scene_root = m_running_scene_root;
    Scene_save_request request = std::move(m_running_request);
    m_running_request = Scene_save_request{};
    m_running_save.reset();
    m_running_export.reset();
    m_running_scene_root->set_save_in_progress(false);
    m_running_scene_root.reset();
    set_active(false);
    if (wrote_stores) {
        notify_stores_changed();
    }
    // An Export knows the Scene key of the scene as it is now: the next key check needs no status job for it.
    if (ok && result.export_report.has_value() && (request.export_mode == Scene_export_mode::export_items)) {
        finished_scene_root->get_prepared_keys().on_export_finished(
            result.export_report->target, request.path, result.export_report->scene_key
        );
    }
    if (request.on_finished) {
        request.on_finished(result);
    }
}

auto Scene_save_service::get_running_path() const -> const std::filesystem::path&
{
    return m_running_export ? get_scene_export_prepared_asset_path(*m_running_export) : m_running_request.path;
}

void Scene_save_service::set_active(const bool active)
{
    m_active = active;
    // Change-driven: the windows' contents are disabled for the save's duration.
    if (m_context.imgui_windows != nullptr) {
        m_context.imgui_windows->set_window_contents_disabled(active);
    }
}

void Scene_save_service::shutdown()
{
    if (!m_active) {
        return;
    }
    log_operations->info("Waiting for the running scene save to finish");
    if (m_pvs_bake_stage) {
        // An Export whose PVS bake is still running ends failed: the bake steps need frames.
        m_pvs_bake_stage = false;
        if (m_context.pvs_baker != nullptr) {
            m_context.pvs_baker->cancel();
        }
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        m_worker_done = true;
        m_worker_ok   = false;
    }
    finish_running();
}

auto Scene_save_service::is_active() const -> bool
{
    return m_active;
}

auto Scene_save_service::is_read_only(const Scene_root& scene_root) const -> bool
{
    return m_active && (m_running_scene_root.get() == &scene_root);
}

auto Scene_save_service::is_read_only(const erhe::Item_host* const item_host) const -> bool
{
    return m_active && (item_host != nullptr) && (static_cast<const erhe::Item_host*>(m_running_scene_root.get()) == item_host);
}

auto Scene_save_service::is_automatic_status_job_active() const -> bool
{
    return m_active && m_running_request.automatic;
}

auto Scene_save_service::is_pvs_bake_stage() const -> bool
{
    return m_active && m_pvs_bake_stage;
}

auto Scene_save_service::get_pending_count() const -> std::size_t
{
    return m_requests.size() + (m_active ? 1u : 0u);
}

auto Scene_save_service::imgui_status() const -> bool
{
    if (!m_active) {
        if (!m_requests.empty()) {
            ImGui::TextUnformatted("Save waiting for operations");
            return true;
        }
        return false;
    }
    const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_running_start_time).count();
    std::size_t done  = 0;
    std::size_t total = 0;
    if (m_running_export) {
        const Scene_export_progress_state progress = get_scene_export_progress(*m_running_export);
        const Scene_export_mode mode = get_scene_export_mode(*m_running_export);
        const char* const verb =
            (mode == Scene_export_mode::status)            ? "Checking" :
            (mode == Scene_export_mode::bake_world_meshes) ? "Baking"   : "Exporting";
        ImGui::Text("%s '%s': %s %.0f s", verb, m_running_scene_root->get_name().c_str(), c_str(progress.stage), elapsed_s);
        done  = progress.done;
        total = progress.total;
    } else {
        const erhe::gltf::Gltf_export_progress_state progress = get_scene_save_progress(*m_running_save);
        ImGui::Text("Saving '%s': %s %.0f s", m_running_scene_root->get_name().c_str(), erhe::gltf::c_str(progress.stage), elapsed_s);
        done  = progress.done;
        total = progress.total;
    }
    if (total > 0) {
        ImGui::SameLine();
        const float fraction = static_cast<float>(done) / static_cast<float>(total);
        char overlay[64];
        const fmt::format_to_n_result<char*> end = fmt::format_to_n(overlay, sizeof(overlay) - 1, "{} / {}", done, total);
        *end.out = '\0';
        ImGui::ProgressBar(fraction, ImVec2{160.0f, 0.0f}, overlay);
    }
    return true;
}

}
