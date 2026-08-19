#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace editor {

class Asset_load_tick_context;

// Where a load is (doc/async-asset-loading-plan.md 2.1). The first four are
// live states; the last three are SETTLED - a settled load never changes
// state again, which is what wait_for_idle and the dependent-task machinery
// key on.
enum class Asset_load_state : unsigned int {
    queued    = 0, // accepted, not advanced yet
    running   = 1, // reading / parsing / decoding
    resident  = 2, // GPU objects created, uploads recorded (may still be publishing)
    done      = 3, // published
    failed    = 4,
    cancelled = 5
};

[[nodiscard]] auto c_str(Asset_load_state state) -> const char*;
[[nodiscard]] auto is_settled(Asset_load_state state) -> bool;

// The caller's view of a queued load. Handed out by
// Asset_manager::queue_load the moment the load is accepted - before it has
// advanced at all - so "a handle exists when the queueing call returns" holds
// even though the message bus pumps later in the tick than the asset tick
// slot (plan 3 step 1).
//
// Readable from any thread: the state and progress are atomics, the error
// string is mutex-guarded. Only the owning task writes.
class Asset_load_handle
{
public:
    explicit Asset_load_handle(std::filesystem::path path);

    [[nodiscard]] auto get_path    () const -> const std::filesystem::path& { return m_path; }
    [[nodiscard]] auto get_state   () const -> Asset_load_state;
    [[nodiscard]] auto get_progress() const -> float;
    [[nodiscard]] auto get_error   () const -> std::string;
    [[nodiscard]] auto is_settled  () const -> bool;

    // Cooperative: sets the flag. The task notices at its next phase
    // boundary and settles as `cancelled`; in-flight workers are not
    // interrupted (plan 2.11).
    void request_cancel();
    [[nodiscard]] auto is_cancel_requested() const -> bool;

    // Task side.
    void set_state   (Asset_load_state state);
    void set_progress(float progress);
    void set_failed  (std::string error);

private:
    const std::filesystem::path   m_path;
    std::atomic<Asset_load_state> m_state           {Asset_load_state::queued};
    std::atomic<float>            m_progress        {0.0f};
    std::atomic<bool>             m_cancel_requested{false};
    mutable std::mutex            m_error_mutex;
    std::string                   m_error;
};

class Prepared_gltf_parse;
class Scene_root;

// What a finished load produced. Handed to the completion callback on the
// main thread.
class Asset_load_result
{
public:
    // The opened scene. Null when the load failed, was cancelled, or the
    // file turned out not to be an erhe-authored scene.
    std::shared_ptr<Scene_root> scene_root;

    // The file is a plain glTF asset, not an erhe-authored scene: the caller
    // should import it instead (Scene_open_operation). Decided from the scan
    // the task ran on a worker, so the whole-file read that decision needs
    // never happens on the main thread.
    bool foreign_gltf{false};

    // Set with foreign_gltf when the task went on to parse the file
    // asynchronously as well: hand it to Scene_open_operation so the
    // operation does no file I/O. Null when the task handed back at the scan
    // (an adoptable container record exists, so the operation should take the
    // record's parse instead).
    std::shared_ptr<Prepared_gltf_parse> prepared_parse;
};

// What to load, and what to do with it.
class Asset_load_request
{
public:
    std::filesystem::path path;

    // Empty: open `path` as a NEW scene (erhe-authored scene or foreign glTF,
    // decided by the scan). Set: import `path` INTO this existing scene, and
    // the scan is skipped - the import machinery takes any glTF.
    //
    // Weak on purpose: a load must not keep its target scene alive, and the
    // task settles as cancelled if the scene closes while it is in flight.
    std::weak_ptr<Scene_root> import_target;

    // Import-only: route the parsed materials through Asset_manager as
    // references instead of copying them into the scene (R7).
    bool materials_as_references{false};

    // Load `path` as a Prefab_library template instead: no scan, and publish
    // hands the parse back for Prefab_library::finish_load_template. Mutually
    // exclusive with import_target.
    bool prefab_template{false};

    // Node name for the parse root. Empty picks a default per mode.
    std::string root_node_name;
};

// One unit of asynchronous loading, owned by Asset_manager and advanced a
// bounded amount from Asset_manager::tick.
//
// Threading contract: tick() runs on the MAIN thread only, with the frame's
// command buffer recording, so it - and only it - may create GPU objects and
// record commands. Anything a task hands to a worker must be detached data
// (plan 2.3).
class Asset_load_task
{
public:
    explicit Asset_load_task(std::shared_ptr<Asset_load_handle> handle);
    virtual ~Asset_load_task() noexcept;

    Asset_load_task(const Asset_load_task&)            = delete;
    Asset_load_task& operator=(const Asset_load_task&) = delete;

    // Advance by at most what the tick context's budget allows. Returns the
    // state after the slice; the manager reaps the task once it is settled.
    virtual auto tick(Asset_load_tick_context& tick_context) -> Asset_load_state = 0;

    // True when no worker this task spawned is still running, so destroying
    // it is safe (plan 2.3 invariant 4). A cancelled task is only reaped once
    // this returns true.
    [[nodiscard]] virtual auto is_worker_idle() const -> bool = 0;

    [[nodiscard]] auto get_handle() const -> const std::shared_ptr<Asset_load_handle>& { return m_handle; }

    // The scene this load imports into, for scene-close cancellation
    // (plan 2.11). Empty for loads that open a new scene - closing an
    // existing scene cannot orphan those.
    [[nodiscard]] virtual auto get_import_target() const -> std::weak_ptr<Scene_root> { return {}; }
    [[nodiscard]] auto get_state () const -> Asset_load_state { return m_handle->get_state(); }

protected:
    std::shared_ptr<Asset_load_handle> m_handle;
};

} // namespace editor
