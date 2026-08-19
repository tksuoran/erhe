#pragma once

#include "assets/asset_load_task.hpp"
#include "parsers/gltf.hpp"

#include "erhe_gltf/gltf.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace erhe::gltf {
    class Image_transfer;
}
namespace erhe::scene {
    class Node;
}

namespace editor {

class App_context;
class Scene_root;

// Asynchronous open of an erhe-authored glTF scene
// (doc/async-asset-loading-plan.md step 6). The phases, and which thread each
// one runs on:
//
//   scan      WORKER  editor::scan_gltf, to decide erhe-authored scene vs
//                     plain glTF asset. It is a whole-file read plus a full
//                     JSON parse, so leaving it on the main thread would
//                     make "a load never blocks the main loop" false for the
//                     primary entry point (plan 2.12).
//   parse     WORKER  erhe::gltf::parse_gltf. Safe off the main thread only
//                     because parse_gltf is device-free: it creates no GPU
//                     object and holds no Device (Gltf_parse_arguments carries
//                     Gltf_device_options by value instead), and it parses
//                     into an UNHOSTED container node - no live Scene - the
//                     way the container and prefab loaders already do.
//   residency MAIN    samplers + textures created, uploads recorded into the
//                     frame command buffer, spread over frames under the
//                     Frame_load_budget.
//   publish   MAIN    finish_open_scene_gltf: Scene_root construction, asset
//                     reference resolution, Buffer_mesh build, content
//                     library / physics / editor-state operations, node
//                     reparenting, raytrace kickoff. One atomic step (plan
//                     2.7 publish-once); it is NOT budgeted yet.
//
// Two modes, chosen by the scan:
//   open_erhe_scene    the file carries ERHE_scene: the task opens it as a
//                      full Scene_root itself, through finish_open_scene_gltf.
//   open_foreign_gltf  a plain glTF asset: the task still does the scan,
//                      parse, build and residency, then hands the finished
//                      parse back so the caller can queue a
//                      Scene_open_operation that consumes it. The operation
//                      stays synchronous and cheap, which is what plan 2.8
//                      asks for - only the loading is async.
//
// Record adoption (plan 2.8): when Asset_manager already holds a container
// record for this path there is nothing to parse. An erhe scene then runs the
// blocking open inline on its first tick; a foreign file hands back with no
// prepared parse so the import operation takes the record's parse.
class Gltf_load_task : public Asset_load_task
{
public:
    Gltf_load_task(
        App_context&                                  context,
        Asset_load_request                            request,
        std::shared_ptr<Asset_load_handle>            handle,
        std::function<void(const Asset_load_result&)> on_done
    );
    ~Gltf_load_task() noexcept override;

    auto tick(Asset_load_tick_context& tick_context) -> Asset_load_state override;
    [[nodiscard]] auto is_worker_idle    () const -> bool override;
    [[nodiscard]] auto get_import_target () const -> std::weak_ptr<Scene_root> override { return m_request.import_target; }

    // The opened scene, once the task reached `done`. Null otherwise.
    [[nodiscard]] auto get_scene_root() const -> const std::shared_ptr<Scene_root>& { return m_scene_root; }

private:
    // Worker output. Held through a shared_ptr so a cancelled task can be
    // destroyed while its worker is still writing (plan 2.3 invariant 4 is
    // enforced by is_worker_idle, this keeps the target alive regardless).
    // Worker output of the scan phase.
    class Scan_result
    {
    public:
        std::atomic<bool> finished{false};
        bool              is_erhe_scene{false};
        bool              ok{false};
    };

    class Parse_result
    {
    public:
        std::atomic<bool>                  finished{false};
        erhe::gltf::Gltf_data              gltf_data;
        std::shared_ptr<erhe::scene::Node> container_node;
        bool                               ok{false};
    };

    // Worker-side Buffer_mesh build (phase 3a). Separate from Parse_result
    // so the two worker stages have independent completion flags.
    class Build_result
    {
    public:
        std::atomic<bool> finished{false};
    };

    enum class Mode : unsigned int {
        open_erhe_scene   = 0,
        open_foreign_gltf = 1,
        // Import into an existing scene. No scan phase: the import machinery
        // takes any glTF, erhe-authored or not. Publish hands the prepared
        // parse back exactly like open_foreign_gltf does; the caller queues
        // the import operation.
        import_into_scene = 2,
        // Load a Prefab_library template. Also no scan; publish hands the
        // parse back and Prefab_library finishes it.
        load_prefab_template = 3
    };

    enum class Phase : unsigned int {
        start     = 0,
        scanning  = 1,
        parsing   = 2,
        building  = 3,
        residency = 4,
        publish   = 5,
        settled   = 6
    };

    void start_scan (Asset_load_tick_context& tick_context);
    void start_parse(Asset_load_tick_context& tick_context);
    void start_build(Asset_load_tick_context& tick_context);
    void notify_done();
    auto advance_residency(Asset_load_tick_context& tick_context) -> bool; // true when residency is complete

    App_context&                                  m_context;
    Asset_load_request                            m_request;
    // Resolved on the main thread at construction: an import uses the target
    // scene's content layer, an open uses the editor-wide constant (mesh
    // layer ids are editor-wide, so parsing before the scene exists is safe).
    std::uint64_t                                 m_mesh_layer_id{0};
    std::function<void(const Asset_load_result&)> m_on_done;
    Phase                         m_phase{Phase::start};
    std::shared_ptr<Scan_result>  m_scan_result;
    std::shared_ptr<Parse_result> m_parse_result;
    // The loader's own Image_transfer, in frame_recording mode: copies go
    // into the frame command buffer, staging comes from the device ring and
    // is reclaimed by frame completion (plan 2.6).
    std::unique_ptr<erhe::gltf::Image_transfer> m_image_transfer;
    bool                          m_samplers_created{false};
    std::shared_ptr<Build_result> m_build_result;
    // Highest LOADER-queue ticket the worker build enqueued. Publish waits
    // until Mesh_memory's loader watermark has reached it, because a
    // budget-drained queue does not guarantee "enqueued implies uploaded"
    // (plan 2.5). Without this gate the scene could appear drawing from
    // vertex/index bytes still sitting in the queue.
    std::uint64_t                 m_build_ticket{0};
    std::shared_ptr<Scene_root>   m_scene_root;
    Mode                          m_mode{Mode::open_erhe_scene};
    bool                          m_foreign_gltf{false};
    std::shared_ptr<Prepared_gltf_parse> m_prepared_parse;
};

} // namespace editor
