#pragma once

#include "erhe_commands/command.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include "tinyexpr/tinyexpr.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graph {
    class Link;
    class Pin;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace ax::NodeEditor {
    class EditorContext;
}

namespace editor {

class Editor_context;
class Editor_message;
class Editor_message_bus;

class Payload
{
public:
    auto operator+=(const Payload& rhs) -> Payload&;
    auto operator-=(const Payload& rhs) -> Payload&;
    auto operator*=(const Payload& rhs) -> Payload&;
    auto operator/=(const Payload& rhs) -> Payload&;

    erhe::dataformat::Format format;
    std::array<int, 4>       int_value;
    std::array<float, 4>     float_value;
};

auto operator+(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator-(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator*(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator/(const Payload& lhs, const Payload& rhs) -> Payload;

class Sheet;

class Shader_graph : public erhe::graph::Graph
{
public:
    void evaluate(Sheet* sheet);
    auto load    (int row, int column) const -> double;
    void store   (int row, int column, double value);

private:
    Sheet* m_sheet{nullptr};
};

class Shader_graph_node : public erhe::graph::Node
{
public:
    Shader_graph_node(const char* label);

    auto accumulate_input_from_links(std::size_t i) -> Payload;
    auto get_output                 (std::size_t i) const -> Payload;
    auto get_input                  (std::size_t i) const -> Payload;
    void set_input                  (std::size_t i, Payload payload);
    void set_output                 (std::size_t i, Payload payload);
    void make_input_pin             (std::size_t key, std::string_view name);
    void make_output_pin            (std::size_t key, std::string_view name);

    virtual void evaluate(Shader_graph& graph);
    virtual void imgui   ();

private:
    std::vector<Payload> m_input_payloads;
    std::vector<Payload> m_output_payloads;
};

class Graph_window : public erhe::imgui::Imgui_window
{
public:
    Graph_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus
    );
    ~Graph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

private:
    auto make_constant() -> Shader_graph_node*;
    auto make_load    () -> Shader_graph_node*;
    auto make_store   () -> Shader_graph_node*;
    auto make_add     () -> Shader_graph_node*;
    auto make_sub     () -> Shader_graph_node*;
    auto make_mul     () -> Shader_graph_node*;
    auto make_div     () -> Shader_graph_node*;

    void on_message(Editor_message& message);

    Editor_context&                                m_context;
    Shader_graph                                   m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;

    std::vector<std::shared_ptr<Shader_graph_node>> m_nodes;
};


} // namespace editor
