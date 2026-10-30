#pragma once

#include "erhe_scene/pvs_database.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace erhe::scene {
    class Camera;
    class Mesh;
}

namespace editor {

class App_context;
class Scene_id_capture;
class Scene_root;

// One bake as asked for (MCP pvs_bake, later the PVS window). The optional
// grid members override the scene's Scene_settings (pvs_region_min / max,
// pvs_cell_size) for this bake only.
class Pvs_bake_request
{
public:
    erhe::scene::Pvs_bake_method method{erhe::scene::Pvs_bake_method::sampled};
    std::optional<float>         cell_size;
    std::optional<glm::vec3>     region_min;
    std::optional<glm::vec3>     region_max;
    // sampled: viewpoints per cell (the cell center plus random points).
    uint32_t                     samples_per_cell  {9};
    uint32_t                     face_resolution   {512};
    float                        face_fov          {2.0943951f}; // radians (120 degrees)
    // sampled: id captures kept busy at once.
    uint32_t                     captures_in_flight{8};
};

enum class Pvs_bake_state : unsigned int {
    idle = 0,  // no bake has run
    running,
    done,      // the database is set on the scene
    cancelled, // the scene's database is unchanged
    failed     // see Pvs_bake_status::error; the scene's database is unchanged
};

[[nodiscard]] auto c_str(Pvs_bake_state state) -> const char*;

// The grid and column count of a started bake, and its progress.
class Pvs_bake_status
{
public:
    Pvs_bake_state               state             {Pvs_bake_state::idle};
    std::string                  scene_name;
    std::string                  error;
    erhe::scene::Pvs_bake_method method            {erhe::scene::Pvs_bake_method::sampled};
    glm::vec3                    grid_origin       {0.0f};
    float                        cell_size         {0.0f};
    glm::uvec3                   cell_counts       {0u};
    uint32_t                     cell_count        {0};
    uint32_t                     column_count      {0};
    uint32_t                     cells_done        {0};
    uint64_t                     capture_count     {0};
    uint64_t                     captures_done     {0};
    // Captured entries that name no column (not in the static set).
    uint64_t                     unmatched_entries {0};
    // Mesh nodes the bake gave a glTF uid (doc/editor/scene_serialization.md).
    uint32_t                     uids_assigned     {0};
    float                        seconds_elapsed   {0.0f};
    float                        seconds_remaining {0.0f}; // estimate, 0 when unknown
};

// The potentially visible set bake (doc/plans/pvs.md "Bake", "Build order"
// steps 2 and 5; doc/editor/renderers.md "Pvs_baker"): the cell loop shared
// by the bake methods. start_bake() computes the grid, enumerates the static
// set (D3) as columns and allocates the per-cell words; tick() advances the
// bake once per frame from the editor's prepare phase (M2); the last cell
// builds the erhe::scene::Pvs_database and sets it on the scene. Main
// thread only; the GPU work of a capture is recorded by its
// Scene_id_capture_node in the render phase.
//
// The bake is an offline job: while it runs it allocates freely (captures,
// decoded entry lists). With no bake running tick() returns at once and
// nothing is allocated.
class Pvs_baker
{
public:
    explicit Pvs_baker(App_context& context);
    ~Pvs_baker() noexcept;

    Pvs_baker(const Pvs_baker&) = delete;
    auto operator=(const Pvs_baker&) -> Pvs_baker& = delete;

    // Main thread (M2). Empty on success, else why the bake was refused: a
    // bake is running or still releasing its captures, the method is not
    // built, a parameter is out of range, or the scene has no static entry.
    [[nodiscard]] auto start_bake(const std::shared_ptr<Scene_root>& scene_root, const Pvs_bake_request& request) -> std::string;
    // Main thread (M2), once per frame: the running bake's own step.
    void tick();
    // Main thread: a running bake ends cancelled; the scene keeps the
    // database it had.
    void cancel();
    // Main thread: a running bake of the closed scene fails.
    void on_scene_closed(const Scene_root* scene_root);

    [[nodiscard]] auto is_running() const -> bool;
    [[nodiscard]] auto get_status() const -> Pvs_bake_status;

private:
    class Column_source
    {
    public:
        // The mesh the column was made from, held weakly (a closed scene's
        // content is not pinned): a captured entry of another mesh with the
        // same uid is not the column's.
        std::weak_ptr<erhe::scene::Mesh> mesh;
    };

    class Capture_slot
    {
    public:
        std::unique_ptr<Scene_id_capture> capture;
        uint32_t                          cell{0};
        bool                              busy{false};
    };

    void stop             (Pvs_bake_state state, const std::string& error);
    void release_captures ();
    void issue_captures   (const std::shared_ptr<Scene_root>& scene_root);
    void collect_capture  (Capture_slot& slot);
    void finish           (Scene_root& scene_root);
    // Places m_camera at viewpoint `sample` of `cell`, looking along `face`.
    void aim_camera       (uint32_t cell, uint32_t sample, uint32_t face);

    App_context&                              m_context;
    Pvs_bake_status                           m_status;
    Pvs_bake_request                          m_request;
    std::weak_ptr<Scene_root>                 m_scene_root;
    std::chrono::steady_clock::time_point     m_start_time;

    // The bake's grid, columns and per-cell words (cell_count *
    // words_per_cell, zero until a capture sets a bit).
    erhe::scene::Pvs_header                   m_header;
    std::vector<erhe::scene::Pvs_column>      m_columns;
    std::vector<Column_source>                m_column_sources;
    // node uid -> column of each mesh primitive index (untracked_column for
    // a primitive outside the static set).
    std::unordered_map<std::string, std::vector<uint32_t>> m_columns_by_uid;
    uint32_t                                  m_words_per_cell{0};
    std::vector<uint32_t>                     m_cell_words;
    // Captures each cell still waits for; a cell is done at zero.
    std::vector<uint32_t>                     m_cell_captures_left;
    // Bounds of the region and of every column: the faces' far plane.
    glm::vec3                                 m_scene_min{0.0f};
    glm::vec3                                 m_scene_max{0.0f};

    // The next capture to issue: cell, viewpoint (sample) and face.
    uint64_t                                  m_next_capture{0};
    uint32_t                                  m_viewpoint_cell{0xffffffffu};
    std::vector<glm::vec3>                    m_viewpoints;
    std::shared_ptr<erhe::scene::Camera>      m_camera;
    std::vector<Capture_slot>                 m_captures;
};

} // namespace editor
