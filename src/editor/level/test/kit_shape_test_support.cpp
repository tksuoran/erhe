#include "kit_shape_test_support.hpp"

#include "level/kit_mesh_builder.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace editor::kit_test {

auto get_library() -> const Kit_shape_library&
{
    static const Kit_shape_library library;
    return library;
}

auto get_shape(const char* id) -> const Kit_shape_definition&
{
    const Kit_shape_definition* shape = get_library().find(id);
    EXPECT_NE(shape, nullptr) << id;
    return *shape;
}

auto contract_id(const Kit_shape_definition& shape) -> const std::string&
{
    return get_contract_id(shape);
}

auto defaults(const Kit_shape_definition& shape) -> Kit_parameter_values
{
    return make_default_values(shape, default_module, default_wall_height);
}

auto generate(const Kit_parameter_values& values) -> std::unique_ptr<GEO::Mesh>
{
    std::unique_ptr<GEO::Mesh> mesh = std::make_unique<GEO::Mesh>(3, true);
    values.get_shape()->generator(values, *mesh);
    return mesh;
}

auto describe(const Kit_mesh_report& report) -> std::string
{
    return fmt::format(
        "{} vertices, {} facets, {} triangles, {} components; problems: {}",
        report.vertex_count, report.facet_count, report.triangle_count, report.component_count,
        report.problems.empty() ? std::string{"none"} : report.problems.front()
    );
}

auto c_str(const Range_end end) -> const char*
{
    return (end == Range_end::max) ? "max" : "min";
}

auto make_range_end(const Kit_shape_definition& shape, const std::size_t index, const Range_end range_end) -> Kit_parameter_values
{
    const bool at_max = (range_end == Range_end::max);
    Kit_parameter_values values    = defaults(shape);
    const Kit_parameter& parameter = shape.parameters[index];
    Kit_parameter_value  value     = values.get_values()[index];
    if (parameter.type == Kit_parameter_type::float_value) {
        value.float_value = at_max ? parameter.float_max : parameter.float_min;
    } else {
        value.int_value = at_max ? parameter.int_max : parameter.int_min;
    }
    values.set_value(index, value);
    return values;
}

auto all_derived(const Kit_shape_definition& shape, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    EXPECT_TRUE(values.set_int("seed", seed));
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        if (shape.parameters[i].derive == Kit_derive::derivable) {
            values.set_value_source(i, Kit_value_source::seed);
        }
    }
    values.resolve_derived(default_module, default_wall_height);
    return values;
}

auto explicit_values(const Kit_shape_definition& shape, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        values.set_value_source(i, Kit_value_source::explicit_value);
    }
    EXPECT_TRUE(values.set_int("seed", seed));
    return values;
}

auto vertices_on_x(const GEO::Mesh& mesh, const float plane_x) -> std::vector<std::array<float, 3>>
{
    std::vector<std::array<float, 3>> points;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        if (p.x == plane_x) {
            points.push_back({p.x, p.y, p.z});
        }
    }
    std::sort(points.begin(), points.end());
    return points;
}

auto range_corner(const Kit_shape_definition& shape, const Range_end range_end) -> Kit_parameter_values
{
    Kit_parameter_values values = defaults(shape);
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        values.set_value_source(i, Kit_value_source::explicit_value);
        const Kit_parameter& parameter = shape.parameters[i];
        if (parameter.name == "seed") {
            continue;
        }
        Kit_parameter_value value = values.get_values()[i];
        if (parameter.type == Kit_parameter_type::float_value) {
            value.float_value = (range_end == Range_end::max) ? parameter.float_max : parameter.float_min;
        } else {
            value.int_value = (range_end == Range_end::max) ? parameter.int_max : parameter.int_min;
        }
        values.set_value(i, value);
    }
    return values;
}

void expect_original_bounds(const Kit_parameter_values& values, const Kit_parameter_values& original_values)
{
    SCOPED_TRACE(fmt::format("{} {}", values.get_shape()->id, values.to_canonical_text()));
    const Kit_mesh_report report          = check_kit_shape(values);
    const Kit_mesh_report original_report = check_kit_shape(original_values);
    EXPECT_TRUE(report.is_valid()) << describe(report);
    const float tolerance = 1.0e-5f * std::max(1.0f, glm::length(original_report.bounds_max - original_report.bounds_min));
    EXPECT_LT(glm::distance(report.bounds_min, original_report.bounds_min), tolerance);
    EXPECT_LT(glm::distance(report.bounds_max, original_report.bounds_max), tolerance);
}

void expect_recreated_keep_original_bounds(const std::vector<std::string>& originals)
{
    EXPECT_FALSE(originals.empty());
    for (const std::string& original_id : originals) {
        const Kit_shape_definition* const shape = get_library().find(original_id + c_kit_recreated_id_suffix);
        ASSERT_NE(shape, nullptr) << original_id;
        const Kit_shape_definition& original = get_shape(original_id.c_str());
        expect_original_bounds(defaults(*shape), defaults(original));
        expect_original_bounds(range_corner(*shape, Range_end::min), range_corner(original, Range_end::min));
        expect_original_bounds(range_corner(*shape, Range_end::max), range_corner(original, Range_end::max));
        for (const int seed : {1, 3, 17, 4242}) {
            expect_original_bounds(all_derived(*shape, seed), all_derived(original, seed));
        }
    }
}

auto make_mesh_pin(const std::string& name, const GEO::Mesh& mesh) -> Mesh_pin
{
    Mesh_pin pin;
    pin.name     = name;
    pin.vertices = mesh.vertices.nb();
    pin.facets   = mesh.facets.nb();
    glm::dvec3 sum{0.0};
    glm::vec3  low { std::numeric_limits<float>::max()};
    glm::vec3  high{-std::numeric_limits<float>::max()};
    const auto point = [&mesh](const GEO::index_t v) -> glm::vec3 {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, v);
        return glm::vec3{p.x, p.y, p.z};
    };
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point(v);
        sum  += glm::dvec3{p};
        low   = glm::min(low, p);
        high  = glm::max(high, p);
    }
    const glm::dvec3 mean = (pin.vertices > 0) ? (sum / static_cast<double>(pin.vertices)) : glm::dvec3{0.0};
    std::vector<uint32_t> facet_slots;
    read_kit_facet_slots(mesh, facet_slots);
    std::vector<uint32_t> slot_counts;
    double area = 0.0;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(f);
        pin.corners += corner_count;
        const glm::dvec3 origin{point(mesh.facets.vertex(f, 0))};
        for (GEO::index_t c = 1; (c + 1) < corner_count; ++c) {
            const glm::dvec3 a = glm::dvec3{point(mesh.facets.vertex(f, c))} - origin;
            const glm::dvec3 b = glm::dvec3{point(mesh.facets.vertex(f, c + 1))} - origin;
            area += 0.5 * glm::length(glm::cross(a, b));
        }
        if (facet_slots[f] >= slot_counts.size()) {
            slot_counts.resize(facet_slots[f] + 1, 0);
        }
        ++slot_counts[facet_slots[f]];
    }
    pin.slots = 0xcbf29ce484222325ull;
    for (const uint32_t count : slot_counts) {
        for (int i = 0; i < 4; ++i) {
            pin.slots ^= static_cast<uint64_t>((count >> (8 * i)) & 0xffu);
            pin.slots *= 0x100000001b3ull;
        }
    }
    for (int i = 0; i < 3; ++i) {
        pin.min[i]      = low[i];
        pin.max[i]      = high[i];
        pin.centroid[i] = static_cast<float>(mean[i]);
    }
    pin.area = static_cast<float>(area);
    return pin;
}

auto get_all_rows() -> const std::vector<Kit_shape_rows>&
{
    static const std::vector<Kit_shape_rows> all{
        get_originals_rows(),
        get_basic_v2_rows(),
        get_architecture_v2_rows(),
        get_medieval_v2_rows(),
        get_nature_v2_rows(),
        get_nature_plants_v2_rows(),
        get_nature_rocks_v2_rows(),
        get_build_13_16_rows(),
        get_build_17_20_rows(),
        get_build_21_24_rows()
    };
    return all;
}

} // namespace editor::kit_test
