#include "geometry_graph/nodes/sdf_nodes.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_voxel/voxel.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace editor {

namespace {

constexpr float c_min_voxel_size = 0.005f;
constexpr float c_max_voxel_size = 1.0f;

[[nodiscard]] auto make_create_info(const Sdf_create_parameters& parameters) -> erhe::voxel::Grid_create_info
{
    return erhe::voxel::Grid_create_info{
        .voxel_size        = std::clamp(parameters.voxel_size, c_min_voxel_size, c_max_voxel_size),
        .narrow_band_width = 3
    };
}

void show_sdf_stats(const Geometry_payload& output)
{
    const std::shared_ptr<erhe::voxel::Grid> sdf = output.get_sdf();
    if (sdf) {
        ImGui::Text("Voxels: %lld", static_cast<long long>(sdf->get_active_voxel_count()));
    }
}

} // anonymous namespace

auto Sdf_create_parameters::imgui(const float content_scale) -> bool
{
    ImGui::TextUnformatted("Voxel size");
    ImGui::SetNextItemWidth(140.0f * content_scale);
    return ImGui::DragFloat("##voxel_size", &voxel_size, 0.001f, c_min_voxel_size, c_max_voxel_size, "%.3f");
}

void Sdf_create_parameters::write(nlohmann::json& out) const
{
    out["voxel_size"] = voxel_size;
}

void Sdf_create_parameters::read(const nlohmann::json& in)
{
    voxel_size = in.value("voxel_size", voxel_size);
}

// Sdf_sphere_node

Sdf_sphere_node::Sdf_sphere_node()
    : Geometry_graph_node{"SDF Sphere"}
{
    make_input_pin(Geometry_pin_key::float_value, "radius");
    make_input_pin(Geometry_pin_key::vec3_value,  "center");
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_sphere_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const float     radius = std::max(0.001f, get_input(0).get_float(m_radius));
    const glm::vec3 center = get_input(1).get_vec3(m_center);

    std::shared_ptr<erhe::voxel::Grid> sdf = std::make_shared<erhe::voxel::Grid>(
        erhe::voxel::Grid::make_sphere(make_create_info(m_create_parameters), center, radius)
    );
    set_output(0, Geometry_payload{.value = sdf});
}

void Sdf_sphere_node::imgui()
{
    if (m_create_parameters.imgui(content_scale())) { mark_dirty(); }
    ImGui::TextUnformatted("Radius");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##radius", &m_radius, 0.01f, 0.001f, 1000.0f)) { mark_dirty(); }
    ImGui::TextUnformatted("Center");
    ImGui::SetNextItemWidth(220.0f * content_scale());
    if (ImGui::DragFloat3("##center", &m_center.x, 0.01f)) { mark_dirty(); }
    show_sdf_stats(get_output(0));
}

void Sdf_sphere_node::write_parameters(nlohmann::json& out) const
{
    m_create_parameters.write(out);
    out["radius"] = m_radius;
    write_vec3(out, "center", m_center);
}

void Sdf_sphere_node::read_parameters(const nlohmann::json& in)
{
    m_create_parameters.read(in);
    m_radius = in.value("radius", m_radius);
    m_center = read_vec3(in, "center", m_center);
    mark_dirty();
}

// Sdf_capsule_node

Sdf_capsule_node::Sdf_capsule_node()
    : Geometry_graph_node{"SDF Capsule"}
{
    make_input_pin(Geometry_pin_key::vec3_value,  "p0");
    make_input_pin(Geometry_pin_key::vec3_value,  "p1");
    make_input_pin(Geometry_pin_key::float_value, "radius 0");
    make_input_pin(Geometry_pin_key::float_value, "radius 1");
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_capsule_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const glm::vec3 p0      = get_input(0).get_vec3(m_p0);
    const glm::vec3 p1      = get_input(1).get_vec3(m_p1);
    const float     radius0 = std::max(0.001f, get_input(2).get_float(m_radius0));
    const float     radius1 = std::max(0.001f, get_input(3).get_float(m_radius1));

    std::shared_ptr<erhe::voxel::Grid> sdf = std::make_shared<erhe::voxel::Grid>(
        erhe::voxel::Grid::make_capsule(make_create_info(m_create_parameters), p0, p1, radius0, radius1)
    );
    set_output(0, Geometry_payload{.value = sdf});
}

void Sdf_capsule_node::imgui()
{
    if (m_create_parameters.imgui(content_scale())) { mark_dirty(); }
    ImGui::TextUnformatted("P0");
    ImGui::SetNextItemWidth(220.0f * content_scale());
    if (ImGui::DragFloat3("##p0", &m_p0.x, 0.01f)) { mark_dirty(); }
    ImGui::TextUnformatted("P1");
    ImGui::SetNextItemWidth(220.0f * content_scale());
    if (ImGui::DragFloat3("##p1", &m_p1.x, 0.01f)) { mark_dirty(); }
    ImGui::TextUnformatted("Radius 0 / 1");
    ImGui::SetNextItemWidth(100.0f * content_scale());
    if (ImGui::DragFloat("##radius0", &m_radius0, 0.01f, 0.001f, 1000.0f)) { mark_dirty(); }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f * content_scale());
    if (ImGui::DragFloat("##radius1", &m_radius1, 0.01f, 0.001f, 1000.0f)) { mark_dirty(); }
    show_sdf_stats(get_output(0));
}

void Sdf_capsule_node::write_parameters(nlohmann::json& out) const
{
    m_create_parameters.write(out);
    write_vec3(out, "p0", m_p0);
    write_vec3(out, "p1", m_p1);
    out["radius0"] = m_radius0;
    out["radius1"] = m_radius1;
}

void Sdf_capsule_node::read_parameters(const nlohmann::json& in)
{
    m_create_parameters.read(in);
    m_p0      = read_vec3(in, "p0", m_p0);
    m_p1      = read_vec3(in, "p1", m_p1);
    m_radius0 = in.value("radius0", m_radius0);
    m_radius1 = in.value("radius1", m_radius1);
    mark_dirty();
}

// Sdf_from_geometry_node

Sdf_from_geometry_node::Sdf_from_geometry_node()
    : Geometry_graph_node{"Voxelize"}
{
    make_input_pin(Geometry_pin_key::geometry, "geometry");
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_from_geometry_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_input(0).get_geometry();
    if (!geometry) {
        set_output(0, Geometry_payload{});
        return;
    }
    std::shared_ptr<erhe::voxel::Grid> sdf = std::make_shared<erhe::voxel::Grid>(
        erhe::voxel::Grid::from_geometry(make_create_info(m_create_parameters), *geometry.get())
    );
    set_output(0, Geometry_payload{.value = sdf});
}

void Sdf_from_geometry_node::imgui()
{
    if (m_create_parameters.imgui(content_scale())) { mark_dirty(); }
    show_sdf_stats(get_output(0));
}

void Sdf_from_geometry_node::write_parameters(nlohmann::json& out) const
{
    m_create_parameters.write(out);
}

void Sdf_from_geometry_node::read_parameters(const nlohmann::json& in)
{
    m_create_parameters.read(in);
    mark_dirty();
}

// Sdf_to_geometry_node

Sdf_to_geometry_node::Sdf_to_geometry_node()
    : Geometry_graph_node{"SDF Mesh"}
{
    make_input_pin(Geometry_pin_key::sdf, "sdf");
    make_input_pin(Geometry_pin_key::float_value, "adaptivity");
    make_output_pin(Geometry_pin_key::geometry, "geometry");
}

void Sdf_to_geometry_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::voxel::Grid> sdf = get_input(0).get_sdf();
    if (!sdf || sdf->is_empty()) {
        set_output(0, Geometry_payload{});
        return;
    }
    const float adaptivity = std::clamp(get_input(1).get_float(m_adaptivity), 0.0f, 1.0f);

    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>("sdf mesh");
    sdf->to_geometry(*geometry.get(), adaptivity);
    process_for_graph(*geometry.get());
    set_output(0, Geometry_payload{.value = geometry});
}

void Sdf_to_geometry_node::imgui()
{
    ImGui::TextUnformatted("Adaptivity");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##adaptivity", &m_adaptivity, 0.01f, 0.0f, 1.0f)) { mark_dirty(); }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Sdf_to_geometry_node::write_parameters(nlohmann::json& out) const
{
    out["adaptivity"] = m_adaptivity;
}

void Sdf_to_geometry_node::read_parameters(const nlohmann::json& in)
{
    m_adaptivity = in.value("adaptivity", m_adaptivity);
    mark_dirty();
}

// Sdf_boolean_node

Sdf_boolean_node::Sdf_boolean_node()
    : Geometry_graph_node{"SDF Boolean"}
{
    make_input_pin(Geometry_pin_key::sdf, "a");
    // b is a multi-input socket: multiple links union together (see
    // Geometry_payload::operator+=) before the operation is applied.
    make_input_pin(Geometry_pin_key::sdf, "b", true);
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_boolean_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::voxel::Grid> a = get_input(0).get_sdf();
    const std::shared_ptr<erhe::voxel::Grid> b = get_input(1).get_sdf();
    if (!a || !b) {
        // Pass through whichever side is connected so partial graphs stay
        // visible (union with empty is the other operand; the mesh
        // Boolean_node behaves the same way).
        const std::shared_ptr<erhe::voxel::Grid> connected = a ? a : b;
        set_output(0, connected ? Geometry_payload{.value = connected} : Geometry_payload{});
        return;
    }
    if (a->get_voxel_size() != b->get_voxel_size()) {
        // Grids with mismatched voxel sizes cannot be combined; pass a
        // through. imgui() derives the warning from the input payloads
        // (evaluate() only ever runs on the worker's shadow clone, so a
        // member written here would never be visible on the live node).
        set_output(0, Geometry_payload{.value = a});
        return;
    }

    std::shared_ptr<erhe::voxel::Grid> result = std::make_shared<erhe::voxel::Grid>(*a.get());
    switch (m_operation) {
        case Boolean_operation::union_operation: result->union_with(*b.get()); break;
        case Boolean_operation::intersection:    result->intersect (*b.get()); break;
        case Boolean_operation::difference:      result->subtract  (*b.get()); break;
    }
    set_output(0, Geometry_payload{.value = result});
}

void Sdf_boolean_node::imgui()
{
    const char* operation_names[] = { "Union", "Intersection", "Difference" };
    int operation = static_cast<int>(m_operation);
    if (imgui_enum_combo("operation", operation, operation_names, IM_ARRAYSIZE(operation_names), content_scale())) {
        m_operation = static_cast<Boolean_operation>(operation);
        mark_dirty();
    }
    // Derived from the input payloads copied back after evaluation, not
    // from state written in evaluate() (which runs on the shadow clone).
    const std::shared_ptr<erhe::voxel::Grid> a = get_input(0).get_sdf();
    const std::shared_ptr<erhe::voxel::Grid> b = get_input(1).get_sdf();
    if (a && b && (a->get_voxel_size() != b->get_voxel_size())) {
        ImGui::TextColored(ImVec4{1.0f, 0.5f, 0.2f, 1.0f}, "Voxel size mismatch");
    }
    show_sdf_stats(get_output(0));
}

void Sdf_boolean_node::write_parameters(nlohmann::json& out) const
{
    out["operation"] = static_cast<int>(m_operation);
}

void Sdf_boolean_node::read_parameters(const nlohmann::json& in)
{
    m_operation = static_cast<Boolean_operation>(in.value("operation", static_cast<int>(m_operation)));
    mark_dirty();
}

// Sdf_offset_node

Sdf_offset_node::Sdf_offset_node()
    : Geometry_graph_node{"SDF Offset"}
{
    make_input_pin(Geometry_pin_key::sdf, "sdf");
    make_input_pin(Geometry_pin_key::float_value, "distance");
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_offset_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::voxel::Grid> source = get_input(0).get_sdf();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }
    // LevelSetFilter::offset advances in ~half-voxel CFL steps and the
    // narrow band only represents +/- background of signed distance, so
    // large offsets are both slow (thousands of renormalization passes)
    // and unrepresentable. Clamp to a small multiple of the narrow band.
    const float limit    = 2.0f * source->get_background();
    const float distance = std::clamp(get_input(1).get_float(m_distance), -limit, limit);

    std::shared_ptr<erhe::voxel::Grid> result = std::make_shared<erhe::voxel::Grid>(*source.get());
    result->offset(distance);
    set_output(0, Geometry_payload{.value = result});
}

void Sdf_offset_node::imgui()
{
    ImGui::TextUnformatted("Distance");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    // evaluate() clamps to 2x the grid background (unknown here), so keep
    // the widget range conservative for the default 0.05 voxel size.
    if (ImGui::DragFloat("##distance", &m_distance, 0.005f, -1.0f, 1.0f)) { mark_dirty(); }
    show_sdf_stats(get_output(0));
}

void Sdf_offset_node::write_parameters(nlohmann::json& out) const
{
    out["distance"] = m_distance;
}

void Sdf_offset_node::read_parameters(const nlohmann::json& in)
{
    m_distance = in.value("distance", m_distance);
    mark_dirty();
}

// Sdf_smooth_node

Sdf_smooth_node::Sdf_smooth_node()
    : Geometry_graph_node{"SDF Smooth"}
{
    make_input_pin(Geometry_pin_key::sdf, "sdf");
    make_input_pin(Geometry_pin_key::int_value, "iterations");
    make_output_pin(Geometry_pin_key::sdf, "sdf");
}

void Sdf_smooth_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::voxel::Grid> source = get_input(0).get_sdf();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }
    const int iterations = std::clamp(get_input(1).get_int(m_iterations), 0, 20);

    std::shared_ptr<erhe::voxel::Grid> result = std::make_shared<erhe::voxel::Grid>(*source.get());
    result->smooth(iterations);
    set_output(0, Geometry_payload{.value = result});
}

void Sdf_smooth_node::imgui()
{
    ImGui::TextUnformatted("Iterations");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##iterations", &m_iterations, 0.1f, 0, 20)) { mark_dirty(); }
    show_sdf_stats(get_output(0));
}

void Sdf_smooth_node::write_parameters(nlohmann::json& out) const
{
    out["iterations"] = m_iterations;
}

void Sdf_smooth_node::read_parameters(const nlohmann::json& in)
{
    m_iterations = in.value("iterations", m_iterations);
    mark_dirty();
}

}
