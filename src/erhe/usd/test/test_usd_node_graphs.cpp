// Texture node graphs as marked `NodeGraph` prims
// (doc/usd-texture-graphs-plan.md). The reader records every `NodeGraph` prim
// carrying the `erhe:graph:format` marker with its nodes, their parameters,
// their pins and the links between them, records which material slot reads a
// graph, and stops the scene conversion at the graph, so its `Shader` children
// are no shading network of the scene; the writer puts a graph record back as
// the same prims. erhe::usd creates no graph asset - that is the editor's half
// of the step - so the export side stands a plain `erhe::Scope` in for the
// asset item and hands the writer the record, which is exactly what makes an
// item a graph prim to the writer.

#include "erhe_item/item.hpp"
#include "erhe_item/scope.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/material.hpp"
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
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "erhe_usd_node_graph_tests";
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

[[nodiscard]] auto material_index_of(const erhe::usd::Usd_data& data, const std::string& name) -> std::size_t
{
    for (std::size_t index = 0, end = data.materials.size(); index < end; ++index) {
        if (data.materials[index] && (data.materials[index]->get_name() == name)) {
            return index;
        }
    }
    return data.materials.size();
}

// What one save of the graphs a load recorded needs: the stand-in tree the
// graph prims sit in, kept alive for as long as the arguments are used.
class Save_scene final
{
public:
    std::shared_ptr<erhe::scene::Node>                      root;
    std::vector<std::shared_ptr<erhe::Scope>>               graph_items;
    std::shared_ptr<erhe::primitive::Material>              material;
    erhe::usd::Usd_save_arguments                           arguments;
};

// The save arguments that write back what one load read: one stand-in item
// per recorded graph, at the place the stage gave it, and the material whose
// slot the load found bound to one of them.
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

    std::shared_ptr<erhe::Scope> graph_scope = std::make_shared<erhe::Scope>("Graph_Textures");
    graph_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
    graph_scope->set_parent(world);

    std::shared_ptr<erhe::Scope> looks_scope = std::make_shared<erhe::Scope>("Looks");
    looks_scope->enable_flag_bits(erhe::Item_flags::show_in_ui);
    looks_scope->set_parent(world);

    out_scene.material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{.name = "Iron"}
    );
    out_scene.material->set_parent(looks_scope);

    out_scene.arguments.path      = path;
    out_scene.arguments.root_node = out_scene.root;
    out_scene.arguments.materials = std::vector<std::shared_ptr<erhe::primitive::Material>>{out_scene.material};

    for (const erhe::usd::Usd_node_graph& graph : data.node_graphs) {
        std::shared_ptr<erhe::Scope> item = std::make_shared<erhe::Scope>(graph.name);
        item->enable_flag_bits(erhe::Item_flags::show_in_ui);
        item->set_parent(graph_scope);
        out_scene.graph_items.push_back(item);
        out_scene.arguments.node_graphs.push_back(
            erhe::usd::Usd_save_node_graph{
                .item    = item,
                .format  = graph.format,
                .outputs = graph.outputs,
                .nodes   = graph.nodes
            }
        );
    }
    for (const erhe::usd::Usd_material_graph_binding& binding : data.material_graph_bindings) {
        for (std::size_t index = 0, end = data.node_graphs.size(); index < end; ++index) {
            if (data.node_graphs[index].stage_path == binding.graph_path) {
                out_scene.arguments.material_graph_bindings.push_back(
                    erhe::usd::Usd_save_material_graph_binding{
                        .material_index = 0,
                        .slot           = binding.slot,
                        .graph          = out_scene.graph_items[index]
                    }
                );
                break;
            }
        }
    }
}

class Node_graphs_import : public testing::Test
{
protected:
    void SetUp() override
    {
        root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("texture_graph.usda"),
            .root_node     = root,
            .mesh_layer_id = 0
        };
        loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    }

    std::shared_ptr<erhe::scene::Node> root;
    erhe::usd::Usd_load_result         loaded;
};

TEST_F(Node_graphs_import, a_marked_node_graph_records_its_nodes_in_authored_order)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->name,   "Rust");
    EXPECT_EQ(graph->format, "erhe_texture_graph");
    ASSERT_EQ(graph->nodes.size(), 3u);
    EXPECT_EQ(graph->nodes[0].name, "Noise");
    EXPECT_EQ(graph->nodes[1].name, "Colorize");
    EXPECT_EQ(graph->nodes[2].name, "Output");
    EXPECT_EQ(graph->nodes[0].type_name, "noise");
    EXPECT_EQ(graph->nodes[1].type_name, "colorize");
    EXPECT_EQ(graph->nodes[2].type_name, "output");
}

TEST_F(Node_graphs_import, a_node_records_its_editor_position)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);
    const erhe::usd::Usd_node_graph_node* noise = find_node(*graph, "Noise");
    ASSERT_NE(noise, nullptr);
    EXPECT_TRUE(noise->has_position);
    EXPECT_FLOAT_EQ(noise->position_x, 120.0f);
    EXPECT_FLOAT_EQ(noise->position_y,  40.0f);
}

TEST_F(Node_graphs_import, an_input_with_a_value_is_a_parameter_of_its_authored_type)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);
    const erhe::usd::Usd_node_graph_node* noise = find_node(*graph, "Noise");
    ASSERT_NE(noise, nullptr);
    ASSERT_EQ(noise->parameters.size(), 2u);
    const erhe::usd::Usd_node_graph_parameter* density = find_parameter(*noise, "density");
    ASSERT_NE(density, nullptr);
    EXPECT_EQ(density->usd_type, "float");
    EXPECT_EQ(density->value,    "0.35");
    const erhe::usd::Usd_node_graph_parameter* size = find_parameter(*noise, "size");
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->usd_type, "int");
    EXPECT_EQ(size->value,    "5");

    // A type with no USD form travels as its text in a `string`, which is how
    // a gradient rides the file (doc/usd-texture-graphs-plan.md 2.1).
    const erhe::usd::Usd_node_graph_node* colorize = find_node(*graph, "Colorize");
    ASSERT_NE(colorize, nullptr);
    const erhe::usd::Usd_node_graph_parameter* gradient = find_parameter(*colorize, "gradient");
    ASSERT_NE(gradient, nullptr);
    EXPECT_EQ(gradient->usd_type, "string");
    EXPECT_EQ(
        gradient->value,
        "\"{'interpolation':0,'stops':[{'color':[0.0,0.0,0.0,1.0],'pos':0.0},"
        "{'color':[1.0,0.5,0.2,1.0],'pos':1.0}]}\""
    );
}

TEST_F(Node_graphs_import, a_connected_input_records_the_source_node_and_pin)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);

    const erhe::usd::Usd_node_graph_node* colorize = find_node(*graph, "Colorize");
    ASSERT_NE(colorize, nullptr);
    const erhe::usd::Usd_node_graph_pin* value = find_pin(colorize->inputs, "input");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value_type,  "float");
    EXPECT_EQ(value->source_node, "Noise");
    EXPECT_EQ(value->source_pin,  "f");

    const erhe::usd::Usd_node_graph_node* output = find_node(*graph, "Output");
    ASSERT_NE(output, nullptr);
    const erhe::usd::Usd_node_graph_pin* rgba = find_pin(output->inputs, "rgba");
    ASSERT_NE(rgba, nullptr);
    EXPECT_EQ(rgba->value_type,  "color4f");
    EXPECT_EQ(rgba->source_node, "Colorize");
    EXPECT_EQ(rgba->source_pin,  "rgba");

    const erhe::usd::Usd_node_graph_pin* f = find_pin(find_node(*graph, "Noise")->outputs, "f");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->value_type, "float");
}

TEST_F(Node_graphs_import, the_graph_records_its_interface_output)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(graph->outputs.size(), 1u);
    EXPECT_EQ(graph->outputs[0].name,        "rgba");
    EXPECT_EQ(graph->outputs[0].value_type,  "color4f");
    EXPECT_EQ(graph->outputs[0].source_node, "Colorize");
    EXPECT_EQ(graph->outputs[0].source_pin,  "rgba");
}

TEST_F(Node_graphs_import, a_material_input_connected_to_a_graph_is_a_slot_binding)
{
    const std::size_t iron = material_index_of(loaded.data, "Iron");
    ASSERT_LT(iron, loaded.data.materials.size());
    ASSERT_EQ(loaded.data.material_graph_bindings.size(), 1u);
    EXPECT_EQ(loaded.data.material_graph_bindings[0].material_index, iron);
    EXPECT_EQ(loaded.data.material_graph_bindings[0].slot,           erhe::usd::Usd_material_texture_slot::base_color);
    EXPECT_EQ(loaded.data.material_graph_bindings[0].graph_path,     "/World/Graph_Textures/Rust");
}

TEST_F(Node_graphs_import, an_unmarked_node_graph_is_no_erhe_graph)
{
    EXPECT_EQ(find_graph(loaded.data, "/World/Graph_Textures/Foreign"), nullptr);
    EXPECT_EQ(loaded.warning.find("Foreign"), std::string::npos) << loaded.warning;
}

TEST_F(Node_graphs_import, a_shader_with_a_foreign_info_id_is_no_node_and_its_links_are_dropped)
{
    const erhe::usd::Usd_node_graph* graph = find_graph(loaded.data, "/World/Graph_Textures/Mixed");
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(graph->nodes.size(), 1u);
    EXPECT_EQ(graph->nodes[0].name, "Constant");
    const erhe::usd::Usd_node_graph_pin* amount = find_pin(graph->nodes[0].inputs, "amount");
    ASSERT_NE(amount, nullptr);
    EXPECT_TRUE(amount->source_node.empty());
    EXPECT_TRUE(amount->source_pin.empty());
}

TEST_F(Node_graphs_import, a_graph_prim_is_no_scene_content)
{
    for (const std::shared_ptr<erhe::scene::Node>& node : loaded.data.nodes) {
        ASSERT_TRUE(node);
        EXPECT_NE(node->get_name(), "Rust");
        EXPECT_NE(node->get_name(), "Noise");
        EXPECT_NE(node->get_name(), "Colorize");
        EXPECT_NE(node->get_name(), "Output");
    }
    for (const std::shared_ptr<erhe::Typed>& prim : loaded.data.prims) {
        ASSERT_TRUE(prim);
        EXPECT_NE(prim->get_name(), "Rust");
        EXPECT_NE(prim->get_name(), "Noise");
        EXPECT_NE(prim->get_name(), "Colorize");
        EXPECT_NE(prim->get_name(), "Output");
    }
}

class Node_graphs_export : public testing::Test
{
protected:
    void SetUp() override
    {
        std::shared_ptr<erhe::scene::Node>  load_root = std::make_shared<erhe::scene::Xform>("import_root");
        const erhe::usd::Usd_load_arguments load_arguments{
            .path          = test_data_path("texture_graph.usda"),
            .root_node     = load_root,
            .mesh_layer_id = 0
        };
        const erhe::usd::Usd_load_result loaded = erhe::usd::load_usd(load_arguments);
        ASSERT_TRUE(loaded.error.empty()) << loaded.error;

        written_path = temporary_path("texture_graph.usda");
        build_save_scene(loaded.data, written_path, scene);
        const erhe::usd::Usd_save_result save = erhe::usd::save_usda(scene.arguments);
        ASSERT_TRUE(save.error.empty()) << save.error;
        lines = read_lines(written_path);
    }

    std::filesystem::path    written_path;
    Save_scene               scene;
    std::vector<std::string> lines;
};

TEST_F(Node_graphs_export, a_graph_record_is_written_as_a_marked_node_graph_prim)
{
    EXPECT_TRUE(has_line_with(lines, "def NodeGraph \"Rust\""));
    EXPECT_TRUE(has_line_with(lines, "custom token erhe:graph:format = \"erhe_texture_graph\""));
    EXPECT_TRUE(has_line_with(lines, "color4f outputs:rgba.connect = </World/Graph_Textures/Rust/Colorize.outputs:rgba>"));
}

TEST_F(Node_graphs_export, a_node_is_written_as_a_generic_shader_prim_with_its_pins)
{
    EXPECT_TRUE(has_line_with(lines, "def Shader \"Noise\""));
    EXPECT_TRUE(has_line_with(lines, "uniform token info:id = \"erhe:texture:noise\""));
    EXPECT_TRUE(has_line_with(lines, "custom float2 erhe:ui:position = (120, 40)"));
    EXPECT_TRUE(has_line_with(lines, "float inputs:density = 0.35"));
    EXPECT_TRUE(has_line_with(lines, "int inputs:size = 5"));
    EXPECT_TRUE(has_line_with(lines, "float outputs:f"));
    EXPECT_TRUE(has_line_with(lines, "string inputs:gradient = "));
    EXPECT_TRUE(
        has_line_with(lines, "float inputs:input.connect = </World/Graph_Textures/Rust/Noise.outputs:f>")
    );
    EXPECT_TRUE(
        has_line_with(lines, "color4f inputs:rgba.connect = </World/Graph_Textures/Rust/Colorize.outputs:rgba>")
    );
}

TEST_F(Node_graphs_export, a_graph_bound_material_slot_connects_to_the_graph_output)
{
    EXPECT_TRUE(
        has_line_with(lines, "color3f inputs:diffuseColor.connect = </World/Graph_Textures/Rust.outputs:rgba>")
    );
    EXPECT_FALSE(has_line_with(lines, "UsdUVTexture"));
}

// The written prims read back as the records they were, and writing those
// again spells the same file (doc/usd-texture-graphs-plan.md R4).
TEST_F(Node_graphs_export, a_second_save_of_the_reloaded_graphs_is_byte_identical)
{
    std::shared_ptr<erhe::scene::Node>  reload_root = std::make_shared<erhe::scene::Xform>("reload_root");
    const erhe::usd::Usd_load_arguments load_arguments{
        .path          = written_path,
        .root_node     = reload_root,
        .mesh_layer_id = 0
    };
    const erhe::usd::Usd_load_result reloaded = erhe::usd::load_usd(load_arguments);
    ASSERT_TRUE(reloaded.error.empty()) << reloaded.error;

    const erhe::usd::Usd_node_graph* graph = find_graph(reloaded.data, "/World/Graph_Textures/Rust");
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(graph->nodes.size(), 3u);
    EXPECT_EQ(graph->format, "erhe_texture_graph");
    ASSERT_EQ(reloaded.data.material_graph_bindings.size(), 1u);
    EXPECT_EQ(reloaded.data.material_graph_bindings[0].graph_path, "/World/Graph_Textures/Rust");

    const std::filesystem::path second_path = temporary_path("texture_graph_2.usda");
    Save_scene                  second_scene;
    build_save_scene(reloaded.data, second_path, second_scene);
    const erhe::usd::Usd_save_result second = erhe::usd::save_usda(second_scene.arguments);
    ASSERT_TRUE(second.error.empty()) << second.error;
    EXPECT_EQ(read_lines(second_path), lines);
}

} // anonymous namespace
