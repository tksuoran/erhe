#include "renderers/pvs_baker.hpp"
#include "renderers/pvs_bake_node.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"
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
#include "erhe_profile/profile.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/pvs_layers.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
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

auto Pvs_baker::get_status() const -> Pvs_bake_status
{
    Pvs_bake_status status = m_status;
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

    if ((mode == Bake_mode::debug_face) && (m_debug.cell >= cell_count)) {
        return fmt::format("cell must be in [0, {})", cell_count);
    }
    // Captures (sampled) or face passes (disocclusion) per cell.
    const uint32_t passes_per_cell = (disocclusion ? request.samples_per_face : request.samples_per_cell) * c_face_count;

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
        .min_entry_pixels = 0.0f
    };
    m_columns        = std::move(columns);
    m_column_sources = std::move(column_sources);
    m_columns_by_uid = std::move(columns_by_uid);
    m_words_per_cell = static_cast<uint32_t>((m_columns.size() + 31) / 32);
    m_cell_words.assign(static_cast<std::size_t>(cell_count) * m_words_per_cell, 0u);
    m_cell_captures_left.assign(static_cast<std::size_t>(cell_count), passes_per_cell);
    m_cell_has_pvs.assign(static_cast<std::size_t>(cell_count), uint8_t{0});
    m_mode = mode;
    m_scene_min = glm::min(region_min, static_bounds.min);
    m_scene_max = glm::max(region_max, static_bounds.max);
    m_next_capture    = 0;
    m_faces_submitted = 0;
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
        .capture_count = (mode == Bake_mode::debug_face) ? uint64_t{1} : (cell_count * uint64_t{passes_per_cell}),
        .uids_assigned = uids_assigned
    };
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
        m_bake_node = erhe::rendergraph::make_rendergraph_node<Pvs_bake_node>(
            *m_context.rendergraph,
            m_context,
            request.face_resolution,
            request.layer_count
        );
        if (mode == Bake_mode::debug_face) {
            // The one face pass: the cell's first sample of the face.
            m_next_capture = (uint64_t{m_debug.cell} * passes_per_cell) + m_debug.face;
        }
    }

    log_pvs->info(
        "PVS {} of '{}' started: method {}, {} x {} x {} cells of {} m from ({}, {}, {}), {} columns, {} {}, {} uids assigned",
        (mode == Bake_mode::debug_face) ? "debug face" : "bake",
        m_status.scene_name, erhe::scene::c_str(request.method), cell_counts.x, cell_counts.y, cell_counts.z, cell_size,
        region_min.x, region_min.y, region_min.z, m_columns.size(), m_status.capture_count,
        disocclusion ? "face passes" : "captures", uids_assigned
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
        // Its faces still in flight retire first (release_bake_node()).
        m_retiring_node = std::move(m_bake_node);
        release_bake_node();
    }
    m_face_records.clear();
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
    m_face_records.clear();
    m_retiring_node->take_completed(m_face_records);
    m_face_records.clear();
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

    m_face_records.clear();
    m_bake_node->take_completed(m_face_records);
    for (const Pvs_face_record& record : m_face_records) {
        collect_face(record);
        if (m_status.state != Pvs_bake_state::running) {
            return;
        }
    }
    m_face_records.clear();

    // One face pass per frame: the node's prepare phase of this frame takes
    // it (tick() runs before the rendergraph prepares the frame).
    if (!m_bake_node->has_pending_job() && (m_faces_submitted < m_status.capture_count)) {
        submit_face(scene_root, m_next_capture);
        ++m_next_capture;
        ++m_faces_submitted;
    }

    if (m_status.captures_done == m_status.capture_count) {
        if (m_mode == Bake_mode::debug_face) {
            stop(Pvs_bake_state::done, {});
        } else {
            finish(*scene_root);
        }
    }
}

void Pvs_baker::submit_face(const std::shared_ptr<Scene_root>& scene_root, const uint64_t index)
{
    const uint32_t samples_per_face = m_header.bake_parameters.samples_per_face;
    const uint64_t passes_per_cell  = uint64_t{samples_per_face} * c_face_count;
    const uint32_t cell   = static_cast<uint32_t>(index / passes_per_cell);
    const uint32_t sample = static_cast<uint32_t>((index / c_face_count) % samples_per_face);
    const uint32_t face   = static_cast<uint32_t>(index % c_face_count);

    // The sample point: the cell center (the free point search of
    // doc/plans/pvs.md "Bake" 1 is step 5). z_near: the largest distance
    // from it to a face of the cell box plus 1 cm ("Bake" 2), so every point
    // of the cell lies behind every face's near plane.
    const glm::uvec3 counts     = m_header.cell_counts;
    const glm::uvec3 coordinate{cell % counts.x, (cell / counts.x) % counts.y, cell / (counts.x * counts.y)};
    const float      cell_size  = m_header.cell_size;
    const glm::vec3  position   = m_header.grid_origin + ((glm::vec3{coordinate} + glm::vec3{0.5f}) * cell_size);
    const float      z_near     = (0.5f * cell_size) + 0.01f;
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

    // Sample 0 is the pixel-centered projection; sample s > 0 shifts it by
    // a sub-pixel offset of the R2 sequence ("Bake" 4), as a projection
    // with asymmetric field of view sides.
    glm::vec2 jitter{0.0f};
    if (sample > 0) {
        const float s = static_cast<float>(sample);
        jitter = glm::vec2{
            (0.5f + (s * 0.7548776662f)) - std::floor(0.5f + (s * 0.7548776662f)) - 0.5f,
            (0.5f + (s * 0.5698402910f)) - std::floor(0.5f + (s * 0.5698402910f)) - 0.5f
        };
    }
    const float half_tangent = std::tan(0.5f * m_request.face_fov);
    const float pixel_tangent = (2.0f * half_tangent) / static_cast<float>(m_request.face_resolution);
    m_camera->set_projection_type(erhe::scene::Projection::Type::perspective_xr);
    m_camera->set_fov_left (std::atan(-half_tangent + (jitter.x * pixel_tangent)));
    m_camera->set_fov_right(std::atan( half_tangent + (jitter.x * pixel_tangent)));
    m_camera->set_fov_down (std::atan(-half_tangent + (jitter.y * pixel_tangent)));
    m_camera->set_fov_up   (std::atan( half_tangent + (jitter.y * pixel_tangent)));
    m_camera->set_perspective_z_near(z_near);
    m_camera->set_perspective_z_far (z_far);
    m_camera->set_parent_from_node(erhe::math::create_look_at(position, position + c_face_directions[face], c_face_ups[face]));

    std::optional<uint32_t> readback_layer{};
    if (m_mode == Bake_mode::debug_face) {
        readback_layer = m_debug.layer;
    }
    m_bake_node->submit(
        Pvs_face_job{
            .scene_root     = scene_root,
            .columns        = m_columns_database.get(),
            .camera         = erhe::scene::Camera_snapshot{*m_camera},
            .z_near         = z_near,
            .z_far          = z_far,
            .cell           = cell,
            .face           = face,
            .sample         = sample,
            .readback_layer = readback_layer
        }
    );
}

void Pvs_baker::collect_face(const Pvs_face_record& record)
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
    ERHE_VERIFY(m_cell_captures_left[cell] > 0);
    --m_cell_captures_left[cell];
    if (m_cell_captures_left[cell] == 0) {
        // The disocclusion passes that turn the layered buffers into the
        // cell's PVS are doc/plans/pvs.md "Build order" step 5: until then
        // a cell ends without a PVS (everything visible).
        ++m_status.cells_done;
        m_cell_has_pvs[cell] = 0;
    }
}

void Pvs_baker::write_debug_images(const Pvs_face_record& record)
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
