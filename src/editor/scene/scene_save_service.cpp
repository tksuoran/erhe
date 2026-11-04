#include "scene/scene_save_service.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "parsers/usd.hpp"
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
        erhe::file::to_string(request.path)
    );
    m_requests.push_back(std::move(request));
}

auto Scene_save_service::can_start() const -> bool
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
    if (m_active) {
        bool done = false;
        {
            std::lock_guard<std::mutex> lock{m_worker_mutex};
            done = m_worker_done;
        }
        if (!done) {
            return;
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
    if ((request.kind == Gltf_scene_save_kind::scene) && (scene_root->get_source_format() == Scene_source_format::usd)) {
        if (request.on_started) {
            request.on_started();
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

    std::unique_ptr<Gltf_scene_save> save = begin_scene_save_gltf(m_context, *scene_root, request.path, request.kind, request.texture_export, request.export_settings.has_value() ? &request.export_settings.value() : nullptr);
    if (!save) {
        if (request.on_finished) {
            request.on_finished(make_failed_result("save refused: " + request.path.string() + " (details in logs/log.txt)"));
        }
        return;
    }

    m_running_request    = std::move(request);
    m_running_scene_root = scene_root;
    m_running_save       = std::move(save);
    m_running_start_time = start_time;
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        m_worker_done = false;
        m_worker_ok   = false;
    }
    m_running_scene_root->set_save_in_progress(true);
    set_active(true);
    log_operations->info("Scene save started: '{}' -> '{}' (scene is read-only until it ends)", scene_root->get_name(), erhe::file::to_string(m_running_request.path));
    if (m_running_request.on_started) {
        m_running_request.on_started();
    }
    Gltf_scene_save* const save_pointer = m_running_save.get();
    m_worker = std::thread{
        [this, save_pointer]() {
            ERHE_PROFILE_THREAD_NAME("Scene save");
            bool ok = false;
            try {
                ok = write_scene_save_gltf(*save_pointer);
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
    m_worker.join();
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock{m_worker_mutex};
        ok = m_worker_ok;
    }
    Scene_save_result result{};
    result.saved = ok;
    if (ok) {
        finish_scene_save_gltf(m_context, *m_running_save, &result.report);
        if (m_running_request.source_binding == Scene_save_source_binding::set_source) {
            m_running_scene_root->set_source_path(m_running_request.path, Scene_source_format::gltf);
        }
        result.file_bytes = result.report.file_bytes;
    } else {
        result.error = "save failed: " + m_running_request.path.string() + " (details in logs/log.txt)";
    }
    result.save_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_running_start_time).count();
    log_operations->info(
        "Scene save {}: '{}' -> '{}' in {:.1f} s",
        ok ? "done" : "failed",
        m_running_scene_root->get_name(),
        erhe::file::to_string(m_running_request.path),
        result.save_ms / 1000.0
    );

    Scene_save_request request = std::move(m_running_request);
    m_running_request = Scene_save_request{};
    m_running_save.reset();
    m_running_scene_root->set_save_in_progress(false);
    m_running_scene_root.reset();
    set_active(false);
    if (request.on_finished) {
        request.on_finished(result);
    }
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
    const erhe::gltf::Gltf_export_progress_state progress = get_scene_save_progress(*m_running_save);
    const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_running_start_time).count();
    const char* const verb = (m_running_request.kind == Gltf_scene_save_kind::prepared) ? "Preparing" : "Saving";
    ImGui::Text("%s '%s': %s %.0f s", verb, m_running_scene_root->get_name().c_str(), erhe::gltf::c_str(progress.stage), elapsed_s);
    if (progress.total > 0) {
        ImGui::SameLine();
        const float fraction = static_cast<float>(progress.done) / static_cast<float>(progress.total);
        char overlay[64];
        const fmt::format_to_n_result<char*> end = fmt::format_to_n(overlay, sizeof(overlay) - 1, "{} / {}", progress.done, progress.total);
        *end.out = '\0';
        ImGui::ProgressBar(fraction, ImVec2{160.0f, 0.0f}, overlay);
    }
    return true;
}

}
