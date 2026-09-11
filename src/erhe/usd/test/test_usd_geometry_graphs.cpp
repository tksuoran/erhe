// Geometry node graphs as marked `NodeGraph` prims
// (doc/usd-texture-graphs-plan.md section 4). A geometry graph is the prim
// form a texture graph takes, with its nodes' `info:id` under the
// `erhe:geometry:` prefix its `erhe:graph:format` token names and its
// evaluated geometry as the child `def Mesh "result"` a brush's geometry is
// written as, so a viewer without erhe sees what the graph makes. erhe::usd
// creates no graph asset - that is the editor's half of the step - so the
// export side stands a plain `erhe::Scope` in for the asset item and hands
// the writer the record.

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_usd/usd.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

[[nodiscard]] auto test_data_path(const char* file_name) -> std::filesystem::path
{
    return std::filesystem::path{ERHE_USD_TEST_DATA_DIR} / file_name;
}

[[nodiscard]] auto temporary_path(const char* file_name) -> std::filesystem::path
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_geometry_graph_tests";
    std::error_code             error_code{};
    std::filesystem::create_directories(directory, error_code);
    return directory / file_name;
}

[[nodiscard]] auto read_lines(const std::filesystem::path& path) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::ifstream            stream{path};
    std::string              line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] auto has_line_with(const std::vector<std::string>& lines, const std::string& needle) -> bool
{
    for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto find_graph(
    const erhe::usd::Usd_data& data,
    const std::string&         stage_path
) -> const erhe::usd::Usd_node_graph*
{
    for (const erhe::usd::Usd_node_graph& graph : data.node_graphs) {
        if (graph.stage_path == stage_path) {
            return &graph;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_node(
    const erhe::usd::Usd_node_graph& graph,
    const std::string&               name
) -> const erhe::usd::Usd_node_graph_node*
{
    for (const erhe::usd::Usd_node_graph_node& node : graph.nodes) {
        if (node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_parameter(
    const erhe::usd::Usd_node_graph_node& node,
    const std::string&                    name
) -> const erhe::usd::Usd_node_graph_parameter*
{
    for (const erhe::usd::Usd_node_graph_parameter& parameter : node.parameters) {
        if (parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

[[nodiscard]] auto find_pin(
    const std::vector<erhe::usd::Usd_node_graph_pin>& pins,
    const std::string&                                name
) -> const erhe::usd::Usd_node_graph_pin*
{
    for (const erhe::usd::Usd_node_graph_pin& pin : pins) {
        if (pin.name == name) {
            return &pin;
        }
    }
    return nullptr;
}

// What one save of the graphs a load recorded needs: the stand-in tree the
// graph prims sit in, kept alive for as long as the arguments are used.
class Save_scene final
{
public:
    std::shared_ptr<erhe::scene::Node>        root;
    std::vector<std::shared_ptr<erhe::Scope>> graph_items;
    erhe::usd::Usd_save_arguments             arguments;
};

void build_save_scene(
    const erhe::usd::Usd_data&   data,
    const std::filesystem::path& path,
    Save_scene&                  out_scene
)
{
    out_scene.root = std::make_shared<erhe::scene::Xform>("save_root");

    std::shared_ptr<erhe::scene::Xform> world = std::make_shared<erhe::scene::Xform>("World");
    world->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    world->set_parent(out_scene.root);

    std::shared_ptr<erhe::Scope> graph_scope = std::make_shared<erhe::Scope>("Graph_Meshes");
    graph_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
    graph_scope->set_parent(world);

    out_scene.arguments.path      = path;
    out_scene.arguments.root_node = out_scene.root;

    for (const erhe::usd::Usd_node_graph& graph : data.node_graphs) {
        std::shared_ptr<erhe::Scope> item = std::make_shared<erhe::Scope>(graph.name);
        item->enable_flag_bits(erhe::Item_flags::show_in_ui);
        item->set_parent(graph_scope);
        out_scene.graph_items.push_back(item);
        out_scene.arguments.node_graphs.push_back(
            erhe::usd::Usd_save_node_graph{
                .item     = item,
                .format   = graph.format,
                .outputs  = graph.outputs,
                .nodes    = graph.nodes,
                .geometry = graph.geometry
            }
        );
    }
}

class Geometry_graphs_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("geometry_graph.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Geometry_graphs_import, a_geometry_graph_records_its_format_and_its_nodes)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->name,   "Terrain");
    EXPECT_EQ(graph->format, erhe::usd::c_geometry_graph_format);
    ASSERT_EQ(graph->nodes.size(), 2u);
    EXPECT_EQ(graph->nodes[0].name, "Box");
    EXPECT_EQ(graph->nodes[1].name, "Transform");
}

// The `erhe:geometry:` prefix of a node of a geometry graph is stripped, so
// what the record carries is the factory type name.
TEST_F(Geometry_graphs_import, a_node_id_is_read_under_the_geometry_prefix)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(graph->nodes.size(), 2u);
    EXPECT_EQ(graph->nodes[0].type_name, "box");
    EXPECT_EQ(graph->nodes[1].type_name, "transform");
    EXPECT_EQ(erhe::usd::node_graph_node_id_prefix(erhe::usd::c_geometry_graph_format), "erhe:geometry:");
    EXPECT_EQ(erhe::usd::node_graph_node_id_prefix(erhe::usd::c_texture_graph_format),  "erhe:texture:");
    EXPECT_TRUE(erhe::usd::node_graph_node_id_prefix("erhe_shader_graph").empty());
}

// A node whose prefix is not the one its graph's format names is no node of
// that graph, and the links into it go with it.
TEST_F(Geometry_graphs_import, a_node_of_another_graph_kind_is_dropped_with_its_links)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(find_node(*graph, "Alien"), nullptr);
    const erhe::usd::Usd_node_graph_node* box = find_node(*graph, "Box");
    ASSERT_NE(box, nullptr);
    const erhe::usd::Usd_node_graph_pin* seed = find_pin(box->inputs, "seed");
    ASSERT_NE(seed, nullptr);
    EXPECT_TRUE(seed->source_node.empty());
    EXPECT_TRUE(seed->source_pin.empty());
}

// The pin value types the mapping gives a geometry graph
// (doc/usd_compatibility.md, "Geometry node graphs"): an opaque payload pin
// is a `token` and a value pin is the USD type of its value, each carried
// through the record as the attribute type it was authored with.
TEST_F(Geometry_graphs_import, the_pin_and_parameter_types_are_read_as_authored)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    const erhe::usd::Usd_node_graph_node* transform = find_node(*graph, "Transform");
    ASSERT_NE(transform, nullptr);

    const erhe::usd::Usd_node_graph_pin* geometry_in = find_pin(transform->inputs, "geometry");
    ASSERT_NE(geometry_in, nullptr);
    EXPECT_EQ(geometry_in->value_type,  "token");
    EXPECT_EQ(geometry_in->source_node, "Box");
    EXPECT_EQ(geometry_in->source_pin,  "geometry");

    const erhe::usd::Usd_node_graph_pin* geometry_out = find_pin(transform->outputs, "geometry");
    ASSERT_NE(geometry_out, nullptr);
    EXPECT_EQ(geometry_out->value_type, "token");

    const erhe::usd::Usd_node_graph_pin* translation = find_pin(transform->inputs, "translation");
    ASSERT_NE(translation, nullptr);
    EXPECT_EQ(translation->value_type, "float3");
    const erhe::usd::Usd_node_graph_pin* tint = find_pin(transform->inputs, "tint");
    ASSERT_NE(tint, nullptr);
    EXPECT_EQ(tint->value_type, "float4");
    const erhe::usd::Usd_node_graph_pin* matrix = find_pin(transform->inputs, "matrix");
    ASSERT_NE(matrix, nullptr);
    EXPECT_EQ(matrix->value_type, "matrix4d");

    // An `inputs:` attribute carrying a value is a parameter, whatever kind
    // of graph it is in.
    const erhe::usd::Usd_node_graph_node* box = find_node(*graph, "Box");
    ASSERT_NE(box, nullptr);
    const erhe::usd::Usd_node_graph_parameter* size = find_parameter(*box, "size");
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->usd_type, "float");
    EXPECT_EQ(size->value,    "2.5");

    ASSERT_EQ(graph->outputs.size(), 1u);
    EXPECT_EQ(graph->outputs[0].name,        "geometry");
    EXPECT_EQ(graph->outputs[0].value_type,  "token");
    EXPECT_EQ(graph->outputs[0].source_node, "Transform");
}

// A vector parameter of a geometry node is a quantity, so it takes the
// `float3` / `float4` spelling a texture graph's color parameter does not
// (doc/usd_compatibility.md, "Geometry node graphs").
TEST_F(Geometry_graphs_import, a_vector_parameter_is_read_as_float3_or_float4)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    const erhe::usd::Usd_node_graph_node* transform = find_node(*graph, "Transform");
    ASSERT_NE(transform, nullptr);

    const erhe::usd::Usd_node_graph_parameter* pivot = find_parameter(*transform, "pivot");
    ASSERT_NE(pivot, nullptr);
    EXPECT_EQ(pivot->usd_type, "float3");
    EXPECT_EQ(pivot->value,    "(1, 2, 3)");

    const erhe::usd::Usd_node_graph_parameter* weights = find_parameter(*transform, "weights");
    ASSERT_NE(weights, nullptr);
    EXPECT_EQ(weights->usd_type, "float4");
    EXPECT_EQ(weights->value,    "(0.25, 0.5, 0.75, 1)");
}

TEST_F(Geometry_graphs_import, the_result_child_is_the_graphs_evaluated_geometry)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    ASSERT_TRUE(graph->geometry);
    EXPECT_EQ(graph->geometry->get_mesh().facets.nb(),   2u);
    EXPECT_EQ(graph->geometry->get_mesh().vertices.nb(), 8u);
}

TEST_F(Geometry_graphs_import, the_result_child_is_no_mesh_of_the_scene)
{
    for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
        ASSERT_TRUE(node);
        EXPECT_NE(node->get_name(), "result");
        EXPECT_NE(node->get_name(), "Terrain");
    }
    for (const std::shared_ptr<erhe::Typed>& prim : loaded.data.prims) {
        ASSERT_TRUE(prim);
        EXPECT_NE(prim->get_name(), "result");
        EXPECT_NE(prim->get_name(), "Terrain");
    }
}

// `load_stage` strips the wiring of every marked `NodeGraph` from the stage
// Tydra converts, and the marker is the attribute rather than any one format
// token, so a geometry graph is stripped the way a texture graph is: the
// stage composes, the meshes convert - the `result` child among them - and
// the graph is still read whole off the kept layer.
TEST_F(Geometry_graphs_import, the_stage_composes_with_the_graph_wiring_stripped)
{
    EXPECT_TRUE(loaded.warning.empty()) << loaded.warning;
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    ASSERT_TRUE(graph->geometry);
    const erhe::usd::Usd_node_graph_node* transform = find_node(*graph, "Transform");
    ASSERT_NE(transform, nullptr);
    const erhe::usd::Usd_node_graph_pin* geometry_in = find_pin(transform->inputs, "geometry");
    ASSERT_NE(geometry_in, nullptr);
    EXPECT_EQ(geometry_in->source_node, "Box");
}

class Geometry_graphs_export : public testing::Test
{
protected:
    void SetUp() override
    {
        std::shared_ptr<erhe::scene::Node>  load_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("geometry_graph.usda"),
            .root_node     = load_root,
            .mesh_layer_id = 0
        };
        const erhe::usd::Usd_load_result loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;

        written_path = temporary_path("geometry_graph.usda");
        build_save_scene(loaded.data, written_path, scene);
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(scene.arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        lines = read_lines(written_path);
    }

    std::filesystem::path    written_path;
    Save_scene               scene;
    std::vector<std::string> lines;
};

TEST_F(Geometry_graphs_export, a_geometry_graph_is_written_with_its_nodes_and_its_result_child)
{
    EXPECT_TRUE(has_line_with(lines, "def NodeGraph \"Terrain\""));
    EXPECT_TRUE(has_line_with(lines, "custom token erhe:graph:format = \"erhe_geometry_graph\""));
    EXPECT_TRUE(has_line_with(lines, "uniform token info:id = \"erhe:geometry:box\""));
    EXPECT_TRUE(has_line_with(lines, "uniform token info:id = \"erhe:geometry:transform\""));
    EXPECT_FALSE(has_line_with(lines, "erhe:texture:"));
    EXPECT_TRUE(has_line_with(lines, "def Mesh \"result\""));
    EXPECT_TRUE(has_line_with(lines, "uniform token subdivisionScheme = \"none\""));
}

TEST_F(Geometry_graphs_export, the_pin_types_are_written_as_they_were_read)
{
    EXPECT_TRUE(
        has_line_with(lines, "token inputs:geometry.connect = </World/Graph_Meshes/Terrain/Box.outputs:geometry>")
    );
    EXPECT_TRUE(
        has_line_with(lines, "token outputs:geometry.connect = </World/Graph_Meshes/Terrain/Transform.outputs:geometry>")
    );
    EXPECT_TRUE(has_line_with(lines, "float3 inputs:translation"));
    EXPECT_TRUE(has_line_with(lines, "float4 inputs:tint"));
    EXPECT_TRUE(has_line_with(lines, "matrix4d inputs:matrix"));
    EXPECT_TRUE(has_line_with(lines, "float inputs:size = 2.5"));
    EXPECT_TRUE(has_line_with(lines, "float3 inputs:pivot = (1, 2, 3)"));
    EXPECT_TRUE(has_line_with(lines, "float4 inputs:weights = (0.25, 0.5, 0.75, 1)"));
}

// The written prims read back as the records they were - the result geometry
// included - and writing those again spells the same file (R4).
TEST_F(Geometry_graphs_export, a_second_save_of_the_reloaded_graph_is_byte_identical)
{
    std::shared_ptr<erhe::scene::Node>  reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = written_path,
        .root_node     = reload_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const erhe::usd::Usd_node_graph* graph = find_graph(reloaded.data, "/World/Graph_Meshes/Terrain");
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->format, erhe::usd::c_geometry_graph_format);
    ASSERT_EQ(graph->nodes.size(), 2u);
    ASSERT_TRUE(graph->geometry);
    EXPECT_EQ(graph->geometry->get_mesh().facets.nb(), 2u);

    const std::filesystem::path second_path = temporary_path("geometry_graph_2.usda");
    Save_scene                  second_scene;
    build_save_scene(reloaded.data, second_path, second_scene);
    const erhe::usd::Usd_save_result second = erhe::usd::save_usda(second_scene.arguments);
    ASSERT_TRUE(second.error.empty()) << second.error;
    EXPECT_EQ(read_lines(second_path), lines);
}

// A geometry graph that evaluated nothing writes no `result` child, and a
// texture graph never has one.
TEST_F(Geometry_graphs_export, a_graph_without_evaluated_geometry_writes_no_result_child)
{
    const std::filesystem::path path = temporary_path("geometry_graph_no_result.usda");
    Save_scene                  bare_scene;
    erhe::usd::Usd_data         data;
    data.node_graphs.push_back(
        erhe::usd::Usd_node_graph{
            .stage_path = "/World/Graph_Meshes/Terrain",
            .name       = "Terrain",
            .format     = std::string{erhe::usd::c_geometry_graph_format}
        }
    );
    build_save_scene(data, path, bare_scene);
    const erhe::usd::Usd_save_result save = erhe::usd::save_usda(bare_scene.arguments);
    ASSERT_TRUE(save.error.empty()) << save.error;
    const std::vector<std::string> bare_lines = read_lines(path);
    EXPECT_TRUE(has_line_with(bare_lines, "def NodeGraph \"Terrain\""));
    EXPECT_FALSE(has_line_with(bare_lines, "def Mesh \"result\""));
}

} // namespace
