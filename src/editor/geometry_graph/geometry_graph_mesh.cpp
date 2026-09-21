#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"

#include "erhe_property/attached_group.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_system.hpp"

namespace editor {

namespace {

using erhe::property::Dependency_property;
using erhe::property::Object_reference;
using erhe::property::Property;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr std::string_view c_group = "Geometry Graph Mesh";

// The graph reference is null or names a Graph_mesh.
[[nodiscard]] auto validate_graph_mesh(const Property_value& value) -> bool
{
    const Object_reference& reference = std::get<Object_reference>(value);
    return !reference.object || (dynamic_cast<const Graph_mesh*>(reference.object.get()) != nullptr);
}

} // anonymous namespace

auto Geometry_graph_mesh::property_owner_type() -> erhe::property::Owner_type
{
    // Geometry_graph_mesh is not a Dependency_object, so there is no Item<> to
    // allocate the id: the registering class's own id sits directly under the
    // root and serves only to qualify the name
    // (Geometry_graph_mesh.graph_mesh).
    static const erhe::property::Owner_type s_id = erhe::property::allocate_owner_type(
        erhe::property::root_owner_type, "Geometry_graph_mesh"
    );
    return s_id;
}

// The group's only value, and its key (D1). It carries no serialize flag: the
// binding's file carriers are the native ones (D8), so writing it here too
// would give one binding two authorities that drift apart.
const Property<Object_reference> Geometry_graph_mesh::graph_mesh_property = Property<Object_reference>::register_attached(
    "graph_mesh", Geometry_graph_mesh::property_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = Object_reference{},
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = false,
        .flags            = Property_flags::none,
        .ui               = Property_ui{
            .group                = c_group,
            .tooltip              = "The geometry graph asset whose bake this node shows",
            .label                = "Graph Mesh",
            .reference_item_types = erhe::Item_type::graph_mesh
        }
    },
    validate_graph_mesh
);

auto Geometry_graph_mesh::all_properties() -> const std::vector<const Dependency_property*>&
{
    static const std::vector<const Dependency_property*> s_properties{
        graph_mesh_property.get_ptr()
    };
    return s_properties;
}

auto carries_geometry_graph_mesh(const erhe::scene::Node& node) -> bool
{
    return erhe::property::carries_attached_group(node, Geometry_graph_mesh::graph_mesh_property.get());
}

auto read_geometry_graph_mesh(const erhe::scene::Node& node) -> std::optional<Geometry_graph_mesh_data>
{
    if (!carries_geometry_graph_mesh(node)) {
        return {};
    }
    Geometry_graph_mesh_data data{};
    data.graph_mesh = std::dynamic_pointer_cast<Graph_mesh>(node.get_value(Geometry_graph_mesh::graph_mesh_property).object);
    return data;
}

void set_geometry_graph_mesh(erhe::scene::Node& node, const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    if (graph_mesh) {
        node.set_value(Geometry_graph_mesh::graph_mesh_property, Object_reference{graph_mesh});
    } else {
        // Back to the default layer, so the node holds no reference at all and
        // Add Property offers the key again.
        node.clear_value(Geometry_graph_mesh::graph_mesh_property);
    }
}

} // namespace editor
