#include "geometry_graph/nodes/lattice_node.hpp"

#include "app_context.hpp"
#include "graph_editor/graph_editor_widgets.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_scene/node.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto offset_index(const glm::ivec3& divisions, const int i, const int j, const int k) -> std::size_t
{
    return erhe::geometry::operation::lattice_offset_index(divisions, i, j, k);
}

// Trilinear sample of an offset field at normalized lattice coordinates
[[nodiscard]] auto sample_offsets(const glm::ivec3& divisions, const std::vector<glm::vec3>& offsets, const glm::vec3 stu) -> glm::vec3
{
    glm::ivec3 cell;
    glm::vec3  fraction;
    for (int axis = 0; axis < 3; ++axis) {
        const float scaled = stu[axis] * static_cast<float>(divisions[axis]);
        const int   i      = std::min(static_cast<int>(scaled), divisions[axis] - 1);
        cell    [axis] = i;
        fraction[axis] = scaled - static_cast<float>(i);
    }
    glm::vec3 result{0.0f};
    for (int corner = 0; corner < 8; ++corner) {
        const int di = (corner     ) & 1;
        const int dj = (corner >> 1) & 1;
        const int dk = (corner >> 2) & 1;
        const float weight =
            ((di == 1) ? fraction.x : 1.0f - fraction.x) *
            ((dj == 1) ? fraction.y : 1.0f - fraction.y) *
            ((dk == 1) ? fraction.z : 1.0f - fraction.z);
        result += weight * offsets[offset_index(divisions, cell.x + di, cell.y + dj, cell.z + dk)];
    }
    return result;
}

} // anonymous namespace

Lattice_node::Lattice_node(App_context& context)
    : Geometry_graph_node{"Lattice"}
    , m_context          {context}
{
    make_input_pin(Geometry_pin_key::geometry,  "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
    resample_offsets(glm::ivec3{0, 0, 0}, {});
    m_transform_node_reference.set_user_label("lattice transform driver");
}

void Lattice_node::set_transform_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    m_transform_node_reference.adopt(*m_context.asset_manager, node);
    capture_transform();
    mark_dirty();
}

void Lattice_node::resolve_transform_reference()
{
    // Main thread only (the manager verifies).
    if (m_transform_node_reference.get()) {
        return;
    }
    if (m_transform_node_reference.get_key().name.empty()) {
        return;
    }
    m_transform_node_reference.resolve(*m_context.asset_manager);
}

auto Lattice_node::capture_transform() -> bool
{
    // The driver's LOCAL (parent-relative) transform is the cage frame:
    // parent the driver under the scene node the graph is bound to and the
    // cage rides with the mesh - moving the parent moves mesh and cage
    // together (no re-deformation), moving the driver moves the cage
    // relative to the mesh.
    const std::shared_ptr<erhe::scene::Node> node = m_transform_node_reference.get_as<erhe::scene::Node>();
    const glm::mat4 transform = node ? node->parent_from_node() : glm::mat4{1.0f};
    if (transform == m_captured_transform) {
        return false;
    }
    m_captured_transform = transform;
    return true;
}

void Lattice_node::update_live()
{
    // Per-frame: retry a deferred driver reference (scene_local misses do
    // not latch) and track the driver's live transform, so dragging the
    // driver node in the viewport re-deforms the geometry.
    resolve_transform_reference();
    if (capture_transform()) {
        mark_dirty();
    }
}

void Lattice_node::prepare_for_evaluation()
{
    update_live();
}

void Lattice_node::capture_evaluation_state(const Geometry_graph_node& live_node)
{
    // Shadow clones copy the live node's captured transform instead of
    // resolving: a shadow must not touch the asset manager or scene state
    // (it can be destroyed off the main thread).
    const Lattice_node* live = dynamic_cast<const Lattice_node*>(&live_node);
    if (live != nullptr) {
        m_captured_transform = live->m_captured_transform;
    }
}

auto Lattice_node::has_deformation() const -> bool
{
    return std::any_of(
        m_offsets.begin(), m_offsets.end(),
        [](const glm::vec3& offset) { return offset != glm::vec3{0.0f}; }
    );
}

void Lattice_node::resample_offsets(const glm::ivec3 old_divisions, const std::vector<glm::vec3>& old_offsets)
{
    m_divisions = glm::clamp(m_divisions, glm::ivec3{1}, glm::ivec3{max_divisions});
    std::vector<glm::vec3> new_offsets(erhe::geometry::operation::lattice_control_point_count(m_divisions), glm::vec3{0.0f});
    const bool old_valid =
        (old_divisions.x >= 1) && (old_divisions.y >= 1) && (old_divisions.z >= 1) &&
        (old_offsets.size() == erhe::geometry::operation::lattice_control_point_count(old_divisions));
    if (old_valid) {
        for (int k = 0; k <= m_divisions.z; ++k) {
            for (int j = 0; j <= m_divisions.y; ++j) {
                for (int i = 0; i <= m_divisions.x; ++i) {
                    const glm::vec3 stu{
                        static_cast<float>(i) / static_cast<float>(m_divisions.x),
                        static_cast<float>(j) / static_cast<float>(m_divisions.y),
                        static_cast<float>(k) / static_cast<float>(m_divisions.z)
                    };
                    new_offsets[offset_index(m_divisions, i, j, k)] = sample_offsets(old_divisions, old_offsets, stu);
                }
            }
        }
    }
    m_offsets = std::move(new_offsets);
    m_selected_point = glm::clamp(m_selected_point, glm::ivec3{0}, m_divisions);
}

void Lattice_node::set_selected_point(const glm::ivec3 point)
{
    m_selected_point = glm::clamp(point, glm::ivec3{0}, m_divisions);
}

auto Lattice_node::get_control_point_offset(const glm::ivec3 point) const -> glm::vec3
{
    const glm::ivec3 clamped = glm::clamp(point, glm::ivec3{0}, m_divisions);
    return m_offsets[offset_index(m_divisions, clamped.x, clamped.y, clamped.z)];
}

void Lattice_node::set_control_point_offset(const glm::ivec3 point, const glm::vec3 offset)
{
    const glm::ivec3 clamped = glm::clamp(point, glm::ivec3{0}, m_divisions);
    glm::vec3& slot = m_offsets[offset_index(m_divisions, clamped.x, clamped.y, clamped.z)];
    if (slot == offset) {
        return;
    }
    slot = offset;
    mark_dirty();
}

auto Lattice_node::resolve_cage(glm::vec3& out_cage_min, glm::vec3& out_cage_max) const -> bool
{
    if (m_auto_fit) {
        const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
        if (!source) {
            return false;
        }
        const erhe::math::Aabb aabb = source->get_aabb();
        out_cage_min = aabb.min;
        out_cage_max = aabb.max;
    } else {
        out_cage_min = m_cage_min;
        out_cage_max = m_cage_max;
    }
    // Pad degenerate axes (flat or point-like input, or a collapsed manual
    // cage) so the deformation stays well defined; clamped normalization
    // places such vertices mid-cage on the padded axis.
    for (int axis = 0; axis < 3; ++axis) {
        if (out_cage_max[axis] - out_cage_min[axis] < 1e-6f) {
            const float center = 0.5f * (out_cage_min[axis] + out_cage_max[axis]);
            out_cage_min[axis] = center - 1e-3f;
            out_cage_max[axis] = center + 1e-3f;
        }
    }
    return true;
}

void Lattice_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    // The cage wireframe is emitted into the destination's debug lines, so
    // even an identity lattice must produce a copy when the cage is shown -
    // nodes never mutate shared upstream geometry. The cage transform alone
    // never deforms (zero offsets stay zero displacements in any cage frame),
    // so it does not gate the pass-through.
    if (!has_deformation() && !m_show_cage) {
        set_output(0, Geometry_payload{.value = source});
        return;
    }

    erhe::geometry::operation::Lattice_deform_parameters parameters;
    parameters.divisions             = m_divisions;
    parameters.interpolation         = static_cast<erhe::geometry::operation::Lattice_interpolation>(m_interpolation);
    parameters.control_point_offsets = m_offsets;
    parameters.regenerate_attributes = m_regenerate_attributes;
    parameters.make_cage_debug_lines = m_show_cage;
    const bool cage_ok = resolve_cage(parameters.cage_min, parameters.cage_max);
    if (!cage_ok) { // cannot happen - the source null check above guarantees a fit target
        set_output(0, Geometry_payload{.value = source});
        return;
    }
    parameters.cage_transform = m_captured_transform;

    std::shared_ptr<erhe::geometry::Geometry> destination = std::make_shared<erhe::geometry::Geometry>("lattice deformed");
    // No process_for_graph() here: lattice_deform() post-processes internally
    // with at least connect + build_edges, re-running them would be redundant.
    erhe::geometry::operation::lattice_deform(*source.get(), *destination.get(), parameters);
    set_output(0, Geometry_payload{.value = destination});
}

void Lattice_node::imgui()
{
    // Transform-driver row: a drop target for a scene node dragged from the
    // item tree (payload type = the item's leaf class name carrying an
    // erhe::Item_base*, see windows/item_tree_window.cpp). The driver's
    // local transform is the cage frame - moving it moves the cage relative
    // to the geometry.
    resolve_transform_reference();
    {
        const std::shared_ptr<erhe::scene::Node> current = m_transform_node_reference.get_as<erhe::scene::Node>();
        ImGui::TextUnformatted("Transform node");
        const std::string label = current
            ? current->get_name()
            : (!m_transform_node_reference.get_key().name.empty()
                ? "(unresolved: " + m_transform_node_reference.get_key().name + ")"
                : "(drop a scene node)");
        ImGui::Button(label.c_str(), ImVec2{140.0f * content_scale(), 0.0f});
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(erhe::scene::Node::static_type_name.data());
            if ((payload != nullptr) && (payload->Data != nullptr) && (payload->DataSize == sizeof(erhe::Item_base*))) {
                erhe::Item_base* const   raw      = *static_cast<erhe::Item_base**>(payload->Data);
                erhe::scene::Node* const node_raw = dynamic_cast<erhe::scene::Node*>(raw);
                if (node_raw != nullptr) {
                    set_transform_node(std::dynamic_pointer_cast<erhe::scene::Node>(node_raw->shared_from_this()));
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (current || !m_transform_node_reference.get_key().name.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("x##clear_transform_node")) {
                set_transform_node({});
            }
        }
    }

    if (ImGui::Checkbox("Auto fit cage", &m_auto_fit)) {
        if (!m_auto_fit) {
            // Freeze the fitted cage: keep deforming from the bounds the
            // lattice was authored against instead of tracking the input.
            const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
            if (source) {
                const erhe::math::Aabb aabb = source->get_aabb();
                m_cage_min = aabb.min;
                m_cage_max = aabb.max;
            }
        }
        mark_dirty();
    }
    if (!m_auto_fit) {
        ImGui::TextUnformatted("Cage min");
        ImGui::SetNextItemWidth(140.0f * content_scale());
        if (ImGui::DragFloat3("##cage_min", &m_cage_min.x, 0.01f)) { mark_dirty(); }
        ImGui::TextUnformatted("Cage max");
        ImGui::SetNextItemWidth(140.0f * content_scale());
        if (ImGui::DragFloat3("##cage_max", &m_cage_max.x, 0.01f)) { mark_dirty(); }
    }

    ImGui::TextUnformatted("Divisions");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    glm::ivec3 divisions = m_divisions;
    if (ImGui::DragInt3("##divisions", &divisions.x, 0.1f, 1, max_divisions)) {
        const glm::ivec3             old_divisions = m_divisions;
        const std::vector<glm::vec3> old_offsets   = std::move(m_offsets);
        m_divisions = divisions;
        resample_offsets(old_divisions, old_offsets);
        mark_dirty();
    }

    const char* interpolation_names[] = { "Trilinear", "Bezier" };
    if (imgui_enum_combo("interpolation", m_interpolation, interpolation_names, IM_ARRAYSIZE(interpolation_names), content_scale())) {
        mark_dirty();
    }

    if (ImGui::Checkbox("Show cage",         &m_show_cage))             { mark_dirty(); }
    if (ImGui::Checkbox("Regenerate normals", &m_regenerate_attributes)) { mark_dirty(); }

    ImGui::TextUnformatted("Control point");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt3("##selected_point", &m_selected_point.x, 0.1f, 0, max_divisions)) {
        m_selected_point = glm::clamp(m_selected_point, glm::ivec3{0}, m_divisions);
        // Selection is a UI affair - the output is unchanged, no mark_dirty()
    }
    m_selected_point = glm::clamp(m_selected_point, glm::ivec3{0}, m_divisions);
    glm::vec3& selected_offset = m_offsets[offset_index(m_divisions, m_selected_point.x, m_selected_point.y, m_selected_point.z)];
    ImGui::TextUnformatted("Offset");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat3("##offset", &selected_offset.x, 0.01f)) { mark_dirty(); }
    if (ImGui::Button("Reset all offsets")) {
        std::fill(m_offsets.begin(), m_offsets.end(), glm::vec3{0.0f});
        mark_dirty();
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Lattice_node::write_parameters(nlohmann::json& out) const
{
    // Written even while unresolved (and when empty), so an unresolved
    // reference survives save and an undo to the no-driver state clears it.
    out["transform_node"] = m_transform_node_reference.get_key().name;
    out["auto_fit"] = m_auto_fit;
    write_vec3 (out, "cage_min",  m_cage_min);
    write_vec3 (out, "cage_max",  m_cage_max);
    write_ivec3(out, "divisions", m_divisions);
    out["interpolation"]         = m_interpolation;
    out["regenerate_attributes"] = m_regenerate_attributes;
    out["show_cage"]             = m_show_cage;
    nlohmann::json offsets = nlohmann::json::array();
    for (const glm::vec3& offset : m_offsets) {
        offsets.push_back(offset.x);
        offsets.push_back(offset.y);
        offsets.push_back(offset.z);
    }
    out["offsets"] = std::move(offsets);
}

void Lattice_node::read_parameters(const nlohmann::json& in)
{
    if (in.contains("transform_node")) {
        // Store the key only; resolution is deferred to the main thread
        // (read_parameters can run off it - shadow snapshots). The captured
        // transform resets so a cleared / changed driver does not leave a
        // stale transform; update_live() re-captures after resolution.
        const std::string name = in.value("transform_node", std::string{});
        if (name != m_transform_node_reference.get_key().name) {
            Asset_key key;
            key.scope = Asset_scope::scene_local;
            key.type  = Asset_type::node;
            key.name  = name;
            m_transform_node_reference.set_key(key);
            m_captured_transform = glm::mat4{1.0f};
        }
    }
    m_auto_fit              = in.value("auto_fit", m_auto_fit);
    m_cage_min              = read_vec3 (in, "cage_min",  m_cage_min);
    m_cage_max              = read_vec3 (in, "cage_max",  m_cage_max);
    m_divisions             = read_ivec3(in, "divisions", m_divisions);
    m_interpolation         = std::clamp(in.value("interpolation", m_interpolation), 0, 1);
    m_regenerate_attributes = in.value("regenerate_attributes", m_regenerate_attributes);
    m_show_cage             = in.value("show_cage", m_show_cage);
    resample_offsets(glm::ivec3{0, 0, 0}, {}); // clamps divisions, zero-fills at the right size
    const auto offsets_it = in.find("offsets");
    if ((offsets_it != in.end()) && offsets_it->is_array() && (offsets_it->size() == m_offsets.size() * 3)) {
        for (std::size_t i = 0; i < m_offsets.size(); ++i) {
            m_offsets[i] = glm::vec3{
                offsets_it->at(3 * i    ).get<float>(),
                offsets_it->at(3 * i + 1).get<float>(),
                offsets_it->at(3 * i + 2).get<float>()
            };
        }
    }
    mark_dirty();
}

}
