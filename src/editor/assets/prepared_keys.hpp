#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;
class Prepared_status_report;
class Scene_export;
class Scene_root;

// What changed of the inputs a scene's keys are computed from
// (doc/editor/prepared_assets.md "Status").
enum class Prepared_key_change : unsigned int
{
    content = 0,   // a content mesh's primitives were replaced
    placement,     // a mesh the world-space bake admits moved
    mesh_set,      // a content mesh entered or left the scene
    settings,      // the scene's export settings
    prefab_reload, // the scene's prefab instances were reloaded (R10.6)
    stores,        // an editor asset or a prepared asset was written
    target         // the status was asked for another target
};

// The status of one scene's stores (doc/editor/prepared_assets.md "Status"):
// a change site marks it stale; request_update() asks for a key check, which
// the next prepare phase starts. Owned by its Scene_root.
class Prepared_keys
{
public:
    Prepared_keys();
    ~Prepared_keys() noexcept;
    Prepared_keys(const Prepared_keys&) = delete;
    auto operator=(const Prepared_keys&) -> Prepared_keys& = delete;

    // Any thread (Scene_host hooks run on load workers too).
    void on_change(Prepared_key_change change);
    // Any thread: only a mesh the last check found admitted by the world-space bake changes a key.
    void on_mesh_transform_changed(const erhe::scene::Mesh& mesh);

    // Main thread. The target and prepared asset of the status ("pc", default location until set).
    void set_target(std::string_view target, const std::filesystem::path& prepared_asset_path);
    [[nodiscard]] auto get_target() const -> const std::string&;
    [[nodiscard]] auto get_prepared_asset_path() const -> const std::filesystem::path&;

    // Main thread: the next prepare phase checks the keys when the status is stale.
    void request_update();

    // Main thread, M2 (Scene_save_service::update()): finishes a done key
    // check and starts a requested one.
    void update(App_context& context, const std::shared_ptr<Scene_root>& scene_root);
    // Main thread, after an Export of the scene into prepared_asset_path (empty:
    // the default location) wrote the Scene item of this key (hex): a check of the
    // unchanged scene and that target takes it as the Scene key.
    void on_export_finished(std::string_view target, const std::filesystem::path& prepared_asset_path, const std::string& scene_key);
    // Main thread: Scene_save_service status jobs of the scene (the Scene key, the drops).
    void on_status_job_queued();
    void on_status_job_finished(const std::shared_ptr<const Prepared_status_report>& status, const std::string& error);

    // Main thread.
    [[nodiscard]] auto get_status () const -> const std::shared_ptr<const Prepared_status_report>&;
    [[nodiscard]] auto get_error  () const -> const std::string&;
    // A change came after the status was computed.
    [[nodiscard]] auto is_stale   () const -> bool;
    // An update is requested and not finished, or a status job runs or waits to run.
    [[nodiscard]] auto is_pending () const -> bool;
    // Main thread: a key check or a status job of the scene runs or is queued.
    [[nodiscard]] auto is_job_pending() const -> bool;

private:
    void finish_check(App_context& context, const std::shared_ptr<Scene_root>& scene_root);
    void join_check();

    mutable std::mutex                           m_mutex;
    bool                                         m_stale{true};      // under m_mutex
    std::uint64_t                                m_change_serial{1}; // under m_mutex
    std::unordered_set<const erhe::scene::Mesh*> m_world_meshes;     // under m_mutex: the meshes the last check found admitted

    // Main thread.
    std::string                                   m_target{"pc"};
    std::filesystem::path                         m_prepared_asset_path; // empty: the default location
    std::shared_ptr<const Prepared_status_report> m_status;
    std::string                                   m_error;
    std::size_t                                   m_status_job_count{0};
    bool                                          m_update_requested{false};
    // The last Export's Scene key, valid while m_change_serial is m_exported_serial.
    std::string                                   m_exported_scene_key;
    std::string                                   m_exported_target;
    std::filesystem::path                         m_exported_path;
    std::uint64_t                                 m_exported_serial{0};

    // The running key check: the main thread starts and joins it.
    std::unique_ptr<Scene_export>                 m_check;
    std::uint64_t                                 m_check_serial{0};
    std::thread                                   m_check_thread;
    bool                                          m_check_done{false}; // under m_mutex
    bool                                          m_check_ok  {false}; // under m_mutex
};

// Main thread: Refresh (Properties, MCP get_prepared_status refresh): every
// open editor asset is listed again at its next use - another editor or a file
// copy may have written it (R5.6) - and an update of the scene's status is requested.
void refresh_prepared_status(App_context& context, Scene_root& scene_root);

}
