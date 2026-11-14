#include "scene/navigation_build_job.hpp"

#include "app_context.hpp"
#include "assets/navigation_item.hpp"
#include "content_library/content_library.hpp"
#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "navigation/make_navigation_build_settings.hpp"
#include "navigation/navigation_build.hpp"
#include "navigation/navigation_mesh.hpp"
#include "scene/navigation_visualization.hpp"
#include "scene/scene_navigation.hpp"
#include "scene/scene_root.hpp"
#include "scene/static_mesh_set.hpp"
#include "config/generated/navigation_settings.hpp"
#include "config/generated/scene_export_settings.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_flags.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor {

namespace {

template <typename F>
void run_parallel(tf::Executor* const executor, const std::size_t count, F&& task)
{
    if ((executor != nullptr) && (count > 1)) {
        tf::Taskflow taskflow;
        for (std::size_t i = 0; i < count; ++i) {
            taskflow.emplace([&task, i]() { task(i); });
        }
        erhe::task::run(*executor, taskflow).wait();
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            task(i);
        }
    }
}

// One primitive shape of the input, read once however many nodes share it.
class Input_shape
{
public:
    std::shared_ptr<erhe::primitive::Primitive> primitive; // keeps the shape alive
    erhe::primitive::Primitive_render_shape*    shape{nullptr};
    erhe::geometry::Geometry*                   geometry{nullptr}; // geometry-normative: its derived triangles
    std::vector<glm::vec3>                      positions;
    std::vector<uint32_t>                       indices;
    Navigation_shape_hash                       content_hash; // of the soup read, or of the derived triangles
    std::string                                 skip_reason;
};

// A primitive without a render shape.
constexpr std::size_t c_no_shape = std::numeric_limits<std::size_t>::max();

class Navigation_build_job : public Scene_service_job
{
public:
    Navigation_build_job(const std::shared_ptr<Scene_root>& scene_root, const Navigation_build_purpose purpose, tf::Executor* executor)
        : m_scene_root  {scene_root}
        , m_scene_name  {scene_root->get_name()}
        , m_title       {
            (purpose == Navigation_build_purpose::export_stage)
                ? fmt::format("Exporting '{}': navigation mesh", scene_root->get_name())
                : fmt::format("Building navigation mesh of '{}'", scene_root->get_name())
        }
        , m_error_prefix{
            (purpose == Navigation_build_purpose::export_stage)
                ? fmt::format("Export of '{}' failed: the navigation mesh build failed", scene_root->get_name())
                : fmt::format("Build Navigation Mesh of '{}' failed", scene_root->get_name())
        }
        , m_executor    {executor}
    {
    }

    [[nodiscard]] auto begin(Scene_root& scene_root) -> std::string;
    [[nodiscard]] auto run() -> bool override;
    void finish(App_context& context, bool ok, Scene_save_result& result) override;
    [[nodiscard]] auto get_progress() const -> Scene_export_progress_state override
    {
        std::lock_guard<std::mutex> lock{m_progress_mutex};
        return m_progress;
    }
    [[nodiscard]] auto get_status_title() const -> const std::string& override { return m_title; }

private:
    void begin_stage(const Scene_export_stage stage, const std::size_t total)
    {
        std::lock_guard<std::mutex> lock{m_progress_mutex};
        m_progress = Scene_export_progress_state{.stage = stage, .done = 0, .total = total};
    }
    void set_progress(const std::size_t done, const std::size_t total)
    {
        std::lock_guard<std::mutex> lock{m_progress_mutex};
        m_progress.done  = done;
        m_progress.total = total;
    }
    void advance()
    {
        std::lock_guard<std::mutex> lock{m_progress_mutex};
        ++m_progress.done;
    }
    [[nodiscard]] auto fail(const std::string& reason) -> bool
    {
        m_error = fmt::format("{}: {}", m_error_prefix, reason);
        log_navigation->warn("{}", m_error);
        return false;
    }
    [[nodiscard]] auto read_triangles() -> bool;
    [[nodiscard]] auto flood(const Navigation_mesh& mesh) -> bool;

    std::weak_ptr<Scene_root>           m_scene_root;
    std::string                         m_scene_name;
    std::string                         m_title;
    std::string                         m_error_prefix;
    tf::Executor*                       m_executor{nullptr};
    Navigation_build_settings           m_settings;
    std::vector<Navigation_seed>        m_seeds;
    std::vector<glm::vec3>              m_seed_points;
    std::vector<Navigation_input_entry> m_input;
    std::vector<std::string>            m_input_names;           // parallel to m_input
    std::vector<std::vector<std::size_t>> m_input_shape_indices; // per input entry and primitive, into m_shapes; c_no_shape for none
    std::vector<Input_shape>            m_shapes;
    std::unordered_map<const erhe::geometry::Geometry*, std::unique_ptr<std::mutex>> m_geometry_mutexes;
    // Set: the installed mesh was built from this input with these settings, so only the flood runs.
    std::shared_ptr<Navigation_mesh>    m_installed_mesh;

    std::shared_ptr<Navigation_mesh>    m_mesh;
    std::vector<Navigation_seed_result> m_seed_results;
    std::vector<uint8_t>                m_reachable;
    Navigation_build_statistics         m_statistics;
    std::string                         m_error;

    mutable std::mutex                  m_progress_mutex;
    Scene_export_progress_state         m_progress{.stage = Scene_export_stage::navigation_triangles};
};

auto Navigation_build_job::begin(Scene_root& scene_root) -> std::string
{
    ERHE_PROFILE_FUNCTION();

    const std::string refusal = get_navigation_build_refusal(scene_root);
    if (!refusal.empty()) {
        return refusal;
    }
    m_settings = make_navigation_build_settings(scene_root.get_scene_settings().export_settings->navigation);
    scene_root.get_navigation().collect_current_seeds(m_seeds);
    for (const Navigation_seed& seed : m_seeds) {
        m_seed_points.push_back(seed.point);
    }

    // Level 0 of every primitive of every input mesh.
    std::unordered_set<const erhe::scene::Node*> animated_nodes;
    const std::shared_ptr<Content_library> content_library = scene_root.get_content_library();
    if (content_library) {
        collect_animated_nodes(*content_library, animated_nodes);
    }
    std::unordered_map<const erhe::primitive::Primitive_render_shape*, std::size_t> shape_indices;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root.item_host_mutex};
        const std::shared_ptr<erhe::scene::Node> root = scene_root.get_scene().get_root_node();
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            if (!is_navigation_input_mesh(*mesh, animated_nodes)) {
                continue;
            }
            Navigation_input_entry entry{};
            entry.mesh_id = mesh->get_id();
            entry.uid     = mesh->get_gltf_uid();
            // The formula Scene_navigation::root_relative() compares with.
            entry.root_from_node = root ? (glm::inverse(root->world_from_node()) * mesh->world_from_node()) : mesh->world_from_node();
            std::vector<std::size_t> primitive_shapes;
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                erhe::primitive::Primitive_render_shape* const shape = mesh_primitive.primitive ? mesh_primitive.primitive->render_shape.get() : nullptr;
                entry.shapes.push_back(mesh_primitive.primitive ? std::shared_ptr<const erhe::primitive::Primitive_render_shape>{mesh_primitive.primitive->render_shape} : nullptr);
                if (shape == nullptr) {
                    primitive_shapes.push_back(c_no_shape);
                    continue;
                }
                const auto [i, inserted] = shape_indices.try_emplace(shape, m_shapes.size());
                if (inserted) {
                    Input_shape input_shape{};
                    input_shape.primitive = mesh_primitive.primitive;
                    input_shape.shape     = shape;
                    if (!shape->has_triangle_soup()) {
                        input_shape.geometry = shape->get_geometry_const().get();
                        if (input_shape.geometry != nullptr) {
                            m_geometry_mutexes.try_emplace(input_shape.geometry, std::make_unique<std::mutex>());
                        }
                    }
                    m_shapes.push_back(std::move(input_shape));
                }
                primitive_shapes.push_back(i->second);
            }
            m_input.push_back(std::move(entry));
            m_input_names.push_back(mesh->get_name());
            m_input_shape_indices.push_back(std::move(primitive_shapes));
        }
    }

    // Geometry and build settings as the installed mesh was built: the flood alone runs again (seeds or search extents).
    std::vector<Navigation_input_entry> installed_input;
    Scene_navigation& navigation = scene_root.get_navigation();
    std::shared_ptr<Navigation_mesh> installed_mesh = navigation.get_mesh();
    navigation.get_input(installed_input);
    if (
        installed_mesh &&
        are_navigation_inputs_equal(m_input, installed_input) &&
        are_navigation_build_settings_equal(m_settings, navigation.get_settings())
    ) {
        m_installed_mesh = std::move(installed_mesh);
    }
    log_navigation->info(
        "Build Navigation Mesh of '{}': {} input meshes, {} primitive shapes, {} seeds{}",
        m_scene_name, m_input.size(), m_shapes.size(), m_seeds.size(),
        m_installed_mesh ? " - geometry and build settings unchanged, the flood alone runs" : ""
    );
    return {};
}

auto Navigation_build_job::read_triangles() -> bool
{
    ERHE_PROFILE_FUNCTION();
    begin_stage(Scene_export_stage::navigation_triangles, m_shapes.size());
    run_parallel(m_executor, m_shapes.size(), [this](const std::size_t i) {
        Input_shape& input_shape = m_shapes[i];
        std::shared_ptr<erhe::primitive::Triangle_soup> soup;
        if (input_shape.shape->has_triangle_soup()) {
            // Resident, or read back from its file.
            soup = input_shape.shape->get_triangle_soup();
            if (!soup) {
                input_shape.skip_reason = "its triangle soup cannot be read";
            }
        } else if (input_shape.geometry == nullptr) {
            input_shape.skip_reason = "it has neither a triangle soup nor a Geometry";
        } else {
            // Deriving writes the Geometry's facet ids: one build of a Geometry at a time.
            std::lock_guard<std::mutex> geometry_lock{*m_geometry_mutexes.at(input_shape.geometry)};
            erhe::primitive::Derived_triangle_soup derived = erhe::primitive::derive_triangle_soup(*input_shape.shape);
            soup = std::move(derived.soup);
            if (!soup) {
                input_shape.skip_reason = std::move(derived.refusal);
            }
        }
        if (soup) {
            // The hash the saved item names the shape by (doc/editor/navigation.md "Saving"); a geometry-normative
            // shape's is that of its derived triangles, which this job holding the scene read-only may set.
            std::optional<erhe::hash::Content_hash> content_hash = input_shape.shape->get_content_hash();
            if (!content_hash.has_value()) {
                content_hash = erhe::primitive::compute_triangle_soup_content_hash(*soup);
                input_shape.shape->set_content_hash(content_hash.value());
            }
            input_shape.content_hash = Navigation_shape_hash{.known = true, .bytes = content_hash->bytes};
            if (soup->primitive_type != erhe::primitive::Primitive_type::triangles) {
                input_shape.skip_reason = "its soup is not a triangle list";
            } else {
                const std::vector<float> positions = erhe::primitive::get_float_positions(*soup);
                if (positions.empty() && (soup->get_vertex_count() > 0)) {
                    input_shape.skip_reason = "its positions are not vec3 float";
                } else {
                    input_shape.positions.resize(positions.size() / 3);
                    std::memcpy(input_shape.positions.data(), positions.data(), input_shape.positions.size() * sizeof(glm::vec3));
                    input_shape.indices = soup->index_data; // level 0: the stored LOD levels are in lod_index_data
                }
            }
        }
        advance();
    });

    // Per node and primitive, positions in the scene root's space; a mirroring transform flips the winding.
    std::vector<std::vector<glm::vec3>> positions;
    std::vector<std::vector<uint32_t>>  indices;
    for (std::size_t entry_index = 0, end = m_input.size(); entry_index < end; ++entry_index) {
        const Navigation_input_entry& entry      = m_input[entry_index];
        const glm::mat4&              transform  = entry.root_from_node;
        const bool                    mirrored   = (glm::determinant(glm::mat3{transform}) < 0.0f);
        std::size_t                   triangles  = 0;
        for (const std::size_t shape_index : m_input_shape_indices[entry_index]) {
            if (shape_index == c_no_shape) {
                m_input[entry_index].shape_hashes.push_back(Navigation_shape_hash{});
                continue;
            }
            const Input_shape& input_shape = m_shapes[shape_index];
            m_input[entry_index].shape_hashes.push_back(input_shape.content_hash);
            if (input_shape.indices.size() < 3) {
                if (!input_shape.skip_reason.empty()) {
                    log_navigation->warn("Build Navigation Mesh of '{}': a primitive of '{}' has no triangles: {}", m_scene_name, m_input_names[entry_index], input_shape.skip_reason);
                }
                continue;
            }
            std::vector<glm::vec3>& out_positions = positions.emplace_back();
            out_positions.reserve(input_shape.positions.size());
            for (const glm::vec3& p : input_shape.positions) {
                out_positions.push_back(glm::vec3{transform * glm::vec4{p, 1.0f}});
            }
            std::vector<uint32_t>& out_indices = indices.emplace_back(input_shape.indices);
            out_indices.resize(out_indices.size() - (out_indices.size() % 3));
            if (mirrored) {
                for (std::size_t t = 0; t + 2 < out_indices.size(); t += 3) {
                    std::swap(out_indices[t + 1], out_indices[t + 2]);
                }
            }
            triangles += out_indices.size() / 3;
        }
        m_statistics.input_triangle_count += triangles;
        if (triangles == 0) {
            ++m_statistics.skipped_mesh_count;
            m_statistics.skipped_meshes.push_back(m_input_names[entry_index]);
            log_navigation->warn("Build Navigation Mesh of '{}': mesh '{}' yields no triangles and is left out", m_scene_name, m_input_names[entry_index]);
        }
    }
    m_statistics.input_mesh_count  = m_input.size();
    m_statistics.input_shape_count = m_shapes.size();
    m_shapes.clear(); // the soups are copied; the shapes need not stay alive past the read

    Navigation_input navigation_input;
    for (std::size_t i = 0, end = positions.size(); i < end; ++i) {
        navigation_input.meshes.push_back(Navigation_input_mesh{.positions = positions[i], .triangle_indices = indices[i]});
    }
    if (navigation_input.meshes.empty()) {
        return fail("no input mesh yields triangles");
    }

    ERHE_VERIFY(m_executor != nullptr);
    begin_stage(Scene_export_stage::navigation_tiles, 0);
    std::string error;
    std::unique_ptr<Navigation_mesh> mesh = build_navigation_mesh(
        navigation_input, m_settings, *m_executor,
        [this](const std::size_t tiles_done, const std::size_t tiles_total) {
            set_progress(tiles_done, tiles_total);
        },
        error
    );
    if (!mesh) {
        return fail(error);
    }
    if (mesh->get_polygons().empty()) {
        return fail("the build made no polygon (no walkable surface in the input)");
    }
    m_statistics.tile_count = mesh->get_tile_count();
    m_mesh = std::move(mesh);
    return true;
}

auto Navigation_build_job::flood(const Navigation_mesh& mesh) -> bool
{
    begin_stage(Scene_export_stage::navigation_flood, 1);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const bool found = mesh.find_reachable(m_seed_points, m_settings.get_seed_search_half_extents(), m_seed_results, m_reachable);
    m_statistics.flood_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    advance();
    if (!found) {
        std::string reasons;
        for (std::size_t i = 0, end = m_seed_results.size(); i < end; ++i) {
            const Navigation_seed_result& result = m_seed_results[i];
            reasons += fmt::format(
                "{}seed '{}' ({}, {}, {}): {}", reasons.empty() ? "" : "; ",
                (i < m_seeds.size()) ? m_seeds[i].name : std::string{"?"}, result.seed.x, result.seed.y, result.seed.z, result.reason
            );
        }
        return fail("every seed failed: " + reasons);
    }
    return true;
}

auto Navigation_build_job::run() -> bool
{
    ERHE_PROFILE_FUNCTION();
    if (m_installed_mesh) {
        return flood(*m_installed_mesh);
    }
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    if (!read_triangles()) {
        return false;
    }
    m_statistics.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!flood(*m_mesh)) {
        return false;
    }
    m_mesh->set_reachable(m_reachable);
    return true;
}

void Navigation_build_job::finish(App_context& context, const bool ok, Scene_save_result& result)
{
    result.saved = false;
    const std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (!scene_root) {
        result.error = fmt::format("{}: the scene was closed", m_error_prefix);
        return;
    }
    if (!ok) {
        // The installed mesh stays as it was.
        result.error = m_error.empty() ? fmt::format("{} (details in logs/log.txt)", m_error_prefix) : m_error;
        return;
    }
    Scene_navigation& navigation = scene_root->get_navigation();
    // Built here, on the main thread with the upload command buffer recording: the flood changes which polygons each mesh holds.
    std::shared_ptr<erhe::scene::Node> visualization = make_navigation_visualization(context, m_installed_mesh ? *m_installed_mesh : *m_mesh, m_reachable);
    if (m_installed_mesh) {
        if (!navigation.install_flood(m_installed_mesh, m_reachable, std::move(m_seed_results), m_settings, std::move(m_seeds), m_statistics.flood_ms, std::move(visualization))) {
            result.error = fmt::format("{}: the navigation mesh was replaced while its flood ran", m_error_prefix);
            log_navigation->warn("{}", result.error);
            return;
        }
    } else {
        navigation.install(m_mesh, std::move(m_seed_results), m_settings, std::move(m_seeds), std::move(m_input), std::move(m_statistics), std::move(visualization));
    }
    // The flood changes the reachable flags, so a flood alone is stored too.
    static_cast<void>(store_navigation_item(context, *scene_root, scene_root->get_source_path(), scene_root->get_editor_asset_record()));
    result.saved = true;
    log_navigation->info("Build Navigation Mesh of '{}' ({}): {}", m_scene_name, m_installed_mesh ? "flood" : "build", navigation.get_status_text());
}

} // anonymous namespace

auto get_navigation_build_refusal(const Scene_root& scene_root) -> std::string
{
    const char* const action = get_export_action(Scene_export_mode::build_navigation);
    const std::string settings_refusal = get_export_settings_refusal(scene_root, action);
    if (!settings_refusal.empty()) {
        return settings_refusal;
    }
    if (scene_root.get_navigation_seed_count() == 0) {
        return fmt::format("{} of '{}' refused: the scene has no navigation seed - mark a node (Properties > Navigation Seed)", action, scene_root.get_name());
    }
    return {};
}

auto make_navigation_build_job(
    App_context&                       context,
    const std::shared_ptr<Scene_root>& scene_root,
    const Navigation_build_purpose     purpose,
    std::string&                       out_refusal
) -> std::shared_ptr<Scene_service_job>
{
    std::shared_ptr<Navigation_build_job> job = std::make_shared<Navigation_build_job>(scene_root, purpose, context.executor);
    out_refusal = job->begin(*scene_root);
    if (!out_refusal.empty()) {
        return {};
    }
    return job;
}

}
