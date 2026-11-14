#pragma once

#include "assets/scene_export.hpp"
#include "parsers/gltf.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace erhe { class Item_host; }

namespace editor {

class App_context;
class Scene_root;

// What a request runs: Save Scene (glTF, or USDA for a USD-backed scene), or a
// job of the store (an Export, a status or a bake; Scene_save_request::export_mode).
enum class Scene_save_job : unsigned int
{
    save_scene = 0,
    store
};

// Whether a successful save makes the written file the scene's source.
enum class Scene_save_source_binding : unsigned int
{
    keep = 0,
    set_source
};

class Scene_save_result
{
public:
    bool                   saved{false};
    std::string            error;
    double                 save_ms{0.0};
    std::uintmax_t         file_bytes{0};
    bool                   usd{false};
    Gltf_scene_save_report report;
    // An Export into the store.
    std::optional<Scene_export_report> export_report;
    // A status job (Scene_export_mode::status).
    std::optional<Prepared_status_report> status_report;
};

class Scene_save_request
{
public:
    std::weak_ptr<Scene_root>       scene_root;
    // Save Scene: the written file; a store job: the prepared asset (empty:
    // the default location; doc/editor/prepared_assets.md).
    std::filesystem::path           path;
    Scene_save_job                  job           {Scene_save_job::save_scene};
    Scene_save_source_binding       source_binding{Scene_save_source_binding::keep};
    // A store job, which reads the scene's export settings when it starts:
    // an Export, or the status with an optional drop (doc/editor/prepared_assets.md "Status").
    Prepare_target                  target        {Prepare_target::pc};
    Scene_export_mode               export_mode   {Scene_export_mode::export_items};
    Prepared_drop                   drop          {Prepared_drop::none};
    // Queued by Prepared_keys, not by a user or a client.
    bool                                 automatic     {false};
    // Main thread, when the save starts running (with the running store job, null
    // for a Save Scene and for an Export's navigation stage, after which it is called
    // again with the Scene_export; its progress is readable on any thread) and when it has finished.
    std::function<void(const std::shared_ptr<const Scene_export>&)> on_started;
    std::function<void(const Scene_save_result&)>                   on_finished;
};

// The action a refusal of a store job names: "Export", "Status", "Bake World-Space Meshes" or "Build Navigation Mesh".
[[nodiscard]] auto get_export_action(Scene_export_mode mode) -> const char*;

// A store job that runs no Scene_export, with the save's staging (Build Navigation Mesh).
class Scene_service_job
{
public:
    virtual ~Scene_service_job() noexcept;

    // Worker thread while the scene is read-only.
    [[nodiscard]] virtual auto run() -> bool = 0;
    // Main thread after run(); ok is run()'s result. Sets result.saved and result.error.
    virtual void finish(App_context& context, bool ok, Scene_save_result& result) = 0;
    // Any thread.
    [[nodiscard]] virtual auto get_progress() const -> Scene_export_progress_state = 0;
    // The status line before the stage, e.g. "Building navigation mesh of '<scene>'".
    [[nodiscard]] virtual auto get_status_title() const -> const std::string& = 0;
};

// Queues Bake World-Space Meshes (R8.7) for the scene; the world-space form is installed when it ends. Main thread.
void request_world_space_bake(
    App_context&                                                    context,
    const std::shared_ptr<Scene_root>&                              scene_root,
    std::function<void(const std::shared_ptr<const Scene_export>&)> on_started,
    std::function<void(const Scene_save_result&)>                   on_finished
);

#if defined(ERHE_USE_RECAST)
// Queues Build Navigation Mesh for the scene (doc/editor/navigation.md "Job"); the
// refusals are checked when it starts. Main thread.
void request_navigation_build(
    App_context&                                  context,
    const std::shared_ptr<Scene_root>&            scene_root,
    std::function<void()>                         on_started,
    std::function<void(const Scene_save_result&)> on_finished
);
#endif

// Queues a status job of the store container for the scene (and a drop of
// R8.13); its report is kept on the Scene_root when the job ends. Main thread.
void request_prepared_status(
    App_context&                                                    context,
    const std::shared_ptr<Scene_root>&                              scene_root,
    Prepare_target                                                  target,
    const std::filesystem::path&                                    prepared_asset_path, // empty: the default location
    Prepared_drop                                                   drop,
    std::function<void(const std::shared_ptr<const Scene_export>&)> on_started,
    std::function<void(const Scene_save_result&)>                   on_finished,
    bool                                                            automatic = false
);

// R10.6: the change site of a reload of the prefab instances of the scene
// (a Save of a prefab source, MCP reload_prefab): the scene's status is
// computed again (Prepared_keys). Main thread.
void on_prefab_instances_reloaded(App_context& context, const std::shared_ptr<Scene_root>& scene_root);

// Runs scene saves one at a time with the editor UI live
// (doc/editor/scene_serialization.md "Saving in the background").
class Scene_save_service
{
public:
    explicit Scene_save_service(App_context& context);
    ~Scene_save_service() noexcept;
    Scene_save_service(const Scene_save_service&) = delete;
    auto operator=(const Scene_save_service&) -> Scene_save_service& = delete;

    void request (Scene_save_request&& request);
    // Main thread, M2, before Operation_stack::update(): the queued saves, then
    // the scenes' Prepared_keys (doc/editor/prepared_assets.md "Status").
    void update  ();
    // Waits for a running save to finish (editor shutdown).
    void shutdown();

    // True while a save runs: the editor-wide gates hold.
    [[nodiscard]] auto is_active   () const -> bool;
    [[nodiscard]] auto is_read_only(const Scene_root& scene_root) const -> bool;
    [[nodiscard]] auto is_read_only(const erhe::Item_host* item_host) const -> bool;
    [[nodiscard]] auto get_pending_count() const -> std::size_t;
    // The running Export bakes its PVS (pvs_mode rebake): the PVS bake steps run during it (R9.6).
    [[nodiscard]] auto is_pvs_bake_stage() const -> bool;
    // A request of `mode` for the scene is queued or running.
    [[nodiscard]] auto is_job_pending(const Scene_root& scene_root, Scene_export_mode mode) const -> bool;
    // A Build Navigation Mesh of the scene is queued or running, or an Export's navigation stage runs for it.
    [[nodiscard]] auto is_navigation_pending(const Scene_root& scene_root) const -> bool;
    // The running store job that is no Scene_export (its progress is readable on any thread); null when none.
    [[nodiscard]] auto get_running_service_job() const -> std::shared_ptr<const Scene_service_job>;
    // The running job is a status job without a drop that Prepared_keys queued.
    [[nodiscard]] auto is_automatic_status_job_active() const -> bool;
    // No operation queued or in flight, no scene load, no live edit, no transform animation.
    [[nodiscard]] auto is_scene_quiescent() const -> bool;

    // Status line: a running save's scene, stage and progress; false when idle.
    [[nodiscard]] auto imgui_status() const -> bool;

    // Every scene's status reads the stores a finished job or a load wrote.
    void notify_stores_changed();

private:
    // What start() does after an Export's navigation stage began (doc/editor/navigation.md "Export").
    enum class Export_navigation_stage : unsigned int
    {
        export_next = 0, // no build job: the Scene_export starts now
        job_started,     // the build job runs; the Scene_export starts after it
        failed           // finished as failed
    };

    [[nodiscard]] auto can_start() const -> bool;
    void update_requests();
    void serve_prepared_keys();
    void start         (Scene_save_request&& request);
    // A store job of a Scene_service_job; false (finished as refused) when it does not start.
    [[nodiscard]] auto start_service_job(Scene_save_request& request, const std::shared_ptr<Scene_root>& scene_root) -> bool;
    // Makes the job the running one and calls on_started.
    void run_service_job(Scene_save_request& request, const std::shared_ptr<Scene_root>& scene_root, std::shared_ptr<Scene_service_job> job);
    [[nodiscard]] auto start_export_navigation_stage(
        Scene_save_request&                request,
        const std::shared_ptr<Scene_root>& scene_root,
        Export_navigation_report&          out_report
    ) -> Export_navigation_stage;
    // After the navigation stage's worker run succeeded: installs the mesh and starts the Scene_export; false (failed) when either fails.
    [[nodiscard]] auto continue_export_after_navigation() -> bool;
    void finish_running();
    // The worker thread of the running save; an Export with a PVS bake runs it twice.
    void launch_worker();
    // The running Export's PVS bake stage, each update() while it runs.
    void update_pvs_bake_stage();
    void set_active    (bool active);
    // The file the running save or export writes.
    [[nodiscard]] auto get_running_path() const -> const std::filesystem::path&;

    App_context&                               m_context;
    std::deque<Scene_save_request>             m_requests;
    std::vector<std::shared_ptr<Scene_root>>   m_prepared_key_roots; // serve_prepared_keys() scratch
    bool                                       m_waiting_logged{false};

    // The running save (main thread owns all of these except m_worker_done).
    Scene_save_request                         m_running_request;
    std::shared_ptr<Scene_root>                m_running_scene_root;
    std::unique_ptr<Gltf_scene_save>           m_running_save;
    std::shared_ptr<Scene_export>              m_running_export;
    std::shared_ptr<Scene_service_job>         m_running_service_job;
    std::chrono::steady_clock::time_point      m_running_start_time;
    Export_navigation_report                   m_running_navigation;
    std::string                                m_running_error; // a failure no job object reports
    std::thread                                m_worker;
    bool                                       m_active{false};
    bool                                       m_pvs_bake_stage{false};

    // Written by the worker, read by the main thread.
    mutable std::mutex                         m_worker_mutex;
    bool                                       m_worker_done{false};
    bool                                       m_worker_ok  {false};
};

}
