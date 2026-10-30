#include "renderers/pvs_baker.hpp"
#include "renderers/pvs_bake_node.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_id_capture.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/gltf_uid.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/pvs_disocclusion.hpp"
#include "erhe_scene_renderer/pvs_layers.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <functional>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <unordered_set>

namespace editor {

namespace {

// Face directions and their camera up vectors: +X -X +Y -Y +Z -Z.
constexpr std::array<glm::vec3, 6> c_face_directions{
    glm::vec3{ 1.0f,  0.0f,  0.0f},
    glm::vec3{-1.0f,  0.0f,  0.0f},
    glm::vec3{ 0.0f,  1.0f,  0.0f},
    glm::vec3{ 0.0f, -1.0f,  0.0f},
    glm::vec3{ 0.0f,  0.0f,  1.0f},
    glm::vec3{ 0.0f,  0.0f, -1.0f}
};
constexpr std::array<glm::vec3, 6> c_face_ups{
    glm::vec3{0.0f, 1.0f,  0.0f},
    glm::vec3{0.0f, 1.0f,  0.0f},
    glm::vec3{0.0f, 0.0f, -1.0f},
    glm::vec3{0.0f, 0.0f,  1.0f},
    glm::vec3{0.0f, 1.0f,  0.0f},
    glm::vec3{0.0f, 1.0f,  0.0f}
};
constexpr uint32_t c_face_count = 6;

// The near plane of a sampled capture: a viewpoint is never inside an
// entry by more than this (geometry closer than it is clipped).
constexpr float c_sample_z_near = 0.01f;

// The grid's cell count is a uint32 and the per-cell words live in memory.
constexpr uint64_t c_max_cell_count = uint64_t{1} << 24;

[[nodiscard]] auto is_finite(const glm::vec3& value) -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The node or an ancestor carries a rigid body whose motion mode is not
// static: the body moves the node (a subtree below a body is carried by it).
[[nodiscard]] auto is_moved_by_physics(const erhe::scene::Node& node) -> bool
{
    const erhe::scene::Node* current = &node;
    std::shared_ptr<erhe::scene::Node> parent;
    while (current != nullptr) {
        const std::optional<Node_physics_data> physics = read_node_physics(*current);
        if (
            physics.has_value() &&
            (physics->motion_mode != erhe::physics::Motion_mode::e_none) &&
            (physics->motion_mode != erhe::physics::Motion_mode::e_static)
        ) {
            return true;
        }
        parent  = current->get_parent_node();
        current = parent.get();
    }
    return false;
}

// The node or an ancestor is the target of an animation channel.
[[nodiscard]] auto is_animated(const erhe::scene::Node& node, const std::unordered_set<const erhe::scene::Node*>& animated_nodes) -> bool
{
    if (animated_nodes.empty()) {
        return false;
    }
    const erhe::scene::Node* current = &node;
    std::shared_ptr<erhe::scene::Node> parent;
    while (current != nullptr) {
        if (animated_nodes.contains(current)) {
            return true;
        }
        parent  = current->get_parent_node();
        current = parent.get();
    }
    return false;
}

} // anonymous namespace

auto c_str(const Pvs_bake_state state) -> const char*
{
    switch (state) {
        case Pvs_bake_state::idle:      return "idle";
        case Pvs_bake_state::running:   return "running";
        case Pvs_bake_state::done:      return "done";
        case Pvs_bake_state::cancelled: return "cancelled";
        case Pvs_bake_state::failed:    return "failed";
        default:                        return "?";
    }
}

Pvs_baker::Pvs_baker(App_context& context)
    : m_context{context}
{
}

Pvs_baker::~Pvs_baker() noexcept = default;

auto Pvs_baker::is_running() const -> bool
{
    return m_status.state == Pvs_bake_state::running;
}

auto Pvs_baker::get_summary() const -> Pvs_bake_summary
{
    Pvs_bake_summary summary{
        .state                      = m_status.state,
        .method                     = m_status.method,
        .cell_count                 = m_status.cell_count,
        .cells_done                 = m_status.cells_done,
        .column_count               = m_status.column_count,
        .cells_with_pvs             = m_status.cells_with_pvs,
        .cells_without_sample_point = m_status.cells_without_sample_point,
        .sample_point_test          = m_status.sample_point_test,
        .seconds_elapsed            = m_status.seconds_elapsed,
        .seconds_remaining          = m_status.seconds_remaining,
        .gpu_times                  = m_bake_node ? m_bake_node->get_gpu_times() : m_status.gpu_times
    };
    if (summary.state == Pvs_bake_state::running) {
        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - m_start_time;
        summary.seconds_elapsed = elapsed.count();
        if ((m_status.captures_done > 0) && (m_status.capture_count > m_status.captures_done)) {
            const double per_capture = static_cast<double>(summary.seconds_elapsed) / static_cast<double>(m_status.captures_done);
            summary.seconds_remaining = static_cast<float>(per_capture * static_cast<double>(m_status.capture_count - m_status.captures_done));
        }
    }
    return summary;
}

auto Pvs_baker::get_status() const -> Pvs_bake_status
{
    Pvs_bake_status status = m_status;
    if (m_bake_node) {
        status.gpu_times = m_bake_node->get_gpu_times();
    }
    if (status.state == Pvs_bake_state::running) {
        const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - m_start_time;
        status.seconds_elapsed = elapsed.count();
        if ((status.captures_done > 0) && (status.capture_count > status.captures_done)) {
            const double per_capture = static_cast<double>(status.seconds_elapsed) / static_cast<double>(status.captures_done);
            status.seconds_remaining = static_cast<float>(per_capture * static_cast<double>(status.capture_count - status.captures_done));
        }
    }
    return status;
}

auto Pvs_baker::start_bake(const std::shared_ptr<Scene_root>& scene_root, const Pvs_bake_request& request) -> std::string
{
    return begin(scene_root, request, Bake_mode::bake);
}

auto Pvs_baker::start_debug_face(
    const std::shared_ptr<Scene_root>& scene_root,
    const Pvs_bake_request&            request,
    const Pvs_debug_face_request&      debug
) -> std::string
{
    if (request.method != erhe::scene::Pvs_bake_method::disocclusion) {
        return "the debug face is a disocclusion face";
    }
    if (debug.face >= c_face_count) {
        return "face must be in [0, 5] (+X -X +Y -Y +Z -Z)";
    }
    if (debug.layer >= request.layer_count) {
        return fmt::format("layer must be in [0, {})", request.layer_count);
    }
    if (debug.path.empty()) {
        return "no path";
    }
    m_debug = debug;
    return begin(scene_root, request, Bake_mode::debug_face);
}

auto Pvs_baker::begin(const std::shared_ptr<Scene_root>& scene_root, const Pvs_bake_request& request, const Bake_mode mode) -> std::string
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    if (m_status.state == Pvs_bake_state::running) {
        return fmt::format("a bake of scene '{}' is running", m_status.scene_name);
    }
    if (!m_captures.empty() || m_retiring_node) {
        return "the previous bake is still releasing its GPU work; try again in a few frames";
    }
    if (!scene_root) {
        return "no scene";
    }
    if ((m_context.graphics_device == nullptr) || (m_context.rendergraph == nullptr) || (m_context.id_renderer == nullptr)) {
        return "rendering is not available";
    }
    if ((request.samples_per_cell < 1) || (request.samples_per_cell > 1024)) {
        return "samples_per_cell must be in [1, 1024]";
    }
    if ((request.face_resolution < 1) || (request.face_resolution > 8192)) {
        return "face_resolution must be in [1, 8192]";
    }
    if (!(request.face_fov > 0.0f) || !(request.face_fov < glm::radians(179.0f))) {
        return "face_fov must be in (0, 179) degrees";
    }
    if ((request.captures_in_flight < 1) || (request.captures_in_flight > 64)) {
        return "captures_in_flight must be in [1, 64]";
    }
    const bool disocclusion = (request.method == erhe::scene::Pvs_bake_method::disocclusion);
    if (disocclusion) {
        if ((request.layer_count < 1) || (request.layer_count > 64)) {
            return "layer_count must be in [1, 64]";
        }
        if ((request.tile_size < 1) || (request.tile_size > request.face_resolution)) {
            return "tile_size must be in [1, face_resolution]";
        }
        if ((request.face_resolution % request.tile_size) != 0) {
            return "face_resolution must be a multiple of tile_size";
        }
        if ((request.samples_per_face < 1) || (request.samples_per_face > 64)) {
            return "samples_per_face must be in [1, 64]";
        }
        // The layered buffer's words are indexed with 32 bits.
        const uint64_t region_words = uint64_t{request.face_resolution} * uint64_t{request.face_resolution} * uint64_t{request.layer_count};
        if ((3 * region_words) >= (uint64_t{1} << 32)) {
            return "face_resolution^2 * layer_count is too large for the layered buffer";
        }
        if ((m_context.forward_renderer == nullptr) || (m_context.program_interface == nullptr)) {
            return "rendering is not available";
        }
        if (!m_context.graphics_device->get_info().fragment_stores_and_atomics) {
            return "the device has no fragment shader storage writes and atomics, which the layered buffer needs";
        }
        const erhe::scene_renderer::Draw_list_scene* draw_list_scene = scene_root->get_draw_list_scene();
        if ((draw_list_scene == nullptr) || !draw_list_scene->supports_pvs_faces()) {
            return "the scene's draw lists cannot draw PVS faces on this device (they need first_instance record indexing)";
        }
    } else if (mode != Bake_mode::bake) {
        return "the debug face is a disocclusion face";
    }

    const Scene_settings& settings  = scene_root->get_scene_settings();
    const float           cell_size = request.cell_size.value_or(settings.pvs_cell_size);
    if (!std::isfinite(cell_size) || !(cell_size > 0.0f)) {
        return "cell size must be positive and finite";
    }

    // The targets of every animation of the scene, before the scene walk
    // (the content library has its own mutex).
    std::unordered_set<const erhe::scene::Node*> animated_nodes;
    const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
    if (content_library) {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library->mutex};
        for (const std::shared_ptr<erhe::scene::Animation>& animation : content_library->get_all<erhe::scene::Animation>()) {
            for (const erhe::scene::Animation_channel& channel : animation->channels) {
                const std::shared_ptr<erhe::scene::Xformable> target = erhe::scene::get_target_node(channel);
                if (target) {
                    animated_nodes.insert(target.get());
                }
            }
        }
    }

    // The static set (doc/plans/pvs.md D3), one column per (mesh, primitive).
    std::vector<erhe::scene::Pvs_column>                   columns;
    std::vector<Column_source>                             column_sources;
    std::unordered_map<std::string, std::vector<uint32_t>> columns_by_uid;
    erhe::math::Aabb                                       static_bounds{};
    uint32_t                                               uids_assigned{0};
    // Every static mesh has raytrace geometry (the free sample point test,
    // doc/plans/pvs.md "Bake" 1, sees every static entry).
    bool                                                   all_raytraced{true};
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};

        // Identifiers a generated uid must not collide with: the uids and
        // names of every node (the rule of the glTF writer's uid pass).
        std::unordered_set<std::string> used_identifiers;
        scene_root->get_scene().for_each_node(
            [&used_identifiers](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
                if (!node->get_gltf_uid().empty()) {
                    used_identifiers.insert(node->get_gltf_uid());
                }
                used_identifiers.insert(node->get_name());
                return true;
            }
        );
        std::mt19937_64 uid_rng{std::random_device{}()};

        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root->layers().content()->meshes) {
            if ((mesh->get_flag_bits() & erhe::Item_flags::content) == 0) {
                continue;
            }
            // Visible at bake time: the mesh's visible flag bit is its
            // effective visibility (an ancestor's hide reaches it through
            // the inherited visible property), the bit the color passes'
            // filters test on the draw-list entries. A hidden entry is
            // drawn by neither method, so a column for it would be set in no
            // cell and showing it later would cull it everywhere; without a
            // column it is untracked (always kept), and it occluded nothing
            // at bake, which is conservative.
            if ((mesh->get_flag_bits() & erhe::Item_flags::visible) == 0) {
                continue;
            }
            if (mesh->skin) {
                continue; // no bounds: posed by its joints
            }
            const erhe::math::Aabb world_aabb = mesh->get_aabb_world();
            if (!world_aabb.is_valid_3d()) {
                continue;
            }
            if (is_moved_by_physics(*mesh) || is_animated(*mesh, animated_nodes)) {
                continue;
            }
            const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
            std::vector<uint32_t> primitive_columns(mesh_primitives.size(), erhe::scene::Pvs_database::untracked_column);
            bool any_column = false;
            for (std::size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
                const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
                if (!mesh_primitive.primitive) {
                    continue;
                }
                if (erhe::primitive::get_blending_class(mesh_primitive.material.get()) == erhe::primitive::Blending_class::translucent) {
                    continue; // never baked: always visible, never occludes
                }
                primitive_columns[primitive_index] = static_cast<uint32_t>(columns.size());
                any_column = true;
                columns.push_back(erhe::scene::Pvs_column{.primitive_index = static_cast<uint32_t>(primitive_index)});
                column_sources.push_back(Column_source{.mesh = mesh});
            }
            if (!any_column) {
                continue;
            }

            // Columns are keyed by the node's glTF uid; a node without one
            // gets it now, as its first export would.
            if (mesh->get_gltf_uid().empty()) {
                std::string uid;
                do {
                    uid = erhe::gltf::generate_gltf_uid(uid_rng);
                } while (used_identifiers.contains(uid));
                used_identifiers.insert(uid);
                mesh->set_gltf_uid(uid);
                ++uids_assigned;
            }
            const std::string& uid = mesh->get_gltf_uid();

            // The extraction the staleness rule compares with
            // (Draw_list_scene, doc/plans/pvs.md D4).
            const std::array<float, 12> world_transform = erhe::scene::make_pvs_world_transform(mesh->world_from_node());
            const std::array<float, 6>  world_bounds    = erhe::scene::make_pvs_world_bounds(world_aabb);
            const uint64_t transform_hash = erhe::scene::compute_pvs_transform_hash(world_transform, world_bounds);
            for (const uint32_t column_index : primitive_columns) {
                if (column_index == erhe::scene::Pvs_database::untracked_column) {
                    continue;
                }
                erhe::scene::Pvs_column& column = columns[column_index];
                column.node_uid        = uid;
                column.world_transform = world_transform;
                column.world_bounds    = world_bounds;
                column.transform_hash  = transform_hash;
            }
            static_bounds.include(world_aabb);
            if ((mesh->get_rt_scene() == nullptr) || mesh->get_rt_primitives().empty()) {
                all_raytraced = false;
            }
            const auto [it, inserted] = columns_by_uid.emplace(uid, std::move(primitive_columns));
            if (!inserted) {
                // Two meshes with one uid (an import that did not make it
                // unique): the database cannot name both.
                return fmt::format("meshes '{}' share the glTF uid '{}'", mesh->get_name(), uid);
            }
        }
    }
    if (columns.empty()) {
        return "the scene has no static entries (doc/plans/pvs.md D3)";
    }

    // The region: the request, else the scene settings, else (both corners
    // zero = unset) the union of the static entries' world bounds.
    glm::vec3 region_min = request.region_min.value_or(settings.pvs_region_min);
    glm::vec3 region_max = request.region_max.value_or(settings.pvs_region_max);
    if (!request.region_min.has_value() && !request.region_max.has_value() && (region_min == glm::vec3{0.0f}) && (region_max == glm::vec3{0.0f})) {
        region_min = static_bounds.min;
        region_max = static_bounds.max;
    }
    if (!is_finite(region_min) || !is_finite(region_max) || glm::any(glm::greaterThanEqual(region_min, region_max))) {
        return fmt::format(
            "the region min ({}, {}, {}) must be below its max ({}, {}, {}) on every axis",
            region_min.x, region_min.y, region_min.z, region_max.x, region_max.y, region_max.z
        );
    }
    const glm::vec3  extent = region_max - region_min;
    const glm::uvec3 cell_counts{
        static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.x / cell_size))),
        static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.y / cell_size))),
        static_cast<uint32_t>(std::max(1.0f, std::ceil(extent.z / cell_size)))
    };
    const uint64_t cell_count = uint64_t{cell_counts.x} * uint64_t{cell_counts.y} * uint64_t{cell_counts.z};
    if (cell_count > c_max_cell_count) {
        return fmt::format("{} x {} x {} cells is more than {}; use a larger cell size or a smaller region", cell_counts.x, cell_counts.y, cell_counts.z, c_max_cell_count);
    }

    if ((request.trace_scope == Pvs_trace_scope::traced_cell_only) && !request.trace_cell.has_value()) {
        return "trace_only needs trace_cell";
    }
    if (request.trace_face.has_value() && !request.trace_cell.has_value()) {
        return "trace_face needs trace_cell";
    }
    if (request.trace_cell.has_value() && (!disocclusion || (request.trace_cell.value() >= cell_count))) {
        return fmt::format("trace_cell must be a cell in [0, {}) of a disocclusion bake", cell_count);
    }
    if ((mode == Bake_mode::debug_face) && (m_debug.cell >= cell_count)) {
        return fmt::format("cell must be in [0, {})", cell_count);
    }
    // Captures per cell (sampled); one cell job (every face pass) per cell
    // (disocclusion).
    const uint32_t passes_per_cell = disocclusion ? 1u : (request.samples_per_cell * c_face_count);

    // Start.
    m_request        = request;
    m_scene_root     = scene_root;
    m_start_time     = std::chrono::steady_clock::now();
    m_header         = erhe::scene::Pvs_header{};
    m_header.scene_id    = scene_root->get_scene_id();
    m_header.grid_origin = region_min;
    m_header.cell_size   = cell_size;
    m_header.cell_counts = cell_counts;
    m_header.page_size   = erhe::scene::Pvs_header::max_page_size;
    m_header.bake_parameters = erhe::scene::Pvs_bake_parameters{
        .method           = request.method,
        .face_resolution  = request.face_resolution,
        .tile_size        = disocclusion ? request.tile_size : 0,
        .layer_count      = disocclusion ? request.layer_count : 0,
        .face_fov         = request.face_fov,
        .samples_per_face = disocclusion ? request.samples_per_face : 1,
        .silhouette_tests = disocclusion && (request.silhouette_tests == erhe::scene_renderer::Pvs_silhouette_tests::on)
    };
    m_columns        = std::move(columns);
    m_column_sources = std::move(column_sources);
    m_columns_by_uid = std::move(columns_by_uid);
    m_trace_faces.clear();
    m_words_per_cell = static_cast<uint32_t>((m_columns.size() + 31) / 32);
    m_cell_words.assign(static_cast<std::size_t>(cell_count) * m_words_per_cell, 0u);
    m_cell_captures_left.assign(static_cast<std::size_t>(cell_count), passes_per_cell);
    m_cell_has_pvs.assign(static_cast<std::size_t>(cell_count), uint8_t{0});
    m_mode = mode;
    m_scene_min = glm::min(region_min, static_bounds.min);
    m_scene_max = glm::max(region_max, static_bounds.max);
    m_next_capture    = 0;
    m_sample_point_test = disocclusion && all_raytraced;
    m_viewpoint_cell  = 0xffffffffu;
    m_viewpoints.clear();
    m_camera = std::make_shared<erhe::scene::Camera>("PVS bake camera");
    m_camera->set_projection_type(erhe::scene::Projection::Type::perspective_vertical);
    m_camera->set_fov_y          (request.face_fov);
    m_camera->set_perspective_z_near(c_sample_z_near);

    m_status = Pvs_bake_status{
        .state         = Pvs_bake_state::running,
        .scene_name    = scene_root->get_name(),
        .error         = {},
        .method        = request.method,
        .grid_origin   = region_min,
        .cell_size     = cell_size,
        .cell_counts   = cell_counts,
        .cell_count    = static_cast<uint32_t>(cell_count),
        .column_count  = static_cast<uint32_t>(m_columns.size()),
        .capture_count =
            (mode == Bake_mode::debug_face) ? uint64_t{1} :
            (request.trace_scope == Pvs_trace_scope::traced_cell_only) ? uint64_t{passes_per_cell} :
            (cell_count * uint64_t{passes_per_cell}),
        .uids_assigned = uids_assigned
    };
    m_status.sample_point_test = m_sample_point_test;
    if (request.trace_cell.has_value()) {
        // The traced entries' columns (entries outside the static set have
        // none and are left out).
        Pvs_bake_trace& trace = m_status.trace;
        trace.valid = true;
        trace.cell  = request.trace_cell.value();
        for (const std::pair<std::string, uint32_t>& entry : request.trace_entries) {
            const std::unordered_map<std::string, std::vector<uint32_t>>::const_iterator it = m_columns_by_uid.find(entry.first);
            if ((it == m_columns_by_uid.end()) || (entry.second >= it->second.size()) || (it->second[entry.second] == erhe::scene::Pvs_database::untracked_column)) {
                continue;
            }
            trace.node_uids.push_back(entry.first);
            trace.primitive_indices.push_back(entry.second);
            trace.columns.push_back(it->second[entry.second]);
        }
        trace.in_cell_pvs.assign(trace.columns.size(), uint8_t{0});
    }
    if (mode == Bake_mode::debug_face) {
        m_status.debug_face       = true;
        m_status.debug_cell       = m_debug.cell;
        m_status.debug_face_index = m_debug.face;
        m_status.debug_layer      = m_debug.layer;
    }

    if (disocclusion) {
        // The column table as the scene's database for the bake: no page is
        // stored (every cell reads as everything visible, nothing is culled),
        // the draw lists publish the static set's columns at the next flush,
        // and an edit that stales it fails the bake.
        const glm::uvec3 page_dims = (cell_counts + glm::uvec3{m_header.page_size - 1}) / m_header.page_size;
        std::vector<erhe::scene::Pvs_page_entry> no_pages(static_cast<std::size_t>(page_dims.x) * page_dims.y * page_dims.z);
        std::vector<erhe::scene::Pvs_column>     columns_copy = m_columns;
        const std::string validation = erhe::scene::Pvs_database::validate(m_header, columns_copy, no_pages, {});
        if (!validation.empty()) {
            const std::string error = "the column table is inconsistent: " + validation;
            stop(Pvs_bake_state::failed, error);
            return error;
        }
        m_columns_database = std::make_shared<erhe::scene::Pvs_database>(m_header, std::move(columns_copy), std::move(no_pages), std::vector<uint32_t>{});
        m_restore_database = scene_root->get_pvs_database();
        scene_root->set_pvs_database(m_columns_database);
        // The columns' world bounds, two vec4 each, for the bounds vote.
        std::vector<glm::vec4> column_bounds;
        column_bounds.reserve(2u * m_columns.size());
        for (const erhe::scene::Pvs_column& column : m_columns) {
            const std::array<float, 6>& bounds = column.world_bounds;
            column_bounds.push_back(glm::vec4{bounds[0], bounds[1], bounds[2], 0.0f});
            column_bounds.push_back(glm::vec4{bounds[3], bounds[4], bounds[5], 0.0f});
        }
        m_bake_node = erhe::rendergraph::make_rendergraph_node<Pvs_bake_node>(
            *m_context.rendergraph,
            m_context,
            request.face_resolution,
            request.tile_size,
            request.layer_count,
            request.trace_cell.has_value() ? Pvs_trace_mode::on : Pvs_trace_mode::off,
            std::span<const glm::vec4>{column_bounds}
        );
        if ((mode == Bake_mode::bake) && !m_bake_node->is_disocclusion_supported()) {
            const std::string error = "the disocclusion passes' compute pipelines did not build (res/shaders/pvs_*.comp)";
            stop(Pvs_bake_state::failed, error);
            return error;
        }
    }

    log_pvs->info(
        "PVS {} of '{}' started: method {}, {} x {} x {} cells of {} m from ({}, {}, {}), {} columns, {} {}, {} uids assigned{}",
        (mode == Bake_mode::debug_face) ? "debug face" : "bake",
        m_status.scene_name, erhe::scene::c_str(request.method), cell_counts.x, cell_counts.y, cell_counts.z, cell_size,
        region_min.x, region_min.y, region_min.z, m_columns.size(), m_status.capture_count,
        disocclusion ? "cell jobs" : "captures", uids_assigned,
        (disocclusion && !m_sample_point_test) ? "; some static entry has no raytrace geometry: every cell samples from its center" : ""
    );
    return {};
}

void Pvs_baker::cancel()
{
    if (m_status.state != Pvs_bake_state::running) {
        return;
    }
    stop(Pvs_bake_state::cancelled, {});
}

void Pvs_baker::on_scene_closed(const Scene_root* scene_root)
{
    std::shared_ptr<Scene_root> baked = m_scene_root.lock();
    if ((m_status.state == Pvs_bake_state::running) && ((baked.get() == scene_root) || !baked)) {
        stop(Pvs_bake_state::failed, "the scene was closed");
    }
}

void Pvs_baker::stop(const Pvs_bake_state state, const std::string& error)
{
    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - m_start_time;
    m_status.state           = state;
    m_status.error           = error;
    m_status.seconds_elapsed = elapsed.count();
    m_status.seconds_remaining = 0.0f;
    if (state == Pvs_bake_state::failed) {
        log_pvs->error("PVS bake of '{}' failed: {}", m_status.scene_name, error);
    } else if (state == Pvs_bake_state::cancelled) {
        log_pvs->info("PVS bake of '{}' cancelled", m_status.scene_name);
    }
    // Everything the bake built goes; the scene keeps (or gets back) its
    // database.
    restore_database();
    if (m_bake_node) {
        // The GPU times stay in the status after the node goes.
        m_status.gpu_times = m_bake_node->get_gpu_times();
        // Its cells still in flight retire first (release_bake_node()).
        m_retiring_node = std::move(m_bake_node);
        release_bake_node();
    }
    m_cell_records.clear();
    m_cell_has_pvs.clear();
    m_scene_root.reset();
    m_columns.clear();
    m_column_sources.clear();
    m_columns_by_uid.clear();
    m_cell_words.clear();
    m_cell_captures_left.clear();
    m_viewpoints.clear();
    m_camera.reset();
    release_captures();
}

void Pvs_baker::release_captures()
{
    // A capture whose copy is recorded into a frame still in flight is
    // released once that frame retires (later ticks); every other capture
    // goes now (a capture still to be rendered is retired with its node,
    // as get_visible_entries does).
    m_captures.erase(
        std::remove_if(
            m_captures.begin(),
            m_captures.end(),
            [](Capture_slot& slot) -> bool {
                return !slot.capture || (slot.capture->poll() != Scene_id_capture_state::in_flight);
            }
        ),
        m_captures.end()
    );
}

void Pvs_baker::restore_database()
{
    if (!m_columns_database) {
        return;
    }
    // A database set on the scene since (pvs_clear, a finished bake) stays.
    std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (scene_root && (scene_root->get_pvs_database() == m_columns_database)) {
        scene_root->set_pvs_database(m_restore_database);
    }
    m_columns_database.reset();
    m_restore_database.reset();
}

void Pvs_baker::release_bake_node()
{
    if (!m_retiring_node) {
        return;
    }
    // A face submitted but not taken by a prepare phase is dropped; a face
    // prepared for a frame still to be recorded is recorded (and retired)
    // first.
    m_retiring_node->cancel_pending_job();
    m_cell_records.clear();
    m_retiring_node->take_completed(m_cell_records);
    m_cell_records.clear();
    if (!m_retiring_node->has_frames_in_flight()) {
        // The deleter unregisters the node; a frame still to be rendered
        // keeps it retired until then.
        m_retiring_node.reset();
    }
}

void Pvs_baker::tick()
{
    if (m_status.state != Pvs_bake_state::running) {
        if (!m_captures.empty()) {
            release_captures();
        }
        if (m_retiring_node) {
            release_bake_node();
        }
        return;
    }
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (!scene_root) {
        stop(Pvs_bake_state::failed, "the scene was closed");
        return;
    }

    if (m_status.method == erhe::scene::Pvs_bake_method::disocclusion) {
        tick_disocclusion(scene_root);
        return;
    }

    for (Capture_slot& slot : m_captures) {
        if (!slot.busy) {
            continue;
        }
        const Scene_id_capture_state state = slot.capture->poll();
        if (state == Scene_id_capture_state::failed) {
            stop(Pvs_bake_state::failed, "id capture failed: " + slot.capture->get_error());
            return;
        }
        if (state == Scene_id_capture_state::complete) {
            collect_capture(slot);
        }
    }

    issue_captures(scene_root);

    if ((m_status.captures_done == m_status.capture_count) && (m_status.state == Pvs_bake_state::running)) {
        finish(*scene_root);
    }
}

void Pvs_baker::collect_capture(Capture_slot& slot)
{
    const uint32_t cell = slot.cell;
    uint32_t* const words = m_cell_words.data() + (static_cast<std::size_t>(cell) * m_words_per_cell);
    for (const Scene_id_capture_entry& entry : slot.capture->get_entries()) {
        const std::unordered_map<std::string, std::vector<uint32_t>>::const_iterator it = m_columns_by_uid.find(entry.mesh->get_gltf_uid());
        if ((it == m_columns_by_uid.end()) || (entry.primitive_index >= it->second.size())) {
            ++m_status.unmatched_entries;
            continue;
        }
        const uint32_t column = it->second[entry.primitive_index];
        if ((column == erhe::scene::Pvs_database::untracked_column) || (m_column_sources[column].mesh.lock() != entry.mesh)) {
            // Not in the static set, or a mesh that took the uid since the
            // bake started.
            ++m_status.unmatched_entries;
            continue;
        }
        words[column / 32] |= (1u << (column % 32));
    }
    slot.busy = false;
    ++m_status.captures_done;
    ERHE_VERIFY(m_cell_captures_left[cell] > 0);
    --m_cell_captures_left[cell];
    if (m_cell_captures_left[cell] == 0) {
        ++m_status.cells_done;
        m_cell_has_pvs[cell] = 1;
    }
}

void Pvs_baker::tick_disocclusion(const std::shared_ptr<Scene_root>& scene_root)
{
    ERHE_VERIFY(m_bake_node && m_columns_database);
    // The column table must stay the scene's database, unchanged, for the
    // whole bake (doc/plans/pvs.md "Bake" 6).
    if (scene_root->get_pvs_database() != m_columns_database) {
        stop(Pvs_bake_state::failed, "the scene's PVS database was replaced during the bake");
        return;
    }
    if (m_columns_database->get_state() == erhe::scene::Pvs_state::stale) {
        stop(Pvs_bake_state::failed, "a static entry was moved, re-registered or removed during the bake (doc/plans/pvs.md D4)");
        return;
    }

    m_cell_records.clear();
    m_bake_node->take_completed(m_cell_records);
    for (const Pvs_cell_record& record : m_cell_records) {
        collect_cell(record);
        if (m_status.state != Pvs_bake_state::running) {
            return;
        }
    }
    m_cell_records.clear();

    // One cell per frame: the node's prepare phase of this frame takes it
    // (tick() runs before the rendergraph prepares the frame). A cell
    // without a free sample point is done at once; the next one is tried.
    if (!m_bake_node->has_pending_job()) {
        while (m_next_capture < m_status.capture_count) {
            const uint32_t cell =
                (m_mode == Bake_mode::debug_face) ? m_debug.cell :
                (m_request.trace_scope == Pvs_trace_scope::traced_cell_only) ? m_request.trace_cell.value() :
                static_cast<uint32_t>(m_next_capture);
            ++m_next_capture;
            if (submit_cell(scene_root, cell)) {
                break;
            }
        }
    }

    if (m_status.captures_done == m_status.capture_count) {
        if (m_mode == Bake_mode::debug_face) {
            stop(Pvs_bake_state::done, {});
        } else {
            finish(*scene_root);
        }
    }
}

auto Pvs_baker::find_sample_point(Scene_root& scene_root, const uint32_t cell) const -> std::optional<glm::vec3>
{
    const glm::uvec3 counts     = m_header.cell_counts;
    const glm::uvec3 coordinate{cell % counts.x, (cell / counts.x) % counts.y, cell / (counts.x * counts.y)};
    const float      cell_size  = m_header.cell_size;
    const glm::vec3  center     = m_header.grid_origin + ((glm::vec3{coordinate} + glm::vec3{0.5f}) * cell_size);
    if (!m_sample_point_test) {
        return center;
    }
    // A candidate is free when none of the six axis-aligned rays from it
    // first hits a back face (it is not inside a closed single-sided
    // surface). A double-sided primitive has no inside.
    erhe::raytrace::IScene& raytrace_scene = scene_root.get_raytrace_scene();
    raytrace_scene.commit();
    const float ray_length = 2.0f * glm::length(m_scene_max - m_scene_min) + cell_size;
    const auto is_free = [&](const glm::vec3& origin) -> bool {
        for (const glm::vec3& direction : c_face_directions) {
            erhe::raytrace::Ray ray{
                .origin    = origin,
                .t_near    = 0.0f,
                .direction = direction,
                .time      = 0.0f,
                .t_far     = ray_length,
                .mask      = Raytrace_node_mask::content,
                .id        = 0,
                .flags     = 0
            };
            erhe::raytrace::Hit hit{};
            raytrace_scene.intersect(ray, hit);
            if (hit.instance == nullptr) {
                continue;
            }
            if (glm::dot(hit.normal, direction) <= 0.0f) {
                continue; // a front face
            }
            const erhe::scene::Raytrace_primitive* const raytrace_primitive = static_cast<const erhe::scene::Raytrace_primitive*>(hit.instance->get_user_data());
            if ((raytrace_primitive != nullptr) && (raytrace_primitive->mesh != nullptr)) {
                const std::vector<erhe::scene::Mesh_primitive>& primitives = raytrace_primitive->mesh->get_primitives();
                if (raytrace_primitive->primitive_index < primitives.size()) {
                    const std::shared_ptr<erhe::primitive::Material>& material = primitives[raytrace_primitive->primitive_index].material;
                    if (material && material->get_double_sided()) {
                        continue;
                    }
                }
            }
            return false;
        }
        return true;
    };
    if (is_free(center)) {
        return center;
    }
    const float quarter = 0.25f * cell_size;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0) && (dy == 0) && (dz == 0)) {
                    continue;
                }
                const glm::vec3 candidate = center + (quarter * glm::vec3{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)});
                if (is_free(candidate)) {
                    return candidate;
                }
            }
        }
    }
    return std::nullopt;
}

auto Pvs_baker::submit_cell(const std::shared_ptr<Scene_root>& scene_root, const uint32_t cell) -> bool
{
    const glm::uvec3 counts     = m_header.cell_counts;
    const glm::uvec3 coordinate{cell % counts.x, (cell / counts.x) % counts.y, cell / (counts.x * counts.y)};
    const float      cell_size  = m_header.cell_size;
    const glm::vec3  cell_min   = m_header.grid_origin + (glm::vec3{coordinate} * cell_size);
    const glm::vec3  cell_max   = cell_min + glm::vec3{cell_size};

    std::optional<glm::vec3> sample_point = find_sample_point(*scene_root, cell);
    if (!sample_point.has_value()) {
        if (m_mode == Bake_mode::debug_face) {
            sample_point = 0.5f * (cell_min + cell_max);
        } else {
            // No free sample point: the cell gets no PVS (everything
            // visible).
            ++m_status.cells_without_sample_point;
            ++m_status.captures_done;
            ++m_status.cells_done;
            m_cell_has_pvs[cell] = 0;
            return false;
        }
    }
    const glm::vec3 position = sample_point.value();

    // z_near: the largest distance from the sample point to a face of the
    // cell box plus 1 cm ("Bake" 2): every point of the cell lies behind
    // every face's near plane, as the cones need.
    const glm::vec3 to_min = position - cell_min;
    const glm::vec3 to_max = cell_max - position;
    const float z_near = std::max(
        std::max(std::max(to_min.x, to_max.x), std::max(to_min.y, to_max.y)),
        std::max(to_min.z, to_max.z)
    ) + 0.01f;
    // z_far: the farthest corner of the region and the static entries.
    float far_distance = 0.0f;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{
            ((corner & 1u) != 0) ? m_scene_max.x : m_scene_min.x,
            ((corner & 2u) != 0) ? m_scene_max.y : m_scene_min.y,
            ((corner & 4u) != 0) ? m_scene_max.z : m_scene_min.z
        };
        far_distance = std::max(far_distance, glm::length(point - position));
    }
    const float z_far = std::max(far_distance, 2.0f * z_near);


    // Row 0 of the layered buffer is gl_FragCoord.y 0: the top row where
    // the framebuffer origin is the top left, the bottom row elsewhere.
    const bool row_0_at_top = (m_context.graphics_device->get_info().coordinate_conventions.framebuffer_origin != erhe::math::Framebuffer_origin::bottom_left);
    const float half_tangent  = std::tan(0.5f * m_request.face_fov);
    const float resolution    = static_cast<float>(m_request.face_resolution);
    const float pixel_tangent = (2.0f * half_tangent) / resolution;

    Pvs_cell_job job{
        .scene_root     = scene_root,
        .columns        = m_columns_database.get(),
        .cell           = cell,
        .faces          = {},
        .readback_layer = {}
    };
    const uint32_t samples_per_face = m_header.bake_parameters.samples_per_face;
    for (uint32_t sample = 0; sample < samples_per_face; ++sample) {
        // Sample 0 is the pixel-centered projection; sample s > 0 shifts it
        // by a sub-pixel offset of the R2 sequence ("Bake" 4), as a
        // projection with asymmetric field of view sides.
        glm::vec2 jitter{0.0f};
        if (sample > 0) {
            const float s = static_cast<float>(sample);
            jitter = glm::vec2{
                (0.5f + (s * 0.7548776662f)) - std::floor(0.5f + (s * 0.7548776662f)) - 0.5f,
                (0.5f + (s * 0.5698402910f)) - std::floor(0.5f + (s * 0.5698402910f)) - 0.5f
            };
        }
        const float tan_left  = -half_tangent + (jitter.x * pixel_tangent);
        const float tan_right =  half_tangent + (jitter.x * pixel_tangent);
        const float tan_down  = -half_tangent + (jitter.y * pixel_tangent);
        const float tan_up    =  half_tangent + (jitter.y * pixel_tangent);
        for (uint32_t face = 0; face < c_face_count; ++face) {
            if ((m_mode == Bake_mode::debug_face) && ((face != m_debug.face) || (sample != 0))) {
                continue;
            }
            m_camera->set_projection_type(erhe::scene::Projection::Type::perspective_xr);
            m_camera->set_fov_left (std::atan(tan_left));
            m_camera->set_fov_right(std::atan(tan_right));
            m_camera->set_fov_down (std::atan(tan_down));
            m_camera->set_fov_up   (std::atan(tan_up));
            m_camera->set_perspective_z_near(z_near);
            m_camera->set_perspective_z_far (z_far);
            const glm::vec3 direction = c_face_directions[face];
            const glm::vec3 up        = c_face_ups[face];
            const glm::vec3 right     = glm::cross(direction, up);
            m_camera->set_parent_from_node(erhe::math::create_look_at(position, position + direction, up));

            // The cell box in the face's view space (x right, y up, z the
            // depth along the view direction), relative to the sample point.
            glm::vec3 view_min{std::numeric_limits<float>::max()};
            glm::vec3 view_max{std::numeric_limits<float>::lowest()};
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec3 point{
                    ((corner & 1u) != 0) ? cell_max.x : cell_min.x,
                    ((corner & 2u) != 0) ? cell_max.y : cell_min.y,
                    ((corner & 4u) != 0) ? cell_max.z : cell_min.z
                };
                const glm::vec3 offset = point - position;
                const glm::vec3 view{glm::dot(offset, right), glm::dot(offset, up), glm::dot(offset, direction)};
                view_min = glm::min(view_min, view);
                view_max = glm::max(view_max, view);
            }
            // The face's view axes as world axes and signs (faces look along
            // world axes): view x = right, y = up, z = the view direction.
            glm::uvec4 view_axes{0u};
            for (uint32_t i = 0; i < 3u; ++i) {
                const glm::vec3 axis = (i == 0u) ? right : ((i == 1u) ? up : direction);
                for (uint32_t w = 0; w < 3u; ++w) {
                    if (axis[w] != 0.0f) {
                        view_axes[i] = w;
                        if (axis[w] < 0.0f) {
                            view_axes.w |= (1u << i);
                        }
                    }
                }
            }
            if (m_status.trace.valid && (m_status.trace.cell == cell)) {
                m_trace_faces.push_back(
                    Trace_face_view{
                        .face      = face,
                        .sample    = sample,
                        .right     = right,
                        .up        = up,
                        .direction = direction,
                        .geometry  = erhe::scene_renderer::Pvs_face_geometry{
                            .resolution  = m_request.face_resolution,
                            .tile_size   = m_request.tile_size,
                            .layer_count = m_request.layer_count,
                            .z_near      = z_near,
                            .z_far       = z_far,
                            .tan_x0      = tan_left,
                            .tan_dx      = (tan_right - tan_left) / resolution,
                            .tan_y0      = row_0_at_top ? tan_up : tan_down,
                            .tan_dy      = row_0_at_top ? ((tan_down - tan_up) / resolution) : ((tan_up - tan_down) / resolution),
                            .cell_min    = view_min,
                            .cell_max    = view_max,
                            .view_axes   = view_axes,
                            .sample_point = position,
                            .silhouette_tests = m_request.silhouette_tests
                        }
                    }
                );
                m_trace_sample_point = position;
            }
            job.faces.push_back(
                Pvs_face_view{
                    .camera   = erhe::scene::Camera_snapshot{*m_camera},
                    .z_near   = z_near,
                    .z_far    = z_far,
                    .face     = face,
                    .sample   = sample,
                    .geometry = erhe::scene_renderer::Pvs_face_geometry{
                        .resolution  = m_request.face_resolution,
                        .tile_size   = m_request.tile_size,
                        .layer_count = m_request.layer_count,
                        .z_near      = z_near,
                        .z_far       = z_far,
                        .tan_x0      = tan_left,
                        .tan_dx      = (tan_right - tan_left) / resolution,
                        .tan_y0      = row_0_at_top ? tan_up : tan_down,
                        .tan_dy      = row_0_at_top ? ((tan_down - tan_up) / resolution) : ((tan_up - tan_down) / resolution),
                        .cell_min    = view_min,
                        .cell_max    = view_max,
                        .view_axes   = view_axes,
                        .sample_point = position,
                        .silhouette_tests = m_request.silhouette_tests
                    }
                }
            );
        }
    }
    if (m_mode == Bake_mode::debug_face) {
        job.readback_layer = m_debug.layer;
    }
    if ((m_mode == Bake_mode::bake) && m_status.trace.valid && (m_status.trace.cell == cell) && !m_status.trace.columns.empty()) {
        job.trace_columns    = m_status.trace.columns;
        job.trace_depth_face = m_request.trace_face;
    }
    m_bake_node->submit(std::move(job));
    return true;
}

void Pvs_baker::collect_cell(const Pvs_cell_record& record)
{
    ++m_status.captures_done;
    if (record.readback) {
        m_status.debug_depth_entries = record.depth_entries;
        m_status.debug_id_entries    = record.id_entries;
        write_debug_images(record);
    }
    const uint32_t cell = record.cell;
    ERHE_VERIFY(cell < m_cell_captures_left.size());
    if (m_mode == Bake_mode::debug_face) {
        return;
    }
    // The cell's visible columns (the bounds vote over its faces), in the
    // database's column order.
    ERHE_VERIFY(record.column_words && (record.column_word_count == m_words_per_cell));
    const std::size_t byte_count = static_cast<std::size_t>(record.column_word_count) * sizeof(uint32_t);
    erhe::graphics::Buffer& buffer = *record.column_words;
    const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
    buffer.invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        buffer.unmap();
        stop(Pvs_bake_state::failed, "the cell's column words readback could not be mapped");
        return;
    }
    // OR-ed: a cell with several captures (samples per face) unions them.
    uint32_t* const words = m_cell_words.data() + (static_cast<std::size_t>(cell) * m_words_per_cell);
    for (uint32_t i = 0; i < record.column_word_count; ++i) {
        uint32_t word = 0;
        std::memcpy(&word, mapped.data() + (static_cast<std::size_t>(i) * sizeof(uint32_t)), sizeof(uint32_t));
        words[i] |= word;
    }
    buffer.unmap();
    ERHE_VERIFY(m_cell_captures_left[cell] > 0);
    --m_cell_captures_left[cell];
    if (m_cell_captures_left[cell] == 0) {
        ++m_status.cells_done;
        m_cell_has_pvs[cell] = 1;
    }
    if (!record.traces.empty()) {
        collect_trace(record);
    }
}

void Pvs_baker::collect_trace(const Pvs_cell_record& record)
{
    // A diagnostic path: it allocates.
    Pvs_bake_trace& trace = m_status.trace;
    trace.sample_point = m_trace_sample_point;
    const uint32_t* const cell_words = m_cell_words.data() + (static_cast<std::size_t>(record.cell) * m_words_per_cell);
    const std::size_t slot_count = trace.columns.size();
    for (std::size_t i = 0; i < slot_count; ++i) {
        const uint32_t column = trace.columns[i];
        trace.in_cell_pvs[i] = ((cell_words[column / 32] & (1u << (column % 32))) != 0u) ? 1 : 0;
    }
    trace.faces.clear();
    ERHE_VERIFY(record.traces.size() == m_trace_faces.size());
    for (std::size_t face_index = 0; face_index < record.traces.size(); ++face_index) {
        const Pvs_face_trace&  face_trace = record.traces[face_index];
        const Trace_face_view& view       = m_trace_faces[face_index];
        const erhe::scene_renderer::Pvs_disocclusion_parameters parameters = erhe::scene_renderer::make_pvs_disocclusion_parameters(view.geometry, static_cast<uint32_t>(m_columns.size()));
        erhe::graphics::Buffer& buffer = *face_trace.readback;
        const std::size_t byte_count = face_trace.word_count * sizeof(uint32_t);
        const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
        buffer.invalidate(0, byte_count);
        std::vector<uint32_t> words(face_trace.word_count);
        if (mapped.size() >= byte_count) {
            std::memcpy(words.data(), mapped.data(), byte_count);
        }
        buffer.unmap();
        const uint32_t* const counters       = words.data();
        const uint32_t* const fragments      = counters + (4 * slot_count);
        const uint32_t* const column_words   = fragments + face_trace.entry_count;
        const uint32_t* const closure        = column_words + face_trace.column_word_count;
        const uint32_t* const tile_masks     = closure + face_trace.tile_count;
        const uint32_t* const column_masks   = tile_masks + (2u * static_cast<std::size_t>(face_trace.tile_count));
        erhe::scene_renderer::Pvs_disocclusion_result masks{};
        masks.tile_masks  .assign(tile_masks,   tile_masks + (2u * static_cast<std::size_t>(face_trace.tile_count)));
        masks.column_masks.assign(column_masks, column_masks + (2u * static_cast<std::size_t>(parameters.get_tiles_per_layer())));
        erhe::scene_renderer::build_pvs_disoccluded_map(parameters, masks);
        const uint32_t tiles            = parameters.get_tiles();
        const uint32_t layer_count      = parameters.get_layer_count();

        Pvs_trace_face face{.face = face_trace.face, .sample = face_trace.sample, .entries = {}};
        for (std::size_t i = 0; i < slot_count; ++i) {
            const uint32_t column = trace.columns[i];
            Pvs_trace_entry_face entry{};
            entry.samples             = counters[(4 * i) + 0];
            entry.core_samples        = counters[(4 * i) + 1];
            entry.disoccluded_samples = counters[(4 * i) + 2];
            for (uint32_t e = 0; e < face_trace.entry_count; ++e) {
                if (record.entry_columns[e] == column) {
                    entry.fragments += fragments[e];
                }
            }
            entry.in_column_words = ((column_words[column / 32] & (1u << (column % 32))) != 0u);
            {
                // The bounds vote of this face alone (pvs_vote.comp's C++ twin).
                const std::array<float, 6>& vote_bounds = m_columns[column].world_bounds;
                entry.bounds_vote = erhe::scene_renderer::pvs_bounds_vote(
                    parameters,
                    glm::vec3{vote_bounds[0], vote_bounds[1], vote_bounds[2]},
                    glm::vec3{vote_bounds[3], vote_bounds[4], vote_bounds[5]},
                    std::span<const uint32_t>{masks.map}
                );
            }
            // (6) the column's world bounds on the face.
            const std::array<float, 6>& bounds = m_columns[column].world_bounds;
            float min_fx = std::numeric_limits<float>::max();
            float max_fx = std::numeric_limits<float>::lowest();
            float min_fy = std::numeric_limits<float>::max();
            float max_fy = std::numeric_limits<float>::lowest();
            float min_z  = std::numeric_limits<float>::max();
            float max_z  = std::numeric_limits<float>::lowest();
            bool  any_beyond = false;
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const glm::vec3 point{
                    ((corner & 1u) != 0) ? bounds[3] : bounds[0],
                    ((corner & 2u) != 0) ? bounds[4] : bounds[1],
                    ((corner & 4u) != 0) ? bounds[5] : bounds[2]
                };
                const glm::vec3 offset = point - m_trace_sample_point;
                const glm::vec3 v{glm::dot(offset, view.right), glm::dot(offset, view.up), glm::dot(offset, view.direction)};
                if (v.z <= view.geometry.z_near) {
                    entry.near = true;
                    continue;
                }
                any_beyond = true;
                const float fx = ((v.x / v.z) - view.geometry.tan_x0) / (view.geometry.tan_dx * static_cast<float>(view.geometry.tile_size));
                const float fy = ((v.y / v.z) - view.geometry.tan_y0) / (view.geometry.tan_dy * static_cast<float>(view.geometry.tile_size));
                min_fx = std::min(min_fx, fx);
                max_fx = std::max(max_fx, fx);
                min_fy = std::min(min_fy, fy);
                max_fy = std::max(max_fy, fy);
                min_z  = std::min(min_z, v.z);
                max_z  = std::max(max_z, v.z);
            }
            const float tiles_f = static_cast<float>(tiles);
            if (any_beyond && (max_fx >= 0.0f) && (min_fx < tiles_f) && (max_fy >= 0.0f) && (min_fy < tiles_f)) {
                entry.in_face   = true;
                entry.rectangle = glm::uvec4{
                    static_cast<uint32_t>(std::clamp(std::floor(min_fx), 0.0f, tiles_f - 1.0f)),
                    static_cast<uint32_t>(std::clamp(std::floor(min_fy), 0.0f, tiles_f - 1.0f)),
                    static_cast<uint32_t>(std::clamp(std::floor(max_fx), 0.0f, tiles_f - 1.0f)),
                    static_cast<uint32_t>(std::clamp(std::floor(max_fy), 0.0f, tiles_f - 1.0f))
                };
                entry.layer_min = erhe::scene_renderer::pvs_layer_of_depth(std::min(std::max(min_z, view.geometry.z_near), view.geometry.z_far), view.geometry.z_near, view.geometry.z_far, layer_count);
                entry.layer_max = erhe::scene_renderer::pvs_layer_of_depth(std::min(max_z, view.geometry.z_far), view.geometry.z_near, view.geometry.z_far, layer_count);
                entry.tile = glm::uvec2{(entry.rectangle.x + entry.rectangle.z) / 2u, (entry.rectangle.y + entry.rectangle.w) / 2u};
                for (uint32_t layer = 0; layer < layer_count; ++layer) {
                    entry.tile_column.push_back(
                        Pvs_trace_layer{
                            .closed      = (closure[(((layer * tiles) + entry.tile.y) * tiles) + entry.tile.x] != 0u),
                            .disoccluded = erhe::scene_renderer::is_pvs_tile_disoccluded(parameters, masks, entry.tile.x, entry.tile.y, layer)
                        }
                    );
                }
            }
            face.entries.push_back(std::move(entry));
        }
        trace.faces.push_back(std::move(face));
    }
    trace.diagnostics.clear();
    if (record.trace_depth && record.trace_depth_face.has_value()) {
        diagnose_trace_depth(record);
    }
    log_pvs->info("PVS bake trace of cell {}: {} faces, {} columns", record.cell, trace.faces.size(), slot_count);
}

void Pvs_baker::diagnose_trace_depth(const Pvs_cell_record& record)
{
    // A diagnostic path: it allocates. The face's depth region re-run on
    // the CPU with and without the silhouette tests (rule 1 continuity,
    // rule 3): for each traced entry on the face, the first layer whose bit
    // its middle tile column lacks, and the windows of that layer whose
    // cones cover the column without the tests, with their rule 3 decisions
    // and the pixel pairs closest to a gap.
    using namespace erhe::scene_renderer;
    Pvs_bake_trace& trace = m_status.trace;
    const uint32_t face_number = record.trace_depth_face.value();
    std::size_t face_index = m_trace_faces.size();
    for (std::size_t i = 0; i < m_trace_faces.size(); ++i) {
        if ((m_trace_faces[i].face == face_number) && (m_trace_faces[i].sample == 0u)) {
            face_index = i;
        }
    }
    if (face_index == m_trace_faces.size()) {
        trace.diagnostics.push_back(fmt::format("face {} not traced", face_number));
        return;
    }
    const Trace_face_view& view = m_trace_faces[face_index];
    const Pvs_disocclusion_parameters parameters = make_pvs_disocclusion_parameters(view.geometry, 0u);
    const std::size_t word_count = static_cast<std::size_t>(parameters.get_words_per_layer()) * parameters.get_layer_count();
    erhe::graphics::Buffer& buffer = *record.trace_depth;
    const std::size_t byte_count = word_count * sizeof(uint32_t);
    const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
    buffer.invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        buffer.unmap();
        trace.diagnostics.push_back("depth dump could not be mapped");
        return;
    }
    std::vector<uint32_t> depth(word_count);
    std::memcpy(depth.data(), mapped.data(), byte_count);
    buffer.unmap();

    // Rule 3 on and off (columns.z); rule 1's continuity is on in both.
    Pvs_disocclusion_parameters parameters_on  = parameters;
    Pvs_disocclusion_parameters parameters_off = parameters;
    parameters_on.columns.z  = 1u;
    parameters_off.columns.z = 0u;
    Pvs_disocclusion_result with_tests{};
    Pvs_disocclusion_result without_tests{};
    run_pvs_disocclusion_reference(parameters_on,  std::span<const uint32_t>{depth}, std::span<const glm::vec4>{}, with_tests);
    run_pvs_disocclusion_reference(parameters_off, std::span<const uint32_t>{depth}, std::span<const glm::vec4>{}, without_tests);
    const uint32_t tiles = parameters.get_tiles();
    const auto has_bit = [&](const Pvs_disocclusion_result& result, const uint32_t x, const uint32_t y, const uint32_t layer, const uint32_t bit) -> bool {
        const uint32_t tile   = (((layer * tiles) + y) * tiles) + x;
        const uint32_t column = (y * tiles) + x;
        const uint32_t word   = bit >> 5u;
        return (((result.tile_masks[(tile * 2u) + word] | result.column_masks[(column * 2u) + word]) >> (bit & 31u)) & 1u) != 0u;
    };
    const auto pair_text = [](const Pvs_gap_pair& pair) -> std::string {
        return fmt::format(
            "margin {:a} a ({},{}) {:a} a' {:a} b ({},{}) {:a} b' {:a} S {}",
            pair.margin, pair.pixels.x, pair.pixels.y, pair.depths.x, pair.depths.y, pair.pixels.z, pair.pixels.w, pair.depths.z, pair.depths.w, pair.start
        );
    };
    const Pvs_trace_face* face_entries = nullptr;
    for (const Pvs_trace_face& face : trace.faces) {
        if ((face.face == face_number) && (face.sample == 0u)) {
            face_entries = &face;
        }
    }
    if (face_entries == nullptr) {
        return;
    }
    // The surface at a pixel from the sample point (the scene's raytrace
    // scene): mesh, primitive, its material's double_sided, the hit normal
    // and the cell corners strictly behind its plane.
    const std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    const auto describe_pixel = [&](const int32_t px, const int32_t py) -> std::string {
        if (!scene_root) {
            return "no scene";
        }
        const Pvs_face_geometry& geometry = view.geometry;
        const float tx = geometry.tan_x0 + ((static_cast<float>(px) + 0.5f) * geometry.tan_dx);
        const float ty = geometry.tan_y0 + ((static_cast<float>(py) + 0.5f) * geometry.tan_dy);
        const glm::vec3 direction = glm::normalize((view.right * tx) + (view.up * ty) + view.direction);
        erhe::raytrace::IScene& raytrace_scene = scene_root->get_raytrace_scene();
        raytrace_scene.commit();
        erhe::raytrace::Ray ray{
            .origin = m_trace_sample_point, .t_near = 0.0f, .direction = direction, .time = 0.0f,
            .t_far = 10000.0f, .mask = Raytrace_node_mask::content, .id = 0, .flags = 0
        };
        erhe::raytrace::Hit hit{};
        raytrace_scene.intersect(ray, hit);
        if (hit.instance == nullptr) {
            return fmt::format("pixel ({}, {}): no hit", px, py);
        }
        const erhe::scene::Raytrace_primitive* const raytrace_primitive = static_cast<const erhe::scene::Raytrace_primitive*>(hit.instance->get_user_data());
        std::string mesh_name = "?";
        bool double_sided = false;
        if ((raytrace_primitive != nullptr) && (raytrace_primitive->mesh != nullptr)) {
            mesh_name = raytrace_primitive->mesh->get_name();
            const std::vector<erhe::scene::Mesh_primitive>& primitives = raytrace_primitive->mesh->get_primitives();
            if ((raytrace_primitive->primitive_index < primitives.size()) && primitives[raytrace_primitive->primitive_index].material) {
                double_sided = primitives[raytrace_primitive->primitive_index].material->get_double_sided();
            }
        }
        const glm::vec3 position = ray.origin + (ray.t_far * ray.direction);
        const glm::vec3 cell_min = m_header.grid_origin + (glm::vec3{
            static_cast<float>(record.cell % m_header.cell_counts.x),
            static_cast<float>((record.cell / m_header.cell_counts.x) % m_header.cell_counts.y),
            static_cast<float>(record.cell / (m_header.cell_counts.x * m_header.cell_counts.y))
        } * m_header.cell_size);
        uint32_t behind = 0;
        for (uint32_t corner = 0; corner < 8u; ++corner) {
            const glm::vec3 c = cell_min + (m_header.cell_size * glm::vec3{
                static_cast<float>(corner & 1u), static_cast<float>((corner >> 1u) & 1u), static_cast<float>((corner >> 2u) & 1u)
            });
            if (glm::dot(hit.normal, c - position) < 0.0f) {
                ++behind;
            }
        }
        return fmt::format(
            "pixel ({}, {}): {} primitive {} double_sided {} distance {:.3f} at ({:.2f}, {:.2f}, {:.2f}) normal ({:.3f}, {:.3f}, {:.3f}) facing {} cell corners behind its plane {}",
            px, py, mesh_name, (raytrace_primitive != nullptr) ? raytrace_primitive->primitive_index : 0u, double_sided, ray.t_far,
            position.x, position.y, position.z, hit.normal.x, hit.normal.y, hit.normal.z,
            (glm::dot(hit.normal, direction) < 0.0f) ? "front" : "back", behind
        );
    };

    // Why chunk (tx, ty, target) is not disoccluded with the tests, one
    // gating level per depth step: its first missing bit's layer, and the
    // windows of that layer that cover it without the tests.
    std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)> explain;
    explain = [&](const uint32_t tx, const uint32_t ty, const uint32_t target, const uint32_t depth_level) -> void {
        const std::string indent(2u * (depth_level + 1u), ' ');
        trace.diagnostics.push_back(fmt::format(
            "{}chunk ({}, {}, {}): disoccluded with tests {} without {}",
            indent, tx, ty, target,
            is_pvs_tile_disoccluded(parameters, with_tests, tx, ty, target), is_pvs_tile_disoccluded(parameters, without_tests, tx, ty, target)
        ));
        uint32_t missing = target;
        for (uint32_t bit = 0; bit < target; ++bit) {
            if (!has_bit(with_tests, tx, ty, target, bit)) {
                missing = bit;
                break;
            }
        }
        if ((missing == target) || (depth_level >= 4u)) {
            return;
        }
        trace.diagnostics.push_back(fmt::format(
            "{}first missing bit {} (without tests: {}); closed at {}: with {} without {}; run with {:x} without {:x}",
            indent, missing, has_bit(without_tests, tx, ty, target, missing),
            missing, with_tests.closed[(((missing * tiles) + ty) * tiles) + tx], without_tests.closed[(((missing * tiles) + ty) * tiles) + tx],
            with_tests.runs[(ty * tiles) + tx], without_tests.runs[(ty * tiles) + tx]
        ));
        std::size_t listed = 0;
        for (const glm::uvec2& cone : without_tests.cones) {
            const uint32_t layer = cone.x / (tiles * tiles);
            if (layer != missing) {
                continue;
            }
            const uint32_t x = cone.x % tiles;
            const uint32_t y = (cone.x / tiles) % tiles;
            const glm::uvec4 rectangle = pvs_cone_rectangle(parameters, x, y, layer, cone.y, target);
            if ((tx < rectangle.x) || (tx >= rectangle.z) || (ty < rectangle.y) || (ty >= rectangle.w)) {
                continue;
            }
            uint32_t with_sides = 0xffffffffu;
            for (const glm::uvec2& other : with_tests.cones) {
                if (other.x == cone.x) {
                    with_sides = other.y;
                }
            }
            const bool closed_with = with_tests.closed[cone.x] != 0u;
            const bool window_with = !closed_with && is_pvs_tile_disoccluded(parameters, with_tests, x, y, layer);
            trace.diagnostics.push_back(fmt::format(
                "{}window ({}, {}, {}) covers it without tests, sides {:x}; with tests: closed {} window {} sides {}",
                indent, x, y, layer, cone.y, closed_with, window_with, (with_sides == 0xffffffffu) ? std::string{"none"} : fmt::format("{:x}", with_sides)
            ));
            if (closed_with) {
                trace.diagnostics.push_back(fmt::format("{}  run with {:x} without {:x}", indent, with_tests.runs[(y * tiles) + x], without_tests.runs[(y * tiles) + x]));
            } else if (window_with) {
                for (uint32_t side = 0; side < 4u; ++side) {
                    if ((cone.y & (1u << side)) == 0u) {
                        continue;
                    }
                    const Pvs_side_gap_details details = pvs_side_gap_details(parameters, std::span<const uint32_t>{depth}, std::span<const uint32_t>{with_tests.runs}, x, y, layer, side);
                    trace.diagnostics.push_back(fmt::format("{}  side {}: edge gap {} {}", indent, side, details.edge_gap, pair_text(details.edge)));
                    trace.diagnostics.push_back(fmt::format("{}  side {}: inner gap {} {}", indent, side, details.inner_gap, pair_text(details.inner)));
                    trace.diagnostics.push_back(indent + "  " + describe_pixel(details.edge.pixels.z, details.edge.pixels.w));
                }
            } else {
                explain(x, y, layer, depth_level + 1u);
            }
            if (++listed >= 6) {
                break;
            }
        }
    };
    for (std::size_t i = 0; i < face_entries->entries.size(); ++i) {
        const Pvs_trace_entry_face& entry = face_entries->entries[i];
        if (!entry.in_face) {
            continue;
        }
        trace.diagnostics.push_back(fmt::format("column {} face {}", trace.columns[i], face_number));
        explain(entry.tile.x, entry.tile.y, entry.layer_min, 0u);
    }
}

void Pvs_baker::write_debug_images(const Pvs_cell_record& record)
{
    ERHE_VERIFY(record.readback && record.readback_layer.has_value());
    const uint32_t    resolution  = m_request.face_resolution;
    const uint32_t    layer_count = m_request.layer_count;
    const uint32_t    layer       = record.readback_layer.value();
    const std::size_t pixel_count = static_cast<std::size_t>(resolution) * resolution;
    const std::size_t byte_count  = 2 * pixel_count * sizeof(uint32_t);
    erhe::graphics::Buffer& buffer = *record.readback;
    const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
    buffer.invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        buffer.unmap();
        stop(Pvs_bake_state::failed, "the debug face readback could not be mapped");
        return;
    }
    std::vector<uint32_t> words(2 * pixel_count);
    std::memcpy(words.data(), mapped.data(), byte_count);
    buffer.unmap();
    const float front = erhe::scene_renderer::pvs_layer_front_depth(layer,      record.z_near, record.z_far, layer_count);
    const float back  = erhe::scene_renderer::pvs_layer_front_depth(layer + 1u, record.z_near, record.z_far, layer_count);
    m_status.debug_z_near      = record.z_near;
    m_status.debug_z_far       = record.z_far;
    m_status.debug_layer       = layer;
    m_status.debug_layer_front = front;
    m_status.debug_layer_back  = back;

    // Row 0 of the buffer is gl_FragCoord.y 0: the top row where the
    // framebuffer origin is the top left, the bottom row elsewhere (OpenGL).
    const bool flip_rows = (m_context.graphics_device->get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left);
    std::vector<uint8_t> depth_pixels(pixel_count * 4);
    std::vector<uint8_t> id_pixels   (pixel_count * 4);
    std::unordered_set<uint32_t> distinct_ids;
    uint64_t covered = 0;
    for (uint32_t y = 0; y < resolution; ++y) {
        const uint32_t source_row = flip_rows ? (resolution - 1u - y) : y;
        for (uint32_t x = 0; x < resolution; ++x) {
            const std::size_t source = (static_cast<std::size_t>(source_row) * resolution) + x;
            const std::size_t target = ((static_cast<std::size_t>(y) * resolution) + x) * 4;
            const uint32_t depth_bits = words[source];
            const uint32_t id_min     = words[pixel_count + source];
            // depth: the layer's front black to its back light gray; an
            // empty sample white.
            uint8_t gray = 255;
            if (depth_bits != 0xffffffffu) {
                ++covered;
                const float z = std::bit_cast<float>(depth_bits);
                const float t = std::clamp((z - front) / (back - front), 0.0f, 1.0f);
                gray = static_cast<uint8_t>(std::lround(t * 200.0f));
            }
            depth_pixels[target + 0] = gray;
            depth_pixels[target + 1] = gray;
            depth_pixels[target + 2] = gray;
            depth_pixels[target + 3] = 255;
            // id: id_min as a hashed color; empty black.
            uint32_t color = 0;
            if (id_min != 0xffffffffu) {
                distinct_ids.insert(id_min);
                color = ((id_min + 1u) * 0x9e3779b1u) | 0x00404040u;
            }
            id_pixels[target + 0] = static_cast<uint8_t>((color >> 16u) & 0xffu);
            id_pixels[target + 1] = static_cast<uint8_t>((color >>  8u) & 0xffu);
            id_pixels[target + 2] = static_cast<uint8_t>( color         & 0xffu);
            id_pixels[target + 3] = 255;
        }
    }
    m_status.debug_covered_pixels = covered;
    m_status.debug_distinct_ids   = distinct_ids.size();

    const std::string depth_path = m_debug.path + "_depth.png";
    const std::string id_path    = m_debug.path + "_id.png";
    std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    const int size = static_cast<int>(resolution);
    const bool written =
        writer->write_png(std::filesystem::path{depth_path}, size, size, size * 4, erhe::dataformat::Format::format_8_vec4_unorm, std::span<const std::byte>{reinterpret_cast<const std::byte*>(depth_pixels.data()), depth_pixels.size()}) &&
        writer->write_png(std::filesystem::path{id_path},    size, size, size * 4, erhe::dataformat::Format::format_8_vec4_unorm, std::span<const std::byte>{reinterpret_cast<const std::byte*>(id_pixels.data()),    id_pixels.size()});
    if (!written) {
        stop(Pvs_bake_state::failed, "the debug face PNGs could not be written to '" + m_debug.path + "_*.png'");
        return;
    }
    m_status.debug_depth_path = depth_path;
    m_status.debug_id_path    = id_path;
    log_pvs->info(
        "PVS debug face: cell {} face {} layer {} ([{}, {}) of z {} .. {}): {} covered pixels, {} distinct ids; {} and {}",
        record.cell, record.face, layer, front, back, record.z_near, record.z_far, covered, distinct_ids.size(), depth_path, id_path
    );
}

void Pvs_baker::aim_camera(const uint32_t cell, const uint32_t sample, const uint32_t face)
{
    // The viewpoints of a cell: its center, then samples_per_cell - 1
    // uniform positions inside the cell shrunk by 5 percent per side, from a
    // generator seeded with the cell index (deterministic per cell).
    if (m_viewpoint_cell != cell) {
        const glm::uvec3 counts = m_header.cell_counts;
        const glm::uvec3 coordinate{cell % counts.x, (cell / counts.x) % counts.y, cell / (counts.x * counts.y)};
        const glm::vec3  cell_min = m_header.grid_origin + (glm::vec3{coordinate} * m_header.cell_size);
        const float      margin   = 0.05f * m_header.cell_size;
        std::mt19937 rng{cell};
        std::uniform_real_distribution<float> distribution{margin, m_header.cell_size - margin};
        m_viewpoints.clear();
        m_viewpoints.push_back(cell_min + glm::vec3{0.5f * m_header.cell_size});
        for (uint32_t i = 1; i < m_request.samples_per_cell; ++i) {
            const float x = distribution(rng);
            const float y = distribution(rng);
            const float z = distribution(rng);
            m_viewpoints.push_back(cell_min + glm::vec3{x, y, z});
        }
        m_viewpoint_cell = cell;
    }
    const glm::vec3 position = m_viewpoints[sample];

    // Far plane: the farthest corner of the region and the static entries.
    float far_distance = 0.0f;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{
            ((corner & 1u) != 0) ? m_scene_max.x : m_scene_min.x,
            ((corner & 2u) != 0) ? m_scene_max.y : m_scene_min.y,
            ((corner & 4u) != 0) ? m_scene_max.z : m_scene_min.z
        };
        far_distance = std::max(far_distance, glm::length(point - position));
    }
    m_camera->set_perspective_z_far(std::max(far_distance * 1.01f, 2.0f * c_sample_z_near));
    m_camera->set_parent_from_node(erhe::math::create_look_at(position, position + c_face_directions[face], c_face_ups[face]));
}

void Pvs_baker::issue_captures(const std::shared_ptr<Scene_root>& scene_root)
{
    const uint64_t captures_per_cell = uint64_t{m_request.samples_per_cell} * c_face_count;
    while (m_next_capture < m_status.capture_count) {
        Capture_slot* free_slot = nullptr;
        for (Capture_slot& slot : m_captures) {
            if (!slot.busy) {
                free_slot = &slot;
                break;
            }
        }
        if ((free_slot == nullptr) && (m_captures.size() < m_request.captures_in_flight)) {
            m_captures.emplace_back();
            free_slot = &m_captures.back();
        }
        if (free_slot == nullptr) {
            return;
        }
        const uint32_t cell   = static_cast<uint32_t>(m_next_capture / captures_per_cell);
        const uint32_t sample = static_cast<uint32_t>((m_next_capture / c_face_count) % m_request.samples_per_cell);
        const uint32_t face   = static_cast<uint32_t>(m_next_capture % c_face_count);
        aim_camera(cell, sample, face);
        const erhe::scene::Camera_snapshot camera{*m_camera};
        if (free_slot->capture) {
            free_slot->capture->rearm(camera);
        } else {
            const int size = static_cast<int>(m_request.face_resolution);
            free_slot->capture = std::make_unique<Scene_id_capture>(
                m_context,
                Scene_id_capture_create_info{
                    .scene_root = scene_root,
                    .camera     = camera,
                    .width      = size,
                    .height     = size,
                    .content    = Scene_id_capture_content::scene_only
                }
            );
        }
        free_slot->cell = cell;
        free_slot->busy = true;
        ++m_next_capture;
    }
}

void Pvs_baker::finish(Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - m_start_time;
    m_header.bake_duration_seconds = elapsed.count();

    // Pages: every page with a cell with a PVS is stored (with the sampled
    // method every cell gets one); a cell without one, and a cell of a
    // stored page outside the grid, holds all-ones words.
    const uint32_t   page_size      = m_header.page_size;
    const glm::uvec3 counts         = m_header.cell_counts;
    const glm::uvec3 page_dims      = (counts + glm::uvec3{page_size - 1}) / page_size;
    const uint32_t   cells_per_page = page_size * page_size * page_size;
    const uint32_t   words_per_page = cells_per_page * m_words_per_cell;
    const uint32_t   page_count     = page_dims.x * page_dims.y * page_dims.z;
    std::vector<erhe::scene::Pvs_page_entry> pages(page_count);
    std::vector<uint32_t>                    bits;
    const auto cell_has_pvs = [this](const uint32_t cell) -> bool { return m_cell_has_pvs[cell] != 0; };
    for (uint32_t pz = 0; pz < page_dims.z; ++pz) {
        for (uint32_t py = 0; py < page_dims.y; ++py) {
            for (uint32_t px = 0; px < page_dims.x; ++px) {
                const uint32_t page = px + (page_dims.x * (py + (page_dims.y * pz)));
                uint64_t cells_with_pvs = 0;
                for (uint32_t local = 0; local < cells_per_page; ++local) {
                    const glm::uvec3 cell_coordinate{
                        (px * page_size) + (local % page_size),
                        (py * page_size) + ((local / page_size) % page_size),
                        (pz * page_size) + (local / (page_size * page_size))
                    };
                    if (glm::any(glm::greaterThanEqual(cell_coordinate, counts))) {
                        continue;
                    }
                    const uint32_t cell = cell_coordinate.x + (counts.x * (cell_coordinate.y + (counts.y * cell_coordinate.z)));
                    if (cell_has_pvs(cell)) {
                        cells_with_pvs |= (uint64_t{1} << local);
                    }
                }
                if (cells_with_pvs == 0) {
                    continue; // not stored: every cell reads as everything visible
                }
                pages[page].word_offset    = static_cast<uint32_t>(bits.size());
                pages[page].cells_with_pvs = cells_with_pvs;
                bits.resize(bits.size() + words_per_page, 0xffffffffu);
                uint32_t* const page_words = bits.data() + pages[page].word_offset;
                for (uint32_t local = 0; local < cells_per_page; ++local) {
                    if ((cells_with_pvs & (uint64_t{1} << local)) == 0) {
                        continue;
                    }
                    const glm::uvec3 cell_coordinate{
                        (px * page_size) + (local % page_size),
                        (py * page_size) + ((local / page_size) % page_size),
                        (pz * page_size) + (local / (page_size * page_size))
                    };
                    const uint32_t cell = cell_coordinate.x + (counts.x * (cell_coordinate.y + (counts.y * cell_coordinate.z)));
                    std::copy_n(
                        m_cell_words.data() + (static_cast<std::size_t>(cell) * m_words_per_cell),
                        m_words_per_cell,
                        page_words + (static_cast<std::size_t>(local) * m_words_per_cell)
                    );
                }
            }
        }
    }

    const std::string validation = erhe::scene::Pvs_database::validate(m_header, m_columns, pages, bits);
    if (!validation.empty()) {
        stop(Pvs_bake_state::failed, "the baked database is inconsistent: " + validation);
        return;
    }
    const std::shared_ptr<erhe::scene::Pvs_database> database = std::make_shared<erhe::scene::Pvs_database>(
        m_header,
        std::move(m_columns),
        std::move(pages),
        std::move(bits)
    );
    // The bake's database replaces its column table (disocclusion), which
    // is not put back.
    m_columns_database.reset();
    m_restore_database.reset();
    scene_root.set_pvs_database(database);
    m_status.cells_with_pvs = database->get_cells_with_pvs();
    log_pvs->info(
        "PVS bake of '{}' done in {} s: {} cells, {} with a PVS, {} columns, {} stored pages, {} captures, {} unmatched entries",
        m_status.scene_name, m_header.bake_duration_seconds, database->get_cell_count(), database->get_cells_with_pvs(),
        database->get_column_count(), database->get_stored_page_count(), m_status.captures_done, m_status.unmatched_entries
    );
    stop(Pvs_bake_state::done, {});
    m_status.seconds_elapsed = m_header.bake_duration_seconds;
}

} // namespace editor
