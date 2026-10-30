#include "renderers/pvs_baker.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_id_capture.hpp"
#include "scene/scene_root.hpp"

#include "erhe_gltf/gltf_uid.hpp"
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
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
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
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    if (m_status.state == Pvs_bake_state::running) {
        return fmt::format("a bake of scene '{}' is running", m_status.scene_name);
    }
    if (!m_captures.empty()) {
        return "the previous bake is still releasing its captures; try again in a few frames";
    }
    if (!scene_root) {
        return "no scene";
    }
    if (request.method == erhe::scene::Pvs_bake_method::disocclusion) {
        return "method 'disocclusion' is not built yet (doc/plans/pvs.md \"Build order\" step 5)";
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

            const glm::mat4& world_from_node = mesh->world_from_node();
            std::array<float, 12> world_transform{};
            for (glm::length_t column = 0; column < 4; ++column) {
                for (glm::length_t row = 0; row < 3; ++row) {
                    world_transform[static_cast<std::size_t>((column * 3) + row)] = world_from_node[column][row];
                }
            }
            const std::array<float, 6> world_bounds{
                world_aabb.min.x, world_aabb.min.y, world_aabb.min.z,
                world_aabb.max.x, world_aabb.max.y, world_aabb.max.z
            };
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
        .tile_size        = 0,
        .layer_count      = 0,
        .face_fov         = request.face_fov,
        .samples_per_face = 1,
        .min_entry_pixels = 0.0f
    };
    m_columns        = std::move(columns);
    m_column_sources = std::move(column_sources);
    m_columns_by_uid = std::move(columns_by_uid);
    m_words_per_cell = static_cast<uint32_t>((m_columns.size() + 31) / 32);
    m_cell_words.assign(static_cast<std::size_t>(cell_count) * m_words_per_cell, 0u);
    m_cell_captures_left.assign(static_cast<std::size_t>(cell_count), request.samples_per_cell * c_face_count);
    m_scene_min = glm::min(region_min, static_bounds.min);
    m_scene_max = glm::max(region_max, static_bounds.max);
    m_next_capture   = 0;
    m_viewpoint_cell = 0xffffffffu;
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
        .capture_count = cell_count * uint64_t{request.samples_per_cell} * c_face_count,
        .uids_assigned = uids_assigned
    };
    log_pvs->info(
        "PVS bake of '{}' started: method {}, {} x {} x {} cells of {} m from ({}, {}, {}), {} columns, {} captures, {} uids assigned",
        m_status.scene_name, erhe::scene::c_str(request.method), cell_counts.x, cell_counts.y, cell_counts.z, cell_size,
        region_min.x, region_min.y, region_min.z, m_columns.size(), m_status.capture_count, uids_assigned
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
    // Everything the bake built goes; the scene keeps its database.
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

void Pvs_baker::tick()
{
    if (m_status.state != Pvs_bake_state::running) {
        if (!m_captures.empty()) {
            release_captures();
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
    }
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
    const auto cell_has_pvs = [this](const uint32_t cell) -> bool { return m_cell_captures_left[cell] == 0; };
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
    scene_root.set_pvs_database(database);
    log_pvs->info(
        "PVS bake of '{}' done in {} s: {} cells, {} with a PVS, {} columns, {} stored pages, {} captures, {} unmatched entries",
        m_status.scene_name, m_header.bake_duration_seconds, database->get_cell_count(), database->get_cells_with_pvs(),
        database->get_column_count(), database->get_stored_page_count(), m_status.captures_done, m_status.unmatched_entries
    );
    stop(Pvs_bake_state::done, {});
    m_status.seconds_elapsed = m_header.bake_duration_seconds;
}

} // namespace editor
