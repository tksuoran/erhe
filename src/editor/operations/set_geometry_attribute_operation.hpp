#pragma once

#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <geogram/basic/numeric.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;

// Pseudo attribute name addressing the vertex positions (GEO::Mesh points).
inline constexpr std::string_view c_position_attribute{"position"};

// One element's value of one attribute: up to four components (an integer
// attribute's components are exact in double), and whether the element
// carries a value at all (Attribute_present's present flag).
class Geometry_attribute_value
{
public:
    std::array<double, 4> components{};
    bool                  present   {false};
};

// Attributes the user may edit: positions and every Mesh_attributes attribute
// except those the geometry pipeline derives from other data (facet_id,
// facet_centroid, vertex_normal_smooth, vertex_valency_edge_count).
[[nodiscard]] auto is_editable_geometry_attribute(std::string_view attribute) -> bool;

// The value of `attribute` (a Mesh_attributes member name, or
// c_position_attribute) at `element`; nullopt for an unknown attribute or an
// element out of range.
[[nodiscard]] auto read_geometry_attribute(
    const erhe::geometry::Geometry& geometry,
    std::string_view                attribute,
    GEO::index_t                    element
) -> std::optional<Geometry_attribute_value>;

// The single entry point for numeric attribute edits (Geometry Spreadsheet,
// MCP set_mesh_attribute_values): validates the edit, captures the before
// values from the primitive's current Geometry and returns the undoable
// operation - Move_mesh_vertices_operation for positions (it refreshes the
// baked normals and the collision shape), Set_geometry_attribute_operation
// otherwise. `after` is parallel to `elements`, or one value for all of them.
// Returns null with out_error set when the edit is invalid.
[[nodiscard]] auto make_geometry_attribute_operation(
    App_context&                                 context,
    const std::shared_ptr<erhe::scene::Mesh>&    mesh,
    std::size_t                                  primitive_index,
    std::string_view                             attribute,
    const std::vector<GEO::index_t>&             elements,
    const std::vector<Geometry_attribute_value>& after,
    std::string&                                 out_error
) -> std::shared_ptr<Operation>;

// Undoable edit of one Mesh_attributes attribute on a set of elements of one
// Geometry. Like Paint_colors_operation it writes into the same Geometry
// object (Mesh_component_selection entries keyed on it survive), rebuilds one
// Primitive and shares it across every mesh that references the Geometry, and
// announces Mesh_geometry_changed_message. edge_sharpness feeds no render
// stream (doc/erhe/subdivision_crease_edges.md), so its edit skips the
// rebuild and only announces the change.
class Set_geometry_attribute_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>        mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        std::string                               attribute;     // Mesh_attributes member name
        std::vector<GEO::index_t>                 elements;
        std::vector<Geometry_attribute_value>     before_values; // parallel to elements
        std::vector<Geometry_attribute_value>     after_values;  // parallel to elements
        erhe::primitive::Build_info               build_info;
        erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
    };

    explicit Set_geometry_attribute_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const std::vector<Geometry_attribute_value>& values);

    Parameters m_parameters;
};

} // namespace editor
