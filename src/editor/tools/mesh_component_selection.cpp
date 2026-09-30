#include "tools/mesh_component_selection.hpp"

#include "app_message_bus.hpp"
#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/topology.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <geogram/mesh/mesh.h>

#include <set>
#include <vector>

namespace editor {

auto c_str(const Mesh_component_mode mode) -> const char*
{
    switch (mode) {
        case Mesh_component_mode::object: return "Object";
        case Mesh_component_mode::vertex: return "Vertex";
        case Mesh_component_mode::edge:   return "Edge";
        case Mesh_component_mode::face:   return "Face";
        case Mesh_component_mode::bone:   return "Bone";
        default:                          return "?";
    }
}

auto is_mesh_component_mode(const Mesh_component_mode mode) -> bool
{
    return (mode == Mesh_component_mode::vertex) ||
           (mode == Mesh_component_mode::edge)   ||
           (mode == Mesh_component_mode::face);
}

auto make_edge_key(const GEO::index_t a, const GEO::index_t b) -> Mesh_edge_key
{
    return (a <= b) ? Mesh_edge_key{a, b} : Mesh_edge_key{b, a};
}

auto c_str(const Mode_conversion conversion) -> const char*
{
    switch (conversion) {
        case Mode_conversion::flush:  return "flush";
        case Mode_conversion::expand: return "expand";
        default:                      return "?";
    }
}

auto c_str(const Loop_kind kind) -> const char*
{
    switch (kind) {
        case Loop_kind::edge_loop:     return "edge_loop";
        case Loop_kind::edge_ring:     return "edge_ring";
        case Loop_kind::boundary_loop: return "boundary_loop";
        case Loop_kind::face_loop:     return "face_loop";
        default:                       return "?";
    }
}

auto c_str(const Select_action action) -> const char*
{
    switch (action) {
        case Select_action::replace:  return "replace";
        case Select_action::extend:   return "extend";
        case Select_action::deselect: return "deselect";
        default:                      return "?";
    }
}

auto walk_mesh_loop(
    const erhe::geometry::Geometry&         geometry,
    const Mesh_edge_key                     edge_key,
    const Loop_kind                         kind,
    const erhe::geometry::Edge_loop_delimit delimit,
    std::vector<GEO::index_t>&              out_elements
) -> bool
{
    out_elements.clear();
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        return false;
    }
    const GEO::index_t vertex_count = geometry.get_mesh().vertices.nb();
    if ((edge_key.first == edge_key.second) || (edge_key.first >= vertex_count) || (edge_key.second >= vertex_count)) {
        return false;
    }
    const GEO::index_t edge = geometry.get_edge(edge_key.first, edge_key.second);
    if (edge == GEO::NO_EDGE) {
        return false;
    }
    switch (kind) {
        case Loop_kind::edge_loop: {
            static_cast<void>(erhe::geometry::walk_edge_loop(geometry, edge, delimit, out_elements));
            break;
        }
        case Loop_kind::edge_ring: {
            static_cast<void>(erhe::geometry::walk_edge_ring(geometry, edge, out_elements));
            break;
        }
        case Loop_kind::boundary_loop: {
            erhe::geometry::walk_boundary_loop(geometry, edge, out_elements);
            break;
        }
        case Loop_kind::face_loop: {
            static_cast<void>(erhe::geometry::walk_face_loop(geometry, edge, out_elements));
            break;
        }
        default: {
            break;
        }
    }
    return true;
}

auto append_mesh_component_targets(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    std::vector<Mesh_component_target>&       out_targets
) -> std::size_t
{
    if (!mesh) {
        return 0;
    }
    // The same filters as the interactive pick (Mesh_component_selection_tool::pick)
    // and is_live(), so every target yields a live entry.
    if (!erhe::utility::test_bit_set(mesh->get_flag_bits(), erhe::Item_flags::content)) {
        return 0;
    }
    if (mesh->skin || mesh->is_lock_edit()) {
        return 0;
    }
    std::size_t count = 0;
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
        const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[primitive_index].primitive;
        if (!primitive || primitive->collision_shape) {
            continue;
        }
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive->get_shape_for_raytrace();
        if (!shape) {
            continue;
        }
        std::shared_ptr<erhe::geometry::Geometry> geometry = shape->get_geometry_const();
        if (!geometry) {
            continue;
        }
        out_targets.push_back(
            Mesh_component_target{
                .mesh            = mesh,
                .primitive_index = primitive_index,
                .geometry        = std::move(geometry)
            }
        );
        ++count;
    }
    return count;
}

namespace {

// Flush and mode conversion read the facet corners only: an edge is the
// canonical vertex pair of two consecutive corners of a facet (erhe meshes
// have no wire edges), so no connectivity is needed. Cold path (mode switch,
// selection command), so the transient sets here are acceptable.

template <typename Key>
void assign_if_changed(Component_set<Key>& target, std::set<Key>&& keys)
{
    if (target.get() != keys) {
        target = std::move(keys);
    }
}

[[nodiscard]] auto next_corner_vertex(const GEO::Mesh& mesh, const GEO::index_t facet, const GEO::index_t local_corner, const GEO::index_t corner_count) -> GEO::index_t
{
    return mesh.facet_corners.vertex(mesh.facets.corner(facet, (local_corner + 1) % corner_count));
}

[[nodiscard]] auto corner_vertex(const GEO::Mesh& mesh, const GEO::index_t facet, const GEO::index_t local_corner) -> GEO::index_t
{
    return mesh.facet_corners.vertex(mesh.facets.corner(facet, local_corner));
}

[[nodiscard]] auto mode_rank(const Mesh_component_mode mode) -> int
{
    switch (mode) {
        case Mesh_component_mode::vertex: return 0;
        case Mesh_component_mode::edge:   return 1;
        case Mesh_component_mode::face:   return 2;
        default:                          return -1;
    }
}

// Derive the two sets other than `mode`'s from `mode`'s set.
void flush_entry(const GEO::Mesh& mesh, const Mesh_component_mode mode, Mesh_component_entry& entry)
{
    switch (mode) {
        case Mesh_component_mode::vertex: {
            const std::set<GEO::index_t>& vertices = entry.vertices;
            std::set<Mesh_edge_key> edges;
            std::set<GEO::index_t>  facets;
            for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
                const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
                bool all_selected = true;
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t vertex = corner_vertex(mesh, facet, i);
                    if (!vertices.contains(vertex)) {
                        all_selected = false;
                        continue;
                    }
                    const GEO::index_t next = next_corner_vertex(mesh, facet, i, corner_count);
                    if (vertices.contains(next)) {
                        edges.insert(make_edge_key(vertex, next));
                    }
                }
                if (all_selected) {
                    facets.insert(facets.end(), facet);
                }
            }
            assign_if_changed(entry.edges,  std::move(edges));
            assign_if_changed(entry.facets, std::move(facets));
            break;
        }
        case Mesh_component_mode::edge: {
            const std::set<Mesh_edge_key>& edges = entry.edges;
            std::set<GEO::index_t> vertices;
            std::set<GEO::index_t> facets;
            for (const Mesh_edge_key& key : edges) {
                vertices.insert(key.first);
                vertices.insert(key.second);
            }
            for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
                const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
                bool all_selected = true;
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t vertex = corner_vertex(mesh, facet, i);
                    const GEO::index_t next   = next_corner_vertex(mesh, facet, i, corner_count);
                    if (!edges.contains(make_edge_key(vertex, next))) {
                        all_selected = false;
                        break;
                    }
                }
                if (all_selected) {
                    facets.insert(facets.end(), facet);
                }
            }
            assign_if_changed(entry.vertices, std::move(vertices));
            assign_if_changed(entry.facets,   std::move(facets));
            break;
        }
        case Mesh_component_mode::face: {
            std::set<GEO::index_t>  vertices;
            std::set<Mesh_edge_key> edges;
            for (const GEO::index_t facet : entry.facets) {
                const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t vertex = corner_vertex(mesh, facet, i);
                    vertices.insert(vertex);
                    edges.insert(make_edge_key(vertex, next_corner_vertex(mesh, facet, i, corner_count)));
                }
            }
            assign_if_changed(entry.vertices, std::move(vertices));
            assign_if_changed(entry.edges,    std::move(edges));
            break;
        }
        case Mesh_component_mode::object:
        default: {
            break;
        }
    }
}

// Mode_conversion::expand from `from` to `to` (both component modes): writes
// `to`'s set only; the caller flushes from `to` afterwards.
void expand_entry(const GEO::Mesh& mesh, const Mesh_component_mode from, const Mesh_component_mode to, Mesh_component_entry& entry)
{
    const int from_rank = mode_rank(from);
    const int to_rank   = mode_rank(to);
    if ((from_rank < 0) || (to_rank < 0) || (from_rank == to_rank)) {
        return;
    }
    if (to_rank > from_rank) {
        // Going up: every element touching the selection.
        std::set<Mesh_edge_key> edges;
        std::set<GEO::index_t>  facets;
        for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
            const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
            bool touches = false;
            for (GEO::index_t i = 0; i < corner_count; ++i) {
                const GEO::index_t  vertex = corner_vertex(mesh, facet, i);
                const GEO::index_t  next   = next_corner_vertex(mesh, facet, i, corner_count);
                const Mesh_edge_key key    = make_edge_key(vertex, next);
                const bool edge_touches = (from == Mesh_component_mode::vertex)
                    ? (entry.vertices.contains(vertex) || entry.vertices.contains(next))
                    : entry.edges.contains(key);
                if (edge_touches) {
                    touches = true;
                    edges.insert(key);
                }
            }
            if (touches) {
                facets.insert(facets.end(), facet);
            }
        }
        if (to == Mesh_component_mode::edge) {
            assign_if_changed(entry.edges, std::move(edges));
        } else {
            assign_if_changed(entry.facets, std::move(facets));
        }
        return;
    }

    // Going down: only the elements completely surrounded by the selection,
    // that is, not part of any unselected element of the old mode.
    std::set<GEO::index_t>  excluded_vertices;
    std::set<Mesh_edge_key> excluded_edges;
    std::set<GEO::index_t>  vertices;
    std::set<Mesh_edge_key> edges;
    for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
        const GEO::index_t corner_count   = mesh.facets.nb_corners(facet);
        const bool         facet_selected = entry.facets.contains(facet);
        for (GEO::index_t i = 0; i < corner_count; ++i) {
            const GEO::index_t  vertex = corner_vertex(mesh, facet, i);
            const GEO::index_t  next   = next_corner_vertex(mesh, facet, i, corner_count);
            const Mesh_edge_key key    = make_edge_key(vertex, next);
            if (from == Mesh_component_mode::face) {
                if (facet_selected) {
                    vertices.insert(vertex);
                    edges.insert(key);
                } else {
                    excluded_vertices.insert(vertex);
                    excluded_edges.insert(key);
                }
            } else { // from edge, to vertex
                if (entry.edges.contains(key)) {
                    vertices.insert(key.first);
                    vertices.insert(key.second);
                } else {
                    excluded_vertices.insert(key.first);
                    excluded_vertices.insert(key.second);
                }
            }
        }
    }
    if (to == Mesh_component_mode::vertex) {
        std::erase_if(vertices, [&excluded_vertices](const GEO::index_t vertex) { return excluded_vertices.contains(vertex); });
        assign_if_changed(entry.vertices, std::move(vertices));
    } else {
        std::erase_if(edges, [&excluded_edges](const Mesh_edge_key& key) { return excluded_edges.contains(key); });
        assign_if_changed(entry.edges, std::move(edges));
    }
}

} // anonymous namespace

#pragma region Mesh_component_entry
auto Mesh_component_entry::is_empty() const -> bool
{
    return vertices.empty() && facets.empty() && edges.empty();
}

void Mesh_component_entry::clear()
{
    vertices.clear();
    facets.clear();
    edges.clear();
}

void Mesh_component_entry::set_owner(Mesh_component_selection* const owner)
{
    vertices.set_owner(owner);
    facets  .set_owner(owner);
    edges   .set_owner(owner);
}

void Mesh_component_entry::add_vertex(const GEO::index_t vertex)
{
    vertices.insert(vertex);
}

void Mesh_component_entry::toggle_vertex(const GEO::index_t vertex)
{
    if (!vertices.erase(vertex)) {
        vertices.insert(vertex);
    }
}

void Mesh_component_entry::add_facet(const GEO::index_t facet)
{
    facets.insert(facet);
}

void Mesh_component_entry::toggle_facet(const GEO::index_t facet)
{
    if (!facets.erase(facet)) {
        facets.insert(facet);
    }
}

void Mesh_component_entry::add_edge(const GEO::index_t a, const GEO::index_t b)
{
    edges.insert(make_edge_key(a, b));
}

void Mesh_component_entry::toggle_edge(const GEO::index_t a, const GEO::index_t b)
{
    const Mesh_edge_key key = make_edge_key(a, b);
    if (!edges.erase(key)) {
        edges.insert(key);
    }
}
#pragma endregion Mesh_component_entry

Mesh_component_selection::Mesh_component_selection(App_message_bus& app_message_bus)
    : m_app_message_bus{app_message_bus}
{
    // Kept as an eager-housekeeping safety net: when an operation announces a
    // geometry swap, prune entries that can no longer become live. Correctness
    // comes from the content-addressed is_live() check, not from this message.
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message& message) {
            on_mesh_geometry_changed(message);
        }
    );
    // The queued announcement has been delivered: the next write queues a new one.
    m_selection_changed_subscription = app_message_bus.mesh_component_selection_changed.subscribe(
        [this](Mesh_component_selection_changed_message&) {
            m_change_pending = false;
        }
    );
}

void Mesh_component_selection::on_components_changed()
{
    if (m_change_pending) {
        return;
    }
    m_change_pending = true;
    m_app_message_bus.mesh_component_selection_changed.queue_message(Mesh_component_selection_changed_message{});
}

void Mesh_component_selection::on_mesh_geometry_changed(Mesh_geometry_changed_message&)
{
    // Drop entries that can never become live again (mesh or geometry freed) or
    // that hold nothing. Dormant-but-alive entries (the swapped-away Geometry is
    // not currently bound but is still held by an undo operation) are retained so
    // undo can make them live again.
    prune();
}

auto Mesh_component_selection::get_mode() const -> Mesh_component_mode
{
    return m_mode;
}

void Mesh_component_selection::set_mode(const Mesh_component_mode mode, const Mode_conversion conversion)
{
    if (m_mode == mode) {
        return;
    }
    const Mesh_component_mode old_mode = m_mode;
    m_mode = mode;
    if (is_mesh_component_mode(mode)) {
        const bool expand = (conversion == Mode_conversion::expand) && is_mesh_component_mode(old_mode);
        for (Mesh_component_entry& entry : m_entries) {
            if (!is_live(entry)) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
            const GEO::Mesh& geo_mesh = geometry->get_mesh();
            if (expand) {
                expand_entry(geo_mesh, old_mode, mode, entry);
            }
            flush_entry(geo_mesh, mode, entry);
        }
    }
    // Announce after the assignment: the message carries no payload, so
    // subscribers read the new mode back through get_mode().
    m_app_message_bus.mesh_component_mode_changed.send_message(Mesh_component_mode_changed_message{});
}

auto Mesh_component_selection::find_entry(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) -> Mesh_component_entry*
{
    for (Mesh_component_entry& entry : m_entries) {
        if (
            (entry.mesh.lock()     == mesh)            &&
            (entry.primitive_index == primitive_index) &&
            (entry.geometry.lock() == geometry)
        ) {
            return &entry;
        }
    }
    return nullptr;
}

auto Mesh_component_selection::find_or_create_entry(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) -> Mesh_component_entry&
{
    Mesh_component_entry* const existing = find_entry(mesh, primitive_index, geometry);
    if (existing != nullptr) {
        return *existing;
    }
    Mesh_component_entry& entry = m_entries.emplace_back();
    entry.set_owner(this);
    entry.mesh            = mesh;
    entry.primitive_index = primitive_index;
    entry.geometry        = geometry;
    return entry;
}

void Mesh_component_selection::clear_all()
{
    const bool had_components = !is_empty();
    m_entries.clear();
    if (had_components) {
        on_components_changed();
    }
}

#pragma region Selection commands
void Mesh_component_selection::flush()
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (Mesh_component_entry& entry : m_entries) {
        if (!is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        flush_entry(geometry->get_mesh(), m_mode, entry);
    }
}

void Mesh_component_selection::select_all(const std::span<const Mesh_component_target> targets)
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (const Mesh_component_target& target : targets) {
        const GEO::Mesh&      geo_mesh = target.geometry->get_mesh();
        Mesh_component_entry& entry    = find_or_create_entry(target.mesh, target.primitive_index, target.geometry);
        switch (m_mode) {
            case Mesh_component_mode::vertex: {
                std::set<GEO::index_t> vertices;
                for (GEO::index_t vertex = 0, end = geo_mesh.vertices.nb(); vertex < end; ++vertex) {
                    vertices.insert(vertices.end(), vertex);
                }
                assign_if_changed(entry.vertices, std::move(vertices));
                break;
            }
            case Mesh_component_mode::edge: {
                std::set<Mesh_edge_key> edges;
                for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                    const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                    for (GEO::index_t i = 0; i < corner_count; ++i) {
                        edges.insert(make_edge_key(corner_vertex(geo_mesh, facet, i), next_corner_vertex(geo_mesh, facet, i, corner_count)));
                    }
                }
                assign_if_changed(entry.edges, std::move(edges));
                break;
            }
            case Mesh_component_mode::face: {
                std::set<GEO::index_t> facets;
                for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                    facets.insert(facets.end(), facet);
                }
                assign_if_changed(entry.facets, std::move(facets));
                break;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
        flush_entry(geo_mesh, m_mode, entry);
    }
}

void Mesh_component_selection::select_none()
{
    clear_all();
}

void Mesh_component_selection::invert()
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (Mesh_component_entry& entry : m_entries) {
        if (!is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        const GEO::Mesh& geo_mesh = geometry->get_mesh();
        switch (m_mode) {
            case Mesh_component_mode::vertex: {
                std::set<GEO::index_t> vertices;
                for (GEO::index_t vertex = 0, end = geo_mesh.vertices.nb(); vertex < end; ++vertex) {
                    if (!entry.vertices.contains(vertex)) {
                        vertices.insert(vertices.end(), vertex);
                    }
                }
                assign_if_changed(entry.vertices, std::move(vertices));
                break;
            }
            case Mesh_component_mode::edge: {
                std::set<Mesh_edge_key> edges;
                for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                    const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                    for (GEO::index_t i = 0; i < corner_count; ++i) {
                        const Mesh_edge_key key = make_edge_key(corner_vertex(geo_mesh, facet, i), next_corner_vertex(geo_mesh, facet, i, corner_count));
                        if (!entry.edges.contains(key)) {
                            edges.insert(key);
                        }
                    }
                }
                assign_if_changed(entry.edges, std::move(edges));
                break;
            }
            case Mesh_component_mode::face: {
                std::set<GEO::index_t> facets;
                for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                    if (!entry.facets.contains(facet)) {
                        facets.insert(facets.end(), facet);
                    }
                }
                assign_if_changed(entry.facets, std::move(facets));
                break;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
        flush_entry(geo_mesh, m_mode, entry);
    }
}

auto Mesh_component_selection::add_linked_region(
    Mesh_component_entry&                entry,
    const erhe::geometry::Geometry&      geometry,
    const erhe::geometry::Region_delimit delimit
) -> bool
{
    // The walk needs vertex -> edge adjacency. It is built by whoever owns
    // the Geometry (process flags at creation); the selection never builds
    // connectivity on a shared scene Geometry.
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        log_selection->warn("Select linked: geometry '{}' has no connectivity; skipped", geometry.get_name());
        return false;
    }
    const GEO::Mesh& geo_mesh = geometry.get_mesh();
    erhe::geometry::walk_connected_region(geometry, m_seed_vertices, delimit, m_region_vertices);
    m_region_marks.assign(geo_mesh.vertices.nb(), std::uint8_t{0});
    for (const GEO::index_t vertex : m_region_vertices) {
        m_region_marks[vertex] = 1;
    }
    const auto in_region = [this](const GEO::index_t vertex) -> bool {
        return m_region_marks[vertex] != 0;
    };

    switch (m_mode) {
        case Mesh_component_mode::vertex: {
            std::set<GEO::index_t> vertices = entry.vertices;
            vertices.insert(m_region_vertices.begin(), m_region_vertices.end());
            assign_if_changed(entry.vertices, std::move(vertices));
            break;
        }
        case Mesh_component_mode::edge: {
            std::set<Mesh_edge_key> edges = entry.edges;
            for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t vertex = corner_vertex(geo_mesh, facet, i);
                    const GEO::index_t next   = next_corner_vertex(geo_mesh, facet, i, corner_count);
                    if (in_region(vertex) && in_region(next)) {
                        edges.insert(make_edge_key(vertex, next));
                    }
                }
            }
            assign_if_changed(entry.edges, std::move(edges));
            break;
        }
        case Mesh_component_mode::face: {
            // Facets whose vertices all lie in the region: the vertex flood
            // across non-delimit edges stands in for a facet flood.
            std::set<GEO::index_t> facets = entry.facets;
            for (GEO::index_t facet = 0, facet_end = geo_mesh.facets.nb(); facet < facet_end; ++facet) {
                const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                bool all_in_region = true;
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    if (!in_region(corner_vertex(geo_mesh, facet, i))) {
                        all_in_region = false;
                        break;
                    }
                }
                if (all_in_region) {
                    facets.insert(facet);
                }
            }
            assign_if_changed(entry.facets, std::move(facets));
            break;
        }
        case Mesh_component_mode::object:
        default: {
            break;
        }
    }
    flush_entry(geo_mesh, m_mode, entry);
    return true;
}

void Mesh_component_selection::select_linked_from_selection(const erhe::geometry::Region_delimit delimit)
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (Mesh_component_entry& entry : m_entries) {
        if (!is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        const GEO::Mesh& geo_mesh = geometry->get_mesh();
        m_seed_vertices.clear();
        m_seed_vertices.insert(m_seed_vertices.end(), entry.vertices.begin(), entry.vertices.end());
        for (const Mesh_edge_key& key : entry.edges) {
            m_seed_vertices.push_back(key.first);
            m_seed_vertices.push_back(key.second);
        }
        for (const GEO::index_t facet : entry.facets) {
            for (GEO::index_t i = 0, corner_count = geo_mesh.facets.nb_corners(facet); i < corner_count; ++i) {
                m_seed_vertices.push_back(corner_vertex(geo_mesh, facet, i));
            }
        }
        if (m_seed_vertices.empty()) {
            continue;
        }
        static_cast<void>(add_linked_region(entry, *geometry, delimit));
    }
}

auto Mesh_component_selection::select_linked(
    const Mesh_component_target&         target,
    const std::span<const GEO::index_t>  seed_vertices,
    const erhe::geometry::Region_delimit delimit
) -> bool
{
    if (!is_mesh_component_mode(m_mode) || !target.mesh || !target.geometry) {
        return false;
    }
    const erhe::geometry::Geometry& geometry = *target.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        log_selection->warn("Select linked: geometry '{}' has no connectivity; skipped", geometry.get_name());
        return false;
    }
    m_seed_vertices.assign(seed_vertices.begin(), seed_vertices.end());
    Mesh_component_entry& entry = find_or_create_entry(target.mesh, target.primitive_index, target.geometry);
    return add_linked_region(entry, geometry, delimit);
}

namespace {

[[nodiscard]] auto loop_kind_applies(const Loop_kind kind, const Mesh_component_mode mode) -> bool
{
    if (kind == Loop_kind::face_loop) {
        return mode == Mesh_component_mode::face;
    }
    return (mode == Mesh_component_mode::vertex) || (mode == Mesh_component_mode::edge);
}

} // anonymous namespace

auto Mesh_component_selection::walk_loop(
    const Mesh_component_target&            target,
    const Mesh_edge_key                     edge_key,
    const Loop_kind                         kind,
    const erhe::geometry::Edge_loop_delimit delimit
) -> bool
{
    m_loop_elements.clear();
    if (!loop_kind_applies(kind, m_mode) || !target.mesh || !target.geometry) {
        return false;
    }
    const erhe::geometry::Geometry& geometry = *target.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        log_selection->warn("Loop select: geometry '{}' has no connectivity; skipped", geometry.get_name());
        return false;
    }
    return walk_mesh_loop(geometry, edge_key, kind, delimit, m_loop_elements) && !m_loop_elements.empty();
}

auto Mesh_component_selection::select_loop(
    const Mesh_component_target&            target,
    const Mesh_edge_key                     edge_key,
    const Loop_kind                         kind,
    const Select_action                     action,
    const erhe::geometry::Edge_loop_delimit delimit
) -> std::size_t
{
    if (!walk_loop(target, edge_key, kind, delimit)) {
        return 0;
    }
    if (action == Select_action::replace) {
        clear_all();
    }
    const bool            add      = (action != Select_action::deselect);
    const GEO::Mesh&      geo_mesh = target.geometry->get_mesh();
    Mesh_component_entry& entry    = find_or_create_entry(target.mesh, target.primitive_index, target.geometry);
    const auto apply_vertex = [&entry, add](const GEO::index_t vertex) {
        if (add) { entry.vertices.insert(vertex); } else { entry.vertices.erase(vertex); }
    };
    for (const GEO::index_t element : m_loop_elements) {
        switch (m_mode) {
            case Mesh_component_mode::vertex: {
                apply_vertex(geo_mesh.edges.vertex(element, 0));
                apply_vertex(geo_mesh.edges.vertex(element, 1));
                break;
            }
            case Mesh_component_mode::edge: {
                const Mesh_edge_key key = make_edge_key(geo_mesh.edges.vertex(element, 0), geo_mesh.edges.vertex(element, 1));
                if (add) { entry.edges.insert(key); } else { entry.edges.erase(key); }
                break;
            }
            case Mesh_component_mode::face: {
                if (add) { entry.facets.insert(element); } else { entry.facets.erase(element); }
                break;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
    }
    flush_entry(geo_mesh, m_mode, entry);
    return m_loop_elements.size();
}

auto Mesh_component_selection::is_loop_selected(
    const Mesh_component_target&            target,
    const Mesh_edge_key                     edge_key,
    const Loop_kind                         kind,
    const erhe::geometry::Edge_loop_delimit delimit
) -> bool
{
    if (!walk_loop(target, edge_key, kind, delimit)) {
        return false;
    }
    const Mesh_component_entry* const entry = find_entry(target.mesh, target.primitive_index, target.geometry);
    if (entry == nullptr) {
        return false;
    }
    const GEO::Mesh& geo_mesh = target.geometry->get_mesh();
    for (const GEO::index_t element : m_loop_elements) {
        switch (m_mode) {
            case Mesh_component_mode::vertex: {
                if (!entry->vertices.contains(geo_mesh.edges.vertex(element, 0)) || !entry->vertices.contains(geo_mesh.edges.vertex(element, 1))) {
                    return false;
                }
                break;
            }
            case Mesh_component_mode::edge: {
                if (!entry->edges.contains(make_edge_key(geo_mesh.edges.vertex(element, 0), geo_mesh.edges.vertex(element, 1)))) {
                    return false;
                }
                break;
            }
            case Mesh_component_mode::face: {
                if (!entry->facets.contains(element)) {
                    return false;
                }
                break;
            }
            case Mesh_component_mode::object:
            default: {
                return false;
            }
        }
    }
    return true;
}
#pragma endregion Selection commands

#pragma region Grow / Shrink
namespace {

// One-ring grow/shrink helpers. Each reads the entry's current set for one mode,
// computes the new set from a snapshot of the old one (so a single step does not
// cascade within itself), then assigns it back. Adjacency comes from the already
// built Geometry connectivity (facets.connect / build_edges). This is a cold path
// (user keypress / button), so transient containers here are acceptable.

void grow_facets(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&       mesh   = geometry.get_mesh();
    std::set<GEO::index_t> result = entry.facets;
    for (const GEO::index_t facet : entry.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t i = 0; i < corner_count; ++i) {
            const GEO::index_t corner = mesh.facets.corner(facet, i);
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            // Blender "Face Step" (the Select More default): a face grows across
            // shared *vertices*, not just shared edges, so the diagonal neighbors
            // that touch the selection at a single corner are included too. Add
            // every facet incident to each of this facet's vertices.
            for (const GEO::index_t vertex_corner : geometry.get_vertex_corners(vertex)) {
                result.insert(geometry.get_corner_facet(vertex_corner));
            }
        }
    }
    entry.facets = std::move(result);
}

void shrink_facets(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&              mesh     = geometry.get_mesh();
    const std::set<GEO::index_t>& selected = entry.facets;
    std::set<GEO::index_t>        result;
    for (const GEO::index_t facet : selected) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        bool is_border = false;
        for (GEO::index_t local_edge = 0; local_edge < corner_count; ++local_edge) {
            const GEO::index_t neighbor = mesh.facets.adjacent(facet, local_edge);
            // A mesh boundary (no neighbor) or an unselected neighbor makes this a
            // border facet.
            if ((neighbor == GEO::NO_INDEX) || (selected.find(neighbor) == selected.end())) {
                is_border = true;
                break;
            }
        }
        if (!is_border) {
            result.insert(facet);
        }
    }
    entry.facets = std::move(result);
}

void grow_edges(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&        mesh   = geometry.get_mesh();
    std::set<Mesh_edge_key> result = entry.edges;
    for (const Mesh_edge_key& key : entry.edges) {
        const GEO::index_t endpoints[2] = {key.first, key.second};
        for (const GEO::index_t vertex : endpoints) {
            for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
                result.insert(make_edge_key(mesh.edges.vertex(edge, 0), mesh.edges.vertex(edge, 1)));
            }
        }
    }
    entry.edges = std::move(result);
}

void shrink_edges(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&               mesh     = geometry.get_mesh();
    const std::set<Mesh_edge_key>& selected = entry.edges;

    // A vertex is interior when every edge incident to it is selected; an edge
    // is kept only when both its endpoints are interior.
    const auto is_interior_vertex = [&](const GEO::index_t vertex) -> bool {
        for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
            const Mesh_edge_key key = make_edge_key(mesh.edges.vertex(edge, 0), mesh.edges.vertex(edge, 1));
            if (selected.find(key) == selected.end()) {
                return false;
            }
        }
        return true;
    };

    std::set<Mesh_edge_key> result;
    for (const Mesh_edge_key& key : selected) {
        if (is_interior_vertex(key.first) && is_interior_vertex(key.second)) {
            result.insert(key);
        }
    }
    entry.edges = std::move(result);
}

void grow_vertices(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&       mesh   = geometry.get_mesh();
    std::set<GEO::index_t> result = entry.vertices;
    for (const GEO::index_t vertex : entry.vertices) {
        for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
            const GEO::index_t a     = mesh.edges.vertex(edge, 0);
            const GEO::index_t b     = mesh.edges.vertex(edge, 1);
            const GEO::index_t other = (a == vertex) ? b : a;
            result.insert(other);
        }
    }
    entry.vertices = std::move(result);
}

void shrink_vertices(const erhe::geometry::Geometry& geometry, Mesh_component_entry& entry)
{
    const GEO::Mesh&              mesh     = geometry.get_mesh();
    const std::set<GEO::index_t>& selected = entry.vertices;
    std::set<GEO::index_t>        result;
    for (const GEO::index_t vertex : selected) {
        bool is_border = false;
        for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
            const GEO::index_t a     = mesh.edges.vertex(edge, 0);
            const GEO::index_t b     = mesh.edges.vertex(edge, 1);
            const GEO::index_t other = (a == vertex) ? b : a;
            if (selected.find(other) == selected.end()) {
                is_border = true;
                break;
            }
        }
        if (!is_border) {
            result.insert(vertex);
        }
    }
    entry.vertices = std::move(result);
}

} // anonymous namespace

void Mesh_component_selection::grow()
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (Mesh_component_entry& entry : m_entries) {
        if (!is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        if (!geometry) {
            continue;
        }
        if (!geometry->has_connectivity() || !geometry->has_edge_connectivity()) {
            log_selection->warn("Grow selection: geometry '{}' has no connectivity; skipped", geometry->get_name());
            continue;
        }
        switch (m_mode) {
            case Mesh_component_mode::vertex: grow_vertices(*geometry, entry); break;
            case Mesh_component_mode::edge:   grow_edges   (*geometry, entry); break;
            case Mesh_component_mode::face:   grow_facets  (*geometry, entry); break;
            case Mesh_component_mode::object: break;
            default:                          break;
        }
        flush_entry(geometry->get_mesh(), m_mode, entry);
    }
}

void Mesh_component_selection::shrink()
{
    if (!is_mesh_component_mode(m_mode)) {
        return;
    }
    for (Mesh_component_entry& entry : m_entries) {
        if (!is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        if (!geometry) {
            continue;
        }
        if (!geometry->has_connectivity() || !geometry->has_edge_connectivity()) {
            log_selection->warn("Shrink selection: geometry '{}' has no connectivity; skipped", geometry->get_name());
            continue;
        }
        switch (m_mode) {
            case Mesh_component_mode::vertex: shrink_vertices(*geometry, entry); break;
            case Mesh_component_mode::edge:   shrink_edges   (*geometry, entry); break;
            case Mesh_component_mode::face:   shrink_facets  (*geometry, entry); break;
            case Mesh_component_mode::object: break;
            default:                          break;
        }
        flush_entry(geometry->get_mesh(), m_mode, entry);
    }
}
#pragma endregion Grow / Shrink

void Mesh_component_selection::set_after_operation(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& after_geometry,
    const std::set<GEO::index_t>&                    vertices,
    const std::set<GEO::index_t>&                    facets,
    const std::set<Mesh_edge_key>&                   edges
)
{
    if (vertices.empty() && facets.empty() && edges.empty()) {
        return;
    }
    Mesh_component_entry& entry = find_or_create_entry(mesh, primitive_index, after_geometry);
    entry.vertices = vertices;
    entry.facets   = facets;
    entry.edges    = edges;
}

auto Mesh_component_selection::is_empty() const -> bool
{
    for (const Mesh_component_entry& entry : m_entries) {
        if (!entry.is_empty()) {
            return false;
        }
    }
    return true;
}

auto Mesh_component_selection::has_live_mode_selection() const -> bool
{
    if (!is_mesh_component_mode(m_mode)) {
        return false;
    }
    for (const Mesh_component_entry& entry : m_entries) {
        const bool has_mode_set =
            ((m_mode == Mesh_component_mode::vertex) && !entry.vertices.empty()) ||
            ((m_mode == Mesh_component_mode::edge  ) && !entry.edges   .empty()) ||
            ((m_mode == Mesh_component_mode::face  ) && !entry.facets  .empty());
        if (has_mode_set && is_live(entry)) {
            return true;
        }
    }
    return false;
}

void Mesh_component_selection::prune()
{
    std::erase_if(
        m_entries,
        [](const Mesh_component_entry& entry) {
            return entry.mesh.expired() || entry.geometry.expired() || entry.is_empty();
        }
    );
}

auto Mesh_component_selection::is_live(const Mesh_component_entry& entry) const -> bool
{
    return is_live(entry.mesh.lock(), entry.primitive_index, entry.geometry.lock());
}

auto Mesh_component_selection::is_live(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::size_t                                primitive_index,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
) const -> bool
{
    if (!mesh) {
        return false;
    }
    const erhe::scene::Node* node = mesh.get();
    if ((node == nullptr) || (node->get_item_host() == nullptr)) {
        return false; // mesh removed from the scene (e.g. an undone insert)
    }
    if (!geometry) {
        return false;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitive_index >= primitives.size()) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[primitive_index].primitive;
    if (!primitive) {
        return false;
    }
    // Two-geometry primitives (a separate collision Primitive_shape) are not
    // valid component-selection targets: the picked/stored geometry is the
    // collision geometry, which differs from the render geometry, so neither
    // rendering the highlight nor editing the vertices would match the visible
    // mesh. Both tool_render and Mesh_component_transform::gather rely on this.
    if (primitive->collision_shape) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive->get_shape_for_raytrace();
    if (!shape) {
        return false;
    }
    // Live only if the primitive still carries the exact Geometry these indices
    // address; a swap installs a different object and the entry goes dormant.
    // Non-blocking: this is an identity comparison against a Geometry the
    // caller already holds, so if there is one it was published. is_live() is
    // called every frame from several tools; it must never build.
    return shape->get_geometry_const() == geometry;
}

auto Mesh_component_selection::get_entries() -> std::vector<Mesh_component_entry>&
{
    return m_entries;
}

auto Mesh_component_selection::get_entries() const -> const std::vector<Mesh_component_entry>&
{
    return m_entries;
}

} // namespace editor
