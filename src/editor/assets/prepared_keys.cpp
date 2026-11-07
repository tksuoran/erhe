#include "assets/prepared_keys.hpp"

#include "app_context.hpp"
#include "assets/editor_asset.hpp"
#include "assets/prepared_status.hpp"
#include "assets/scene_export.hpp"
#include "editor_log.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_save_service.hpp"

#include "erhe_asset_store/key.hpp"
#include "erhe_file/file.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>

namespace editor {

namespace {

// The scene has an editor asset to check against: one is open, or its file is
// where the scene records it or beside the source. A check never makes one (Export does).
[[nodiscard]] auto has_editor_asset(App_context& context, const Scene_root& scene_root) -> bool
{
    const std::filesystem::path& source_path = scene_root.get_source_path();
    if ((context.editor_asset_registry != nullptr) && context.editor_asset_registry->find(source_path)) {
        return true;
    }
    std::error_code error_code;
    const std::filesystem::path& record_path = scene_root.get_editor_asset_record();
    if (!record_path.empty() && std::filesystem::is_regular_file(record_path, error_code)) {
        return true;
    }
    return std::filesystem::is_regular_file(get_default_editor_asset_path(source_path), error_code);
}

} // anonymous namespace

Prepared_keys::Prepared_keys() = default;

Prepared_keys::~Prepared_keys() noexcept
{
    join_check();
}

void Prepared_keys::join_check()
{
    if (m_check_thread.joinable()) {
        m_check_thread.join();
    }
}

void Prepared_keys::on_change(const Prepared_key_change change)
{
    static_cast<void>(change);
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_stale = true;
    ++m_change_serial;
}

void Prepared_keys::on_mesh_transform_changed(const erhe::scene::Mesh& mesh)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_world_meshes.contains(&mesh)) {
        m_stale = true;
        ++m_change_serial;
    }
}

void Prepared_keys::set_target(const std::string_view target, const std::filesystem::path& prepared_asset_path)
{
    if ((m_target == target) && (m_prepared_asset_path == prepared_asset_path)) {
        return;
    }
    m_target              = std::string{target};
    m_prepared_asset_path = prepared_asset_path;
    on_change(Prepared_key_change::target);
}

auto Prepared_keys::get_target() const -> const std::string&
{
    return m_target;
}

auto Prepared_keys::get_prepared_asset_path() const -> const std::filesystem::path&
{
    return m_prepared_asset_path;
}

auto Prepared_keys::get_status() const -> const std::shared_ptr<const Prepared_status_report>&
{
    return m_status;
}

auto Prepared_keys::get_error() const -> const std::string&
{
    return m_error;
}

auto Prepared_keys::is_job_pending() const -> bool
{
    return m_check || (m_status_job_count > 0);
}

void Prepared_keys::request_update()
{
    m_update_requested = true;
}

auto Prepared_keys::is_stale() const -> bool
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_stale;
}

auto Prepared_keys::is_pending() const -> bool
{
    if (is_job_pending()) {
        return true;
    }
    return m_update_requested && is_stale();
}

void Prepared_keys::on_export_finished(const std::string_view target, const std::filesystem::path& prepared_asset_path, const std::string& scene_key)
{
    m_exported_scene_key = scene_key;
    m_exported_target    = std::string{target};
    m_exported_path      = prepared_asset_path;
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_exported_serial = m_change_serial;
}

void Prepared_keys::on_status_job_queued()
{
    ++m_status_job_count;
}

void Prepared_keys::on_status_job_finished(const std::shared_ptr<const Prepared_status_report>& status, const std::string& error)
{
    if (m_status_job_count > 0) {
        --m_status_job_count;
    }
    if (status) {
        m_status = status;
    }
    m_error = error;
}

void Prepared_keys::finish_check(App_context& context, const std::shared_ptr<Scene_root>& scene_root)
{
    join_check();
    bool ok = false;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        ok = m_check_ok;
        m_check_done = false;
    }
    std::unique_ptr<Scene_export> check = std::move(m_check);
    if (!ok) {
        m_error = get_scene_export_error(*check);
        if (m_error.empty()) {
            m_error = "the key check failed (details in logs/log.txt)";
        }
        return;
    }
    std::shared_ptr<const Prepared_status_report> status = std::make_shared<const Prepared_status_report>(finish_scene_status(*check));
    {
        std::vector<const erhe::scene::Mesh*> world_meshes;
        collect_scene_export_world_meshes(*check, world_meshes);
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_world_meshes.clear();
        m_world_meshes.insert(world_meshes.begin(), world_meshes.end());
    }
    check.reset(); // releases the shapes it held
    m_status = status;
    m_error.clear();
    bool changed_since = false;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        changed_since = (m_change_serial != m_check_serial);
    }
    // A check that leaves the Scene key open: the status job computes it with the scene read-only.
    if (!status->scene_key_checked && !changed_since && (context.scene_save_service != nullptr)) {
        const Prepare_target target = (m_target == c_str(Prepare_target::quest)) ? Prepare_target::quest : Prepare_target::pc;
        request_prepared_status(context, scene_root, target, m_prepared_asset_path, Prepared_drop::none, {}, {}, true);
    }
}

void Prepared_keys::update(App_context& context, const std::shared_ptr<Scene_root>& scene_root)
{
    if (m_check) {
        bool done = false;
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            done = m_check_done;
        }
        if (!done) {
            return;
        }
        finish_check(context, scene_root);
    }
    if (!m_update_requested || (m_status_job_count > 0)) {
        return;
    }
    std::uint64_t serial = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_stale) {
            m_update_requested = false;
            return;
        }
        serial = m_change_serial;
    }
    // A check reads the stores Export writes and the scene a live edit changes: it starts when both are quiet.
    Scene_save_service* const save_service = context.scene_save_service;
    if ((save_service == nullptr) || (save_service->get_pending_count() > 0) || !save_service->is_scene_quiescent()) {
        return;
    }
    m_update_requested = false;
    const auto settle = [this, serial](std::shared_ptr<const Prepared_status_report> status, std::string error) {
        m_status = std::move(status);
        m_error  = std::move(error);
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_change_serial == serial) {
            m_stale = false;
        }
    };
    const std::string refusal = get_export_refusal(*scene_root, "Status");
    if (!refusal.empty()) {
        settle({}, refusal);
        return;
    }
    if (!has_editor_asset(context, *scene_root)) {
        settle({}, "the scene has no editor asset yet - Export makes it");
        return;
    }
    std::unique_ptr<Scene_export> check = begin_scene_export(context, *scene_root, m_target, m_prepared_asset_path, Scene_export_mode::key_check, Prepared_drop::none);
    if (!check) {
        settle({}, "the key check was refused (details in logs/log.txt)");
        return;
    }
    const bool exported_state = (m_exported_serial == serial) && (m_exported_target == m_target) && (m_exported_path == m_prepared_asset_path);
    if (exported_state) {
        const std::optional<erhe::asset_store::Key> scene_key = erhe::asset_store::Key::from_hex(m_exported_scene_key);
        if (scene_key.has_value()) {
            set_scene_export_known_scene_key(*check, scene_key.value());
        }
    }
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_change_serial == serial) {
            m_stale = false;
        }
        m_check_done = false;
        m_check_ok   = false;
    }
    m_check_serial = serial;
    m_check        = std::move(check);
    Scene_export* const check_pointer = m_check.get();
    m_check_thread = std::thread{
        [this, check_pointer]() {
            ERHE_PROFILE_THREAD_NAME("Key check");
            bool ok = false;
            try {
                ok = run_scene_export(*check_pointer);
            } catch (const std::exception& e) {
                log_export->error("Key check threw: {}", e.what());
            }
            const std::lock_guard<std::mutex> lock{m_mutex};
            m_check_ok   = ok;
            m_check_done = true;
        }
    };
}

void refresh_prepared_status(App_context& context, Scene_root& scene_root)
{
    if (context.editor_asset_registry != nullptr) {
        context.editor_asset_registry->invalidate_item_catalogs();
    }
    Prepared_keys& keys = scene_root.get_prepared_keys();
    keys.on_change(Prepared_key_change::stores);
    keys.request_update();
}

}
