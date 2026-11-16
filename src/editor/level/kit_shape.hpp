#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GEO { class Mesh; }
namespace erhe::math { class Aabb; }

namespace editor {

class Kit_connector;
class Kit_shape_definition;

enum class Kit_parameter_type : unsigned int {
    float_value = 0,
    int_value,
    enum_value
};

enum class Kit_unit : unsigned int {
    meters = 0,
    degrees,
    count,
    ratio,
    none
};

// The scale a float default is stated in; modules and wall heights resolve against the level's lengths (R2.1).
enum class Kit_default_scale : unsigned int {
    meters = 0,
    modules,
    wall_heights
};

enum class Kit_normal_style : unsigned int {
    faceted = 0,
    smooth,
    smooth_with_hard_edges
};

// The collision shape of a shape's pieces (R2.11, D2.3); none: no physics body and no navigation input (P5).
enum class Kit_collision : unsigned int {
    convex_hull = 0,
    triangle_mesh,
    none
};

// What the mesh checks hold a shape's mesh to (R2.5): a closed solid, or a stated surface whose boundary is open.
enum class Kit_mesh_kind : unsigned int {
    solid = 0,
    surface
};

// A side of a shape's bounds; a seam side is one whose facets fill that side's rectangle (R2.4).

enum class Kit_side : unsigned int {
    x_neg = 0,
    x_pos,
    y_neg,
    y_pos,
    z_neg,
    z_pos
};

// Where the origin sits in a shape's base (R2.2).
enum class Kit_origin_rule : unsigned int {
    base_center = 0,     // the base's vertices average to the origin
    base_contains_origin // the origin lies within the base's horizontal bounds (seeded shapes)
};

// Whether a parameter can take its value from the seed (D1.8, R3.9).
enum class Kit_derive : unsigned int {
    fixed = 0,
    derivable
};

// Where a parameter's value comes from: given, or derived from the seed (a bit of the mask).
enum class Kit_value_source : unsigned int {
    explicit_value = 0,
    seed
};

// Whether a shape's generator draws random numbers from its seed (the other shapes use the seed for derived values only).
enum class Kit_randomness : unsigned int {
    none = 0,
    seeded
};

[[nodiscard]] auto c_str(Kit_parameter_type type) -> const char*;
[[nodiscard]] auto c_str(Kit_unit unit) -> const char*;
[[nodiscard]] auto c_str(Kit_default_scale scale) -> const char*;
[[nodiscard]] auto c_str(Kit_normal_style style) -> const char*;
[[nodiscard]] auto c_str(Kit_side side) -> const char*;
[[nodiscard]] auto c_str(Kit_collision collision) -> const char*;
[[nodiscard]] auto c_str(Kit_mesh_kind mesh_kind) -> const char*;
[[nodiscard]] auto c_str(Kit_derive derive) -> const char*;

// A plane through a shape's origin its mesh is symmetric about at every parameter value (D1.10): x is x = 0.
enum class Kit_mirror_plane : unsigned int {
    x = 0,
    z
};

[[nodiscard]] auto c_str(Kit_mirror_plane plane) -> const char*;

// The level lengths in effect for a scene (D5).
class Kit_lengths
{
public:
    float module     {1.0f};
    float wall_height{3.0f};
};

constexpr int kit_seed_max = 1000000;

// The seed after the given one: a hash, the same on every platform, never the seed itself.
[[nodiscard]] auto kit_next_seed(int seed) -> int;

// A float derive range in meters (or the parameter's unit), resolved against the level lengths and clamped to the valid range.
class Kit_derive_range
{
public:
    float min_value {0.0f};
    float max_value {0.0f};
    float step_value{0.0f};
};

// Minimum and maximum are in the parameter's unit (lengths in meters); float defaults and float derive ranges are in default_scale.
class Kit_parameter
{
public:
    std::string              name;
    std::string              description;
    Kit_parameter_type       type         {Kit_parameter_type::float_value};
    Kit_unit                 unit         {Kit_unit::meters};
    Kit_default_scale        default_scale{Kit_default_scale::meters};
    float                    float_default{0.0f};
    float                    float_min    {0.0f};
    float                    float_max    {0.0f};
    int                      int_default  {0};
    int                      int_min      {0};
    int                      int_max      {0};
    std::vector<std::string> enum_values;
    Kit_derive               derive           {Kit_derive::fixed};
    float                    derive_float_min {0.0f};
    float                    derive_float_max {0.0f};
    float                    derive_float_step{0.0f};
    int                      derive_int_min   {0};
    int                      derive_int_max   {0};
    Kit_value_source         default_source   {Kit_value_source::explicit_value}; // seed: in the shape's default mask

    [[nodiscard]] auto resolve_float_default(float module, float wall_height) const -> float;
    [[nodiscard]] auto resolve_derive_range (float module, float wall_height) const -> Kit_derive_range;
};

class Kit_parameter_value
{
public:
    float float_value{0.0f};
    int   int_value  {0};
};

[[nodiscard]] auto operator==(const Kit_parameter_value& lhs, const Kit_parameter_value& rhs) -> bool;

// The value a derivable parameter takes for a seed: a function of the seed, the name and the derive range alone (D1.8).
[[nodiscard]] auto derive_parameter_value(const Kit_parameter& parameter, int seed, float module, float wall_height) -> Kit_parameter_value;

// The parameter values of one piece, in its shape definition's parameter order (D1.3).
class Kit_parameter_values
{
public:
    Kit_parameter_values() = default;
    explicit Kit_parameter_values(const Kit_shape_definition& shape);

    [[nodiscard]] auto get_shape () const -> const Kit_shape_definition*;
    [[nodiscard]] auto get_values() const -> const std::vector<Kit_parameter_value>&;
    [[nodiscard]] auto get_float (std::string_view name) const -> float;
    [[nodiscard]] auto get_int   (std::string_view name) const -> int;
    [[nodiscard]] auto get_enum  (std::string_view name) const -> std::string_view;

    // Return false when the shape has no parameter of that name and type.
    auto set_float(std::string_view name, float value) -> bool;
    auto set_int  (std::string_view name, int value) -> bool;
    auto set_enum (std::string_view name, std::string_view value_name) -> bool;
    void set_value(std::size_t index, const Kit_parameter_value& value);

    // The derive mask (D1.8): one bit per parameter index.
    [[nodiscard]] auto get_value_source(std::size_t index) const -> Kit_value_source;
    void set_value_source(std::size_t index, Kit_value_source source);
    [[nodiscard]] auto get_derive_mask() const -> uint64_t;
    [[nodiscard]] auto get_seed       () const -> int;

    // Writes every masked parameter's value from the seed; derive ranges in modules or wall heights resolve against the lengths.
    void resolve_derived(float module, float wall_height);

    // Empty when valid; otherwise the R3.6 message naming the parameter and its range, or a masked parameter that is not derivable.
    [[nodiscard]] auto validate() const -> std::string;

    // JSON object in parameter order: floats as hexadecimal strings, ints as numbers, enums as names;
    // then, with a non-empty mask, "derive" with the masked names in parameter order.
    [[nodiscard]] auto to_canonical_text() const -> std::string;

    // Equal values and equal masks.
    [[nodiscard]] auto operator==(const Kit_parameter_values& other) const -> bool;

private:
    [[nodiscard]] auto find_index(std::string_view name, Kit_parameter_type type) const -> std::optional<std::size_t>;

    const Kit_shape_definition*      m_shape{nullptr};
    std::vector<Kit_parameter_value> m_values;
    uint64_t                         m_derive_mask{0};
};

// Defaults of every parameter with float defaults resolved against the level's module and wall height (R2.1),
// the shape's default mask and its values derived from the default seed.
[[nodiscard]] auto make_default_values(const Kit_shape_definition& shape, float module, float wall_height) -> Kit_parameter_values;

// Parses the text of Kit_parameter_values::to_canonical_text(); returns an error message, empty on success.
[[nodiscard]] auto parse_canonical_text(const Kit_shape_definition& shape, std::string_view text, Kit_parameter_values& out_values) -> std::string;

// Hexadecimal float in glibc's %a form on every platform (sign, "0x", shortest exact digits).
[[nodiscard]] auto format_hexfloat(float value) -> std::string;
[[nodiscard]] auto parse_hexfloat (std::string_view text, float& out_value) -> bool;

// The default agent of doc/editor/navigation.md "Settings", for walkability by construction (R2.10).
class Kit_agent
{
public:
    float height           {1.8f};
    float radius           {0.3f};
    float max_climb        {0.4f};
    float max_slope_degrees{45.0f};
};

// Margin an opening keeps beyond agent height and twice the agent radius (R2.10).
constexpr float kit_opening_margin = 0.2f;

using Kit_generator           = std::function<void(const Kit_parameter_values& values, GEO::Mesh& mesh)>;
using Kit_values_check        = std::function<std::string(const Kit_parameter_values& values)>;
using Kit_walkability_check   = std::function<std::string(const Kit_parameter_values& values, const Kit_agent& agent)>;
using Kit_seam_sides_function = std::function<std::vector<Kit_side>(const Kit_parameter_values& values)>;
using Kit_connectors_function = std::function<void(const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors)>;

class Kit_shape_definition
{
public:
    std::string                id;           // <kit>.<shape>
    std::string                kit;
    std::string                display_name;
    std::string                description;
    std::vector<std::string>   tags;
    std::vector<Kit_parameter> parameters;
    std::vector<std::string>   material_slots;         // named, unique, in order; facets name theirs by index (D1.11)
    Kit_normal_style           normal_style           {Kit_normal_style::faceted};
    float                      hard_edge_angle_degrees{0.0f};
    unsigned int               triangle_budget        {0}; // at default parameters (R2.6)
    Kit_collision              collision              {Kit_collision::convex_hull};
    Kit_mesh_kind              mesh_kind              {Kit_mesh_kind::solid};
    Kit_randomness             randomness             {Kit_randomness::none};
    Kit_origin_rule            origin_rule            {Kit_origin_rule::base_center}; // an unseeded shape's; a seeded one's is always base_contains_origin
    std::vector<Kit_mirror_plane> mirror_planes;       // empty: no exact mirror image (D1.10)
    Kit_connectors_function    get_connectors;         // optional: the connectors at the values, the same names at every value (D1.12)
    Kit_generator              generator;
    Kit_values_check           check_combination;      // optional: limits between parameters
    Kit_walkability_check      check_walkability;      // set for shapes meant to be walked on or through
    Kit_seam_sides_function    get_seam_sides;         // optional: sides that fill their bounds rectangle

    [[nodiscard]] auto find_parameter(std::string_view name) const -> std::optional<std::size_t>;
    [[nodiscard]] auto has_tag       (std::string_view tag) const -> bool;
};

// The bounds of the values' mesh from the generator's CPU geometry alone (no renderable mesh, no upload): the min and
// max of the facet corners' single precision points, the box a brush of the values reports. Invalid without facets.
[[nodiscard]] auto get_kit_geometry_bounds(const Kit_parameter_values& values) -> erhe::math::Aabb;

} // namespace editor
