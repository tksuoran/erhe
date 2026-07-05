#include "geometry_graph/nodes/instance_nodes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>

namespace editor {

// Distribute_points_node

Distribute_points_node::Distribute_points_node()
    : Geometry_graph_node{"Distribute Points"}
{
    make_input_pin(Geometry_pin_key::geometry,  "in");
    make_input_pin(Geometry_pin_key::int_value, "count");
    make_input_pin(Geometry_pin_key::int_value, "seed");
    make_output_pin(Geometry_pin_key::points, "points");
}

void Distribute_points_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<erhe::geometry::Geometry> source = get_input(0).get_geometry();
    if (!source) {
        set_output(0, Geometry_payload{});
        return;
    }

    const int count = std::clamp(get_input(1).get_int(m_count), 0, 100000);
    const int seed  = get_input(2).get_int(m_seed);

    // Fan triangulate every facet and build a cumulative area table so
    // triangles can be picked with probability proportional to area.
    const GEO::Mesh& mesh = source->get_mesh();
    std::vector<glm::vec3> triangle_vertices;  // 3 per triangle
    std::vector<glm::vec3> triangle_normals;   // 1 per triangle
    std::vector<float>     cumulative_areas;   // 1 per triangle, running sum
    float total_area = 0.0f;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, 0));
        const glm::vec3 a{p0.x, p0.y, p0.z};
        for (GEO::index_t i = 1; (i + 1) < corner_count; ++i) {
            const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i));
            const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i + 1));
            const glm::vec3 b{p1.x, p1.y, p1.z};
            const glm::vec3 c{p2.x, p2.y, p2.z};
            const glm::vec3 cross_product = glm::cross(b - a, c - a);
            const float double_area = glm::length(cross_product);
            if (!(double_area > 0.0f)) {
                continue;
            }
            triangle_vertices.push_back(a);
            triangle_vertices.push_back(b);
            triangle_vertices.push_back(c);
            triangle_normals.push_back(cross_product / double_area);
            total_area += 0.5f * double_area;
            cumulative_areas.push_back(total_area);
        }
    }

    std::shared_ptr<Point_cloud> points = std::make_shared<Point_cloud>();
    if ((total_area > 0.0f) && (count > 0)) {
        points->positions.reserve(static_cast<std::size_t>(count));
        points->normals.reserve(static_cast<std::size_t>(count));
        std::mt19937 rng{static_cast<std::mt19937::result_type>(seed)};
        std::uniform_real_distribution<float> uniform_01{0.0f, 1.0f};
        for (int i = 0; i < count; ++i) {
            const float pick = uniform_01(rng) * total_area;
            const auto  it   = std::lower_bound(cumulative_areas.begin(), cumulative_areas.end(), pick);
            const std::size_t triangle =
                (it != cumulative_areas.end())
                    ? static_cast<std::size_t>(std::distance(cumulative_areas.begin(), it))
                    : (cumulative_areas.size() - 1);
            const glm::vec3& a = triangle_vertices[(3 * triangle) + 0];
            const glm::vec3& b = triangle_vertices[(3 * triangle) + 1];
            const glm::vec3& c = triangle_vertices[(3 * triangle) + 2];
            float u = uniform_01(rng);
            float v = uniform_01(rng);
            if ((u + v) > 1.0f) { // reflect into the triangle
                u = 1.0f - u;
                v = 1.0f - v;
            }
            points->positions.push_back(a + (u * (b - a)) + (v * (c - a)));
            points->normals.push_back(triangle_normals[triangle]);
        }
    }
    set_output(0, Geometry_payload{.value = points});
}

void Distribute_points_node::imgui()
{
    ImGui::TextUnformatted("Count");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##count", &m_count, 1.0f, 0, 100000)) { mark_dirty(); }
    ImGui::TextUnformatted("Seed");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragInt("##seed", &m_seed, 0.1f)) { mark_dirty(); }

    const std::shared_ptr<Point_cloud> points = get_output(0).get_points();
    if (points) {
        ImGui::Text("Points: %zu", points->positions.size());
    }
}

void Distribute_points_node::write_parameters(nlohmann::json& out) const
{
    out["count"] = m_count;
    out["seed"]  = m_seed;
}

void Distribute_points_node::read_parameters(const nlohmann::json& in)
{
    m_count = in.value("count", m_count);
    m_seed  = in.value("seed",  m_seed);
    mark_dirty();
}

// Instance_on_points_node

Instance_on_points_node::Instance_on_points_node()
    : Geometry_graph_node{"Instance on Points"}
{
    make_input_pin(Geometry_pin_key::points,      "points");
    make_input_pin(Geometry_pin_key::geometry,    "instance");
    make_input_pin(Geometry_pin_key::float_value, "scale");
    make_output_pin(Geometry_pin_key::instances, "instances");
}

void Instance_on_points_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<Point_cloud>              points   = get_input(0).get_points();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_input(1).get_geometry();
    if (!points || points->positions.empty() || !geometry) {
        set_output(0, Geometry_payload{});
        return;
    }

    const float scale = get_input(2).get_float(m_scale);
    const bool  align = m_align_to_normal && (points->normals.size() == points->positions.size());

    std::shared_ptr<Geometry_instances> instances = std::make_shared<Geometry_instances>();
    Geometry_instances::Entry& entry = instances->entries.emplace_back();
    entry.geometry = geometry;
    entry.transforms.reserve(points->positions.size());
    for (std::size_t i = 0, end = points->positions.size(); i < end; ++i) {
        glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
        glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
        glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
        if (align) {
            // Right handed orthonormal basis with +Y along the normal;
            // the rotation around the normal is arbitrary.
            axis_y = points->normals[i];
            const glm::vec3 reference = (std::abs(axis_y.y) < 0.99f) ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
            axis_x = glm::normalize(glm::cross(axis_y, reference));
            axis_z = glm::cross(axis_x, axis_y);
        }
        entry.transforms.push_back(
            glm::mat4{
                glm::vec4{axis_x * scale, 0.0f},
                glm::vec4{axis_y * scale, 0.0f},
                glm::vec4{axis_z * scale, 0.0f},
                glm::vec4{points->positions[i], 1.0f}
            }
        );
    }
    set_output(0, Geometry_payload{.value = instances});
}

void Instance_on_points_node::imgui()
{
    ImGui::TextUnformatted("Scale");
    ImGui::SetNextItemWidth(140.0f * content_scale());
    if (ImGui::DragFloat("##scale", &m_scale, 0.01f, 0.0f, 100.0f)) { mark_dirty(); }
    if (ImGui::Checkbox("Align to normal", &m_align_to_normal)) { mark_dirty(); }

    const std::shared_ptr<Geometry_instances> instances = get_output(0).get_instances();
    if (instances) {
        ImGui::Text("Instances: %zu", instances->instance_count());
    }
}

void Instance_on_points_node::write_parameters(nlohmann::json& out) const
{
    out["scale"] = m_scale;
    out["align"] = m_align_to_normal;
}

void Instance_on_points_node::read_parameters(const nlohmann::json& in)
{
    m_scale           = in.value("scale", m_scale);
    m_align_to_normal = in.value("align", m_align_to_normal);
    mark_dirty();
}

// Realize_instances_node

Realize_instances_node::Realize_instances_node()
    : Geometry_graph_node{"Realize Instances"}
{
    make_input_pin(Geometry_pin_key::instances, "instances");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Realize_instances_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    const std::shared_ptr<Geometry_instances> instances = get_input(0).get_instances();
    if (!instances || (instances->instance_count() == 0)) {
        set_output(0, Geometry_payload{});
        return;
    }

    std::shared_ptr<erhe::geometry::Geometry> realized = std::make_shared<erhe::geometry::Geometry>("realized");
    for (const Geometry_instances::Entry& entry : instances->entries) {
        if (!entry.geometry) {
            continue;
        }
        for (const glm::mat4& transform : entry.transforms) {
            realized->merge_with_transform(*entry.geometry.get(), erhe::geometry::to_geo_mat4f(transform));
        }
    }
    process_for_graph(*realized.get());
    set_output(0, Geometry_payload{.value = realized});
}

void Realize_instances_node::imgui()
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

}
