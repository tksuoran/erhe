#pragma once

#include "parsers/gltf.hpp"
#include "scene/generated/scene_export_settings.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace erhe { class Item_host; }

namespace editor {

class App_context;
class Scene_root;

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
};

class Scene_save_request
{
public:
    std::weak_ptr<Scene_root>       scene_root;
    std::filesystem::path           path;
    Gltf_scene_save_kind            kind          {Gltf_scene_save_kind::scene};
    erhe::gltf::Gltf_texture_export texture_export{erhe::gltf::Gltf_texture_export::source};
    Scene_save_source_binding       source_binding{Scene_save_source_binding::keep};
    // Prepare's settings (doc/editor/world_space_bake.md); null for a plain save.
    std::optional<Scene_export_settings> export_settings{};
    // Main thread, when the save starts running and when it has finished.
    std::function<void()>                         on_started;
    std::function<void(const Scene_save_result&)> on_finished;
};

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
    // Main thread, M2, before Operation_stack::update().
    void update  ();
    // Waits for a running save to finish (editor shutdown).
    void shutdown();

    // True while a save runs: the editor-wide gates hold.
    [[nodiscard]] auto is_active   () const -> bool;
    [[nodiscard]] auto is_read_only(const Scene_root& scene_root) const -> bool;
    [[nodiscard]] auto is_read_only(const erhe::Item_host* item_host) const -> bool;
    [[nodiscard]] auto get_pending_count() const -> std::size_t;

    // Status line: a running save's scene, stage and progress; false when idle.
    [[nodiscard]] auto imgui_status() const -> bool;

private:
    [[nodiscard]] auto can_start() const -> bool;
    void start         (Scene_save_request&& request);
    void finish_running();
    void set_active    (bool active);

    App_context&                               m_context;
    std::deque<Scene_save_request>             m_requests;
    bool                                       m_waiting_logged{false};

    // The running save (main thread owns all of these except m_worker_done).
    Scene_save_request                         m_running_request;
    std::shared_ptr<Scene_root>                m_running_scene_root;
    std::unique_ptr<Gltf_scene_save>           m_running_save;
    std::chrono::steady_clock::time_point      m_running_start_time;
    std::thread                                m_worker;
    bool                                       m_active{false};

    // Written by the worker, read by the main thread.
    mutable std::mutex                         m_worker_mutex;
    bool                                       m_worker_done{false};
    bool                                       m_worker_ok  {false};
};

}
