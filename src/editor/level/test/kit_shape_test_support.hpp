#pragma once

#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shape_library.hpp"

#include <geogram/mesh/mesh.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Helpers and per-batch expectation rows shared by the kit shape test sources (doc/editor/level_tools.md "Verification").
namespace editor::kit_test {

constexpr float default_module      = 1.0f;
constexpr float default_wall_height = 3.0f;

[[nodiscard]] auto get_library() -> const Kit_shape_library&;
[[nodiscard]] auto get_shape  (const char* id) -> const Kit_shape_definition&;

// The id the tests key a shape's expectations by: a recreated shape shares its original's.
[[nodiscard]] auto contract_id(const Kit_shape_definition& shape) -> const std::string&;

[[nodiscard]] auto defaults(const Kit_shape_definition& shape) -> Kit_parameter_values;
[[nodiscard]] auto generate(const Kit_parameter_values& values) -> std::unique_ptr<GEO::Mesh>;
[[nodiscard]] auto describe(const Kit_mesh_report& report) -> std::string;

enum class Range_end : unsigned int { min = 0, max };

[[nodiscard]] auto c_str(Range_end end) -> const char*;

// One end of one parameter's range, with every other parameter at its default.
[[nodiscard]] auto make_range_end(const Kit_shape_definition& shape, std::size_t index, Range_end range_end) -> Kit_parameter_values;

// Every derivable parameter of the shape in the mask, derived from the seed at the default lengths.
[[nodiscard]] auto all_derived(const Kit_shape_definition& shape, int seed) -> Kit_parameter_values;

// Values of the shape with an empty mask (every value explicit) and the given seed.
[[nodiscard]] auto explicit_values(const Kit_shape_definition& shape, int seed) -> Kit_parameter_values;

// Seed sweeps. A shape whose generator draws nothing from the seed (Kit_randomness::none, which
// seeds_select_the_mesh proves for every shape) gives one mesh for every seed of explicit values,
// so its sweep is one seed; a seeded shape's is c_seed_sweep_count seeds.
constexpr int c_seed_sweep_count = 2;
[[nodiscard]] auto sweep_seeds(const Kit_shape_definition& shape) -> std::vector<int>;

// Sweeps of derived values (all_derived): the seed picks the derived values of every shape.
constexpr int c_derive_sweep_count = 8;
[[nodiscard]] auto derive_seeds() -> std::vector<int>;

// The positions of the mesh's vertices on the plane x = plane_x, sorted.
[[nodiscard]] auto vertices_on_x(const GEO::Mesh& mesh, float plane_x) -> std::vector<std::array<float, 3>>;

// Every parameter of the shape at the minimum or the maximum of its range, an empty mask.
[[nodiscard]] auto range_corner(const Kit_shape_definition& shape, Range_end range_end) -> Kit_parameter_values;

// The recreated shape's mesh checks pass and its bounds are its original's at the two value sets.
void expect_original_bounds(const Kit_parameter_values& values, const Kit_parameter_values& original_values);

// The recreated shapes of the listed originals keep their originals' bounds at the defaults, the range corners and derived values.
void expect_recreated_keep_original_bounds(const std::vector<std::string>& originals);

// What generated_meshes_are_pinned keeps of a mesh: counts and slots exactly, positions within a tolerance.
class Mesh_pin
{
public:
    std::string          name;
    unsigned int         vertices{0};
    unsigned int         facets  {0};
    unsigned int         corners {0};
    uint64_t             slots   {0}; // FNV-1a 64 of the facet count of each slot, in slot order
    std::array<float, 3> min     {};
    std::array<float, 3> max     {};
    std::array<float, 3> centroid{}; // mean of the vertex positions
    float                area    {0.0f};
};

[[nodiscard]] auto make_mesh_pin(const std::string& name, const GEO::Mesh& mesh) -> Mesh_pin;

// A shape's component count at its defaults; a shape without a row is one component.
enum class Kit_component_rule : unsigned int { exactly = 0, more_than };

class Kit_component_row
{
public:
    std::string        id;
    Kit_component_rule rule {Kit_component_rule::exactly};
    unsigned int       count{1};
};

// One batch's expectations; the tests take the union of every batch's rows.
class Kit_shape_rows
{
public:
    std::vector<std::string>                          shapes;              // ids of the shapes the batch adds, but recreated ones
    std::vector<std::string>                          recreated_originals; // originals whose recreated shape the batch adds
    std::vector<std::string>                          seeded_shapes;       // ids of the batch's shapes of Kit_randomness::seeded
    std::vector<std::string>                          enum_shapes;         // ids whose every enum value is checked
    std::vector<Kit_component_row>                    components;          // by shape id, else by contract id
    std::vector<std::string>                          walkability_open;    // contract ids left out of derived walkability at other lengths
    std::vector<std::pair<std::string, unsigned int>> triangle_counts;     // at the defaults
    std::vector<std::pair<std::string, std::string>>  worst_cases;         // shape id, canonical text
    std::vector<Mesh_pin>                             pins;                // "<id> <seed>" at the defaults
};

// One function per batch, each in its own kit_shape_rows_<batch>.cpp.
[[nodiscard]] auto get_originals_rows        () -> Kit_shape_rows;
[[nodiscard]] auto get_basic_v2_rows         () -> Kit_shape_rows;
[[nodiscard]] auto get_architecture_v2_rows  () -> Kit_shape_rows;
[[nodiscard]] auto get_medieval_v2_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_nature_v2_rows        () -> Kit_shape_rows;
[[nodiscard]] auto get_nature_plants_v2_rows () -> Kit_shape_rows;
[[nodiscard]] auto get_nature_rocks_v2_rows  () -> Kit_shape_rows;
[[nodiscard]] auto get_build_13_16_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_build_17_20_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_build_21_24_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_extra_01_08_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_extra_09_16_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_extra_17_24_rows      () -> Kit_shape_rows;
[[nodiscard]] auto get_extra_25_32_rows      () -> Kit_shape_rows;

[[nodiscard]] auto get_all_rows() -> const std::vector<Kit_shape_rows>&;

// The concatenation of one table over every batch.
template <typename T>
[[nodiscard]] auto get_union(std::vector<T> Kit_shape_rows::* const table) -> std::vector<T>
{
    std::vector<T> all;
    for (const Kit_shape_rows& rows : get_all_rows()) {
        all.insert(all.end(), (rows.*table).begin(), (rows.*table).end());
    }
    return all;
}

} // namespace editor::kit_test
