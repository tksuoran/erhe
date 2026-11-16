#include "scene/scene_navigation.hpp"

#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "navigation/make_navigation_build_settings.hpp"
#include "scene/scene_root.hpp"
#include "scene/static_mesh_set.hpp"
#include "config/generated/navigation_settings.hpp"
#include "config/generated/scene_export_settings.hpp"

#include "erhe_item/item_flags.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>

namespace editor {

namespace {

// A mesh that would be navigation input if it entered after the build (the job also tests bounds, physics and animation).
[[nodiscard]] auto is_input_candidate(const erhe::scene::Mesh& mesh) -> bool
{
    return is_navigation_input_flag_admitted(mesh.get_flag_bits()) && !mesh.skin;
}

[[nodiscard]] auto shapes_equal(const erhe::scene::Mesh& mesh, const std::vector<std::shared_ptr<const erhe::primitive::Primitive_render_shape>>& shapes) -> bool
{
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh.get_primitives();
    if (primitives.size() != shapes.size()) {
        return false;
    }
    for (std::size_t i = 0, end = primitives.size(); i < end; ++i) {
        const erhe::primitive::Primitive_render_shape* const shape = primitives[i].primitive ? primitives[i].primitive->render_shape.get() : nullptr;
        if (shape != shapes[i].get()) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

auto is_navigation_input_flag_admitted(const uint64_t flag_bits) -> bool
{
    return
        ((flag_bits & erhe::Item_flags::content)            != 0) &&
        ((flag_bits & erhe::Item_flags::visible)            != 0) &&
        ((flag_bits & erhe::Item_flags::render_proxy)       == 0) &&
        ((flag_bits & erhe::Item_flags::session_only)       == 0) &&
        ((flag_bits & Editor_item_flags::world_space_form)  == 0) &&
        ((flag_bits & Editor_item_flags::pass_through)      == 0);
}

auto is_navigation_input_mesh(const erhe::scene::Mesh& mesh, const std::unordered_set<const erhe::scene::Node*>& animated_nodes) -> bool
{
    // The PVS static set (doc/editor/pvs.md D3) with translucent meshes.
    return
        is_navigation_input_flag_admitted(mesh.get_flag_bits()) &&
        !mesh.skin &&
        mesh.get_aabb_world().is_valid_3d() &&
        !is_moved_by_physics(mesh) &&
        !is_animated(mesh, animated_nodes);
}

auto are_navigation_build_settings_equal(const Navigation_build_settings& lhs, const Navigation_build_settings& rhs) -> bool
{
    return
        (lhs.cell_size               == rhs.cell_size              ) &&
        (lhs.cell_height             == rhs.cell_height            ) &&
        (lhs.agent_height            == rhs.agent_height           ) &&
        (lhs.agent_radius            == rhs.agent_radius           ) &&
        (lhs.agent_max_climb         == rhs.agent_max_climb        ) &&
        (lhs.agent_max_slope         == rhs.agent_max_slope        ) &&
        (lhs.region_min_size         == rhs.region_min_size        ) &&
        (lhs.region_merge_size       == rhs.region_merge_size      ) &&
        (lhs.partitioning            == rhs.partitioning           ) &&
        (lhs.edge_max_length         == rhs.edge_max_length        ) &&
        (lhs.edge_max_error          == rhs.edge_max_error         ) &&
        (lhs.verts_per_poly          == rhs.verts_per_poly         ) &&
        (lhs.detail_sample_distance  == rhs.detail_sample_distance ) &&
        (lhs.detail_sample_max_error == rhs.detail_sample_max_error) &&
        (lhs.tile_size               == rhs.tile_size              ) &&
        (lhs.region_min              == rhs.region_min             ) &&
        (lhs.region_max              == rhs.region_max             );
}

auto are_navigation_inputs_equal(const std::span<const Navigation_input_entry> lhs, const std::span<const Navigation_input_entry> rhs) -> bool
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    // In any order: an undo of a removal registers the mesh again at the end of its layer.
    std::unordered_map<std::size_t, const Navigation_input_entry*> rhs_entries;
    for (const Navigation_input_entry& entry : rhs) {
        rhs_entries.emplace(entry.mesh_id, &entry);
    }
    for (const Navigation_input_entry& entry : lhs) {
        if ((entry.mesh_id == 0) || !entry.shapes_known) {
            return false;
        }
        const auto i = rhs_entries.find(entry.mesh_id);
        if (
            (i == rhs_entries.end()) ||
            !i->second->shapes_known ||
            (i->second->root_from_node != entry.root_from_node) ||
            (i->second->shapes != entry.shapes)
        ) {
            return false;
        }
    }
    return true;
}

auto are_navigation_seeds_equal(const std::span<const Navigation_seed> lhs, const std::span<const Navigation_seed> rhs) -> bool
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (const Navigation_seed& seed : lhs) {
        const auto match = std::find_if(rhs.begin(), rhs.end(), [&seed](const Navigation_seed& other) {
            return (other.node_id == seed.node_id) && (other.point == seed.point);
        });
        if (match == rhs.end()) {
            return false;
        }
    }
    return true;
}

Scene_navigation::Scene_navigation(Scene_root& scene_root)
    : m_scene_root{scene_root}
{
    update_status_text_locked();
}

Scene_navigation::~Scene_navigation() noexcept = default;

auto Scene_navigation::root_relative(const erhe::scene::Mesh& mesh) const -> glm::mat4
{
    // The input collection's formula: an unmoved node gives the same bits.
    const std::shared_ptr<erhe::scene::Node> root = m_scene_root.get_scene().get_root_node();
    if (!root) {
        return mesh.world_from_node();
    }
    return glm::inverse(root->world_from_node()) * mesh.world_from_node();
}

void Scene_navigation::collect_current_seeds(std::vector<Navigation_seed>& out_seeds) const
{
    out_seeds.clear();
    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
    m_scene_root.get_navigation_seed_nodes(nodes);
    for (const std::shared_ptr<erhe::scene::Node>& node : nodes) {
        out_seeds.push_back(Navigation_seed{
            .node         = node,
            .node_id      = node->get_id(),
            .name         = node->get_name(),
            .uid          = node->get_gltf_uid(),
            .point        = get_navigation_seed_point(m_scene_root, *node)
        });
    }
}

void Scene_navigation::install(
    std::shared_ptr<Navigation_mesh>      mesh,
    std::vector<Navigation_seed_result>&& seed_results,
    const Navigation_build_settings&      settings,
    std::vector<Navigation_seed>&&        seeds,
    std::vector<Navigation_input_entry>&& input,
    Navigation_build_statistics&&         statistics,
    std::shared_ptr<erhe::scene::Node>    visualization
)
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_mesh         = std::move(mesh);
        m_seed_results = std::move(seed_results);
        m_settings     = settings;
        m_seeds        = std::move(seeds);
        m_input        = std::move(input);
        m_statistics   = std::move(statistics);
        m_input_states.clear();
        for (const Navigation_input_entry& entry : m_input) {
            m_input_states.insert_or_assign(entry.mesh_id, Input_state{.root_from_node = entry.root_from_node, .shapes = entry.shapes});
        }
        m_invalid_count       = 0;
        m_missing_input_count = 0;
        m_added_meshes.clear();
        m_staleness = Navigation_staleness{};
        ++m_build_count;
        ++m_flood_count;
        observe_seeds_locked();
        update_status_text_locked();
    }
    replace_visualization(std::move(visualization));
}

void Scene_navigation::install_loaded(
    std::shared_ptr<Navigation_mesh>                          mesh,
    std::vector<Navigation_seed_result>&&                     seed_results,
    const Navigation_build_settings&                          settings,
    std::vector<Navigation_seed>&&                            seeds,
    std::vector<Navigation_input_entry>&&                     input,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> input_meshes,
    std::unordered_set<std::size_t>&&                         added_meshes,
    Navigation_build_statistics&&                             statistics,
    const Navigation_item_state&                              item,
    std::shared_ptr<erhe::scene::Node>                        visualization
)
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_mesh         = std::move(mesh);
        m_seed_results = std::move(seed_results);
        m_settings     = settings;
        m_seeds        = std::move(seeds);
        m_input        = std::move(input);
        m_statistics   = std::move(statistics);
        m_input_states.clear();
        m_invalid_count       = 0;
        m_missing_input_count = 0;
        for (std::size_t i = 0, end = m_input.size(); i < end; ++i) {
            const Navigation_input_entry&                   entry      = m_input[i];
            const std::shared_ptr<erhe::scene::Mesh> scene_mesh = (i < input_meshes.size()) ? input_meshes[i] : std::shared_ptr<erhe::scene::Mesh>{};
            if (!scene_mesh || (entry.mesh_id == 0)) {
                ++m_missing_input_count;
                continue;
            }
            Input_state state{.root_from_node = entry.root_from_node, .shapes = entry.shapes, .shapes_known = entry.shapes_known};
            state.transform_matches = (root_relative(*scene_mesh) == state.root_from_node);
            state.shapes_match      = state.shapes_known && shapes_equal(*scene_mesh, state.shapes);
            state.admitted          = is_navigation_input_flag_admitted(scene_mesh->get_flag_bits());
            if (!state.is_valid()) {
                ++m_invalid_count;
            }
            m_input_states.insert_or_assign(entry.mesh_id, std::move(state));
        }
        m_added_meshes = std::move(added_meshes);
        m_staleness    = Navigation_staleness{};
        m_staleness.geometry_changed = (m_invalid_count > 0) || (m_missing_input_count > 0) || !m_added_meshes.empty();
        m_item = item;
        observe_seeds_locked();
        update_status_text_locked();
    }
    replace_visualization(std::move(visualization));
    // The saved settings and seeds against the opened scene's.
    on_export_settings_changed(m_scene_root.get_scene_settings().export_settings);
    on_seeds_changed();
}

auto Scene_navigation::install_flood(
    const std::shared_ptr<Navigation_mesh>& mesh,
    const std::span<const uint8_t>          reachable,
    std::vector<Navigation_seed_result>&&   seed_results,
    const Navigation_build_settings&        settings,
    std::vector<Navigation_seed>&&          seeds,
    const double                            flood_ms,
    std::shared_ptr<erhe::scene::Node>      visualization
) -> bool
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_mesh || (m_mesh != mesh)) {
            return false;
        }
        m_mesh->set_reachable(reachable);
        m_seed_results = std::move(seed_results);
        // Only the seed search extents may differ: the rest is what the mesh was built with.
        m_settings.seed_search_radius = settings.seed_search_radius;
        m_settings.seed_search_height = settings.seed_search_height;
        m_seeds                       = std::move(seeds);
        m_statistics.flood_ms         = flood_ms;
        m_staleness.seeds_changed     = false;
        ++m_flood_count;
        observe_seeds_locked();
        update_status_text_locked();
    }
    replace_visualization(std::move(visualization));
    return true;
}

void Scene_navigation::clear()
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_mesh.reset();
        m_seed_results.clear();
        m_settings = Navigation_build_settings{};
        m_seeds.clear();
        m_input.clear();
        m_statistics = Navigation_build_statistics{};
        m_input_states.clear();
        m_invalid_count       = 0;
        m_missing_input_count = 0;
        m_added_meshes.clear();
        m_seed_observers.clear();
        m_staleness = Navigation_staleness{};
        update_status_text_locked();
    }
    replace_visualization({});
}

void Scene_navigation::set_item(const Navigation_item_state& item)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_item = item;
}

void Scene_navigation::clear_item()
{
    std::lock_guard<std::mutex> lock{m_mutex};
    m_item.reset();
}

void Scene_navigation::replace_visualization(std::shared_ptr<erhe::scene::Node> visualization)
{
    std::shared_ptr<erhe::scene::Node> previous;
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        previous        = std::move(m_visualization);
        m_visualization = visualization;
    }
    if (!previous && !visualization) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> root = m_scene_root.get_scene().get_root_node();
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{m_scene_root.item_host_mutex};
    if (previous) {
        previous->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    }
    if (visualization && root) {
        visualization->set_parent(root);
    }
}

void Scene_navigation::observe_seeds_locked()
{
    m_seed_observers.clear();
    for (const Navigation_seed& seed : m_seeds) {
        const std::shared_ptr<erhe::scene::Node> node = seed.node.lock();
        if (!node) {
            continue;
        }
        // A built seed that moves (or moves back, an undo) changes "seeds changed".
        m_seed_observers.push_back(node->add_transform_observer([this](erhe::scene::Xformable&) {
            on_seeds_changed();
        }));
    }
}

template <typename F>
void Scene_navigation::change_input_locked(const erhe::scene::Mesh& mesh, F&& change)
{
    const auto i = m_input_states.find(mesh.get_id());
    if (i == m_input_states.end()) {
        return;
    }
    Input_state& state = i->second;
    const bool was_valid = state.is_valid();
    change(state);
    const bool is_valid = state.is_valid();
    if (was_valid && !is_valid) {
        ++m_invalid_count;
    } else if (!was_valid && is_valid) {
        --m_invalid_count;
    }
    update_geometry_changed_locked();
}

void Scene_navigation::update_geometry_changed_locked()
{
    const bool geometry_changed = (m_invalid_count > 0) || (m_missing_input_count > 0) || !m_added_meshes.empty();
    if (geometry_changed != m_staleness.geometry_changed) {
        m_staleness.geometry_changed = geometry_changed;
        update_status_text_locked();
    }
}

void Scene_navigation::on_mesh_transform_changed(const erhe::scene::Mesh& mesh)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    change_input_locked(mesh, [this, &mesh](Input_state& state) {
        state.transform_matches = (root_relative(mesh) == state.root_from_node);
    });
}

void Scene_navigation::on_mesh_primitives_changed(const erhe::scene::Mesh& mesh)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    change_input_locked(mesh, [&mesh](Input_state& state) {
        state.shapes_match = state.shapes_known && shapes_equal(mesh, state.shapes);
    });
}

void Scene_navigation::on_mesh_registered(const erhe::scene::Mesh& mesh)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    if (!m_input_states.contains(mesh.get_id())) {
        if (is_input_candidate(mesh)) {
            m_added_meshes.insert(mesh.get_id());
            update_geometry_changed_locked();
        }
        return;
    }
    // The hooks of a mesh outside the scene do not reach here: its placement, primitives and flags are read again as it enters.
    change_input_locked(mesh, [this, &mesh](Input_state& state) {
        state.registered        = true;
        state.transform_matches = (root_relative(mesh) == state.root_from_node);
        state.shapes_match      = state.shapes_known && shapes_equal(mesh, state.shapes);
        state.admitted          = is_navigation_input_flag_admitted(mesh.get_flag_bits());
    });
}

void Scene_navigation::on_mesh_unregistered(const erhe::scene::Mesh& mesh)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    if (m_added_meshes.erase(mesh.get_id()) > 0) {
        update_geometry_changed_locked();
        return;
    }
    change_input_locked(mesh, [](Input_state& state) {
        state.registered = false;
    });
}

void Scene_navigation::on_mesh_flags_changed(const erhe::scene::Mesh& mesh, const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    // Hover and selection bits change often: only an admission change is followed.
    const bool was_admitted = is_navigation_input_flag_admitted(old_flag_bits);
    const bool is_admitted  = is_navigation_input_flag_admitted(new_flag_bits);
    if (was_admitted == is_admitted) {
        return;
    }
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh || (mesh.get_item_host() != static_cast<const erhe::Item_host*>(&m_scene_root))) {
        return;
    }
    if (m_input_states.contains(mesh.get_id())) {
        change_input_locked(mesh, [is_admitted](Input_state& state) {
            state.admitted = is_admitted;
        });
        return;
    }
    // A mesh the build did not read enters the input when admitted (shown), and leaves it again when not.
    if (is_admitted && !mesh.skin) {
        m_added_meshes.insert(mesh.get_id());
    } else {
        m_added_meshes.erase(mesh.get_id());
    }
    update_geometry_changed_locked();
}

void Scene_navigation::on_export_settings_changed(const std::optional<Scene_export_settings>& export_settings)
{
    // Without export settings no build can run: the installed mesh matches no settings.
    const std::optional<Navigation_build_settings> settings = export_settings.has_value()
        ? std::optional<Navigation_build_settings>{make_navigation_build_settings(export_settings->navigation)}
        : std::optional<Navigation_build_settings>{};
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    const bool settings_changed = !settings.has_value() || !are_navigation_build_settings_equal(settings.value(), m_settings);
    if (settings_changed != m_staleness.settings_changed) {
        m_staleness.settings_changed = settings_changed;
        update_status_text_locked();
    }
}

void Scene_navigation::on_seeds_changed()
{
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_mesh) {
            return;
        }
    }
    // Read outside m_mutex: the seed list has its own mutex in Scene_root.
    std::vector<Navigation_seed> current;
    collect_current_seeds(current);
    update_seeds_changed(current);
}

void Scene_navigation::update_seeds_changed(const std::span<const Navigation_seed> current)
{
    std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_mesh) {
        return;
    }
    const bool seeds_changed = !are_navigation_seeds_equal(current, m_seeds);
    if (seeds_changed != m_staleness.seeds_changed) {
        m_staleness.seeds_changed = seeds_changed;
        update_status_text_locked();
    }
}

void Scene_navigation::update_status_text_locked()
{
    if (!m_mesh) {
        m_status_text = "No navigation mesh";
        return;
    }
    const Navigation_totals totals = m_mesh->get_totals();
    m_status_text = fmt::format(
        "{} polygons ({} reachable), {} islands ({} reachable), {:.1f} of {:.1f} m2 reachable, {} tiles, {:.0f} ms",
        totals.polygon_count, totals.reachable_polygon_count, totals.island_count, totals.reachable_island_count, totals.reachable_area, totals.total_area,
        m_statistics.tile_count, m_statistics.build_ms + m_statistics.flood_ms
    );
    if (m_statistics.skipped_mesh_count > 0) {
        m_status_text += fmt::format(", {} meshes without triangles", m_statistics.skipped_mesh_count);
    }
    if (m_staleness.is_stale()) {
        std::string reasons;
        const std::array<bool, 3>        holds  {m_staleness.geometry_changed, m_staleness.settings_changed, m_staleness.seeds_changed};
        const std::array<const char*, 3> reason {"geometry changed", "settings changed", "seeds changed"};
        for (std::size_t i = 0; i < holds.size(); ++i) {
            if (holds[i]) {
                reasons += reasons.empty() ? std::string{reason[i]} : fmt::format(", {}", reason[i]);
            }
        }
        m_status_text += " - stale: " + reasons;
    }
}

auto Scene_navigation::get_mesh() const -> std::shared_ptr<Navigation_mesh>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_mesh;
}

auto Scene_navigation::get_visualization() const -> std::shared_ptr<erhe::scene::Node>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_visualization;
}

auto Scene_navigation::get_status() const -> Navigation_status
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return Navigation_status{
        .available   = static_cast<bool>(m_mesh),
        .staleness   = m_staleness,
        .totals      = m_mesh ? m_mesh->get_totals() : Navigation_totals{},
        .statistics  = m_statistics,
        .build_count = m_build_count,
        .flood_count = m_flood_count,
        .item        = m_item
    };
}

auto Scene_navigation::is_current(const Navigation_build_settings& settings) const -> bool
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return
        m_mesh &&
        !m_staleness.is_stale() &&
        (m_settings.get_seed_search_half_extents() == settings.get_seed_search_half_extents());
}

auto Scene_navigation::get_status_text() const -> std::string
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_status_text;
}

auto Scene_navigation::get_settings() const -> Navigation_build_settings
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_settings;
}

void Scene_navigation::get_seeds(std::vector<Navigation_seed>& out_seeds) const
{
    std::lock_guard<std::mutex> lock{m_mutex};
    out_seeds = m_seeds;
}

void Scene_navigation::get_seed_results(std::vector<Navigation_seed_result>& out_results) const
{
    std::lock_guard<std::mutex> lock{m_mutex};
    out_results = m_seed_results;
}

void Scene_navigation::get_input(std::vector<Navigation_input_entry>& out_input) const
{
    std::lock_guard<std::mutex> lock{m_mutex};
    out_input = m_input;
}

auto Scene_navigation::get_statistics() const -> Navigation_build_statistics
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_statistics;
}

auto Scene_navigation::get_item() const -> std::optional<Navigation_item_state>
{
    std::lock_guard<std::mutex> lock{m_mutex};
    return m_item;
}

}
