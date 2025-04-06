#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "windows/graph_window.hpp"
#include "windows/sheet_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor {

auto operator+(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] + rhs.int_value[0],
            lhs.int_value[1] + rhs.int_value[1],
            lhs.int_value[2] + rhs.int_value[2],
            lhs.int_value[3] + rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] + rhs.float_value[0],
            lhs.float_value[1] + rhs.float_value[1],
            lhs.float_value[2] + rhs.float_value[2],
            lhs.float_value[3] + rhs.float_value[3]
        }
    };
}

auto operator-(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] - rhs.int_value[0],
            lhs.int_value[1] - rhs.int_value[1],
            lhs.int_value[2] - rhs.int_value[2],
            lhs.int_value[3] - rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] - rhs.float_value[0],
            lhs.float_value[1] - rhs.float_value[1],
            lhs.float_value[2] - rhs.float_value[2],
            lhs.float_value[3] - rhs.float_value[3]
        }
    };
}

auto operator*(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] * rhs.int_value[0],
            lhs.int_value[1] * rhs.int_value[1],
            lhs.int_value[2] * rhs.int_value[2],
            lhs.int_value[3] * rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] * rhs.float_value[0],
            lhs.float_value[1] * rhs.float_value[1],
            lhs.float_value[2] * rhs.float_value[2],
            lhs.float_value[3] * rhs.float_value[3]
        }
    };
}

auto operator/(const Payload& lhs, const Payload& rhs) -> Payload
{
    ERHE_VERIFY(lhs.format == rhs.format); // TODO implicit cast?
    return Payload{
        .format    = lhs.format,
        .int_value = {
            lhs.int_value[0] / ((rhs.int_value[0] != 0) ? rhs.int_value[0] : 1),
            lhs.int_value[1] / ((rhs.int_value[1] != 0) ? rhs.int_value[1] : 1),
            lhs.int_value[2] / ((rhs.int_value[2] != 0) ? rhs.int_value[2] : 1),
            lhs.int_value[3] / ((rhs.int_value[3] != 0) ? rhs.int_value[3] : 1)
        },
        .float_value = { 
            lhs.float_value[0] / ((rhs.float_value[0] != 0.0f) ? rhs.float_value[0] : 1.0f),
            lhs.float_value[1] / ((rhs.float_value[1] != 0.0f) ? rhs.float_value[1] : 1.0f),
            lhs.float_value[2] / ((rhs.float_value[2] != 0.0f) ? rhs.float_value[2] : 1.0f),
            lhs.float_value[3] / ((rhs.float_value[3] != 0.0f) ? rhs.float_value[3] : 1.0f)
        }
    };
}

auto Payload::operator+=(const Payload& rhs) -> Payload&
{
    int_value  [0] += rhs.int_value  [0];
    int_value  [1] += rhs.int_value  [1];
    int_value  [2] += rhs.int_value  [2];
    int_value  [3] += rhs.int_value  [3];
    float_value[0] += rhs.float_value[0];
    float_value[1] += rhs.float_value[1];
    float_value[2] += rhs.float_value[2];
    float_value[3] += rhs.float_value[3];
    return *this;
}

auto Payload::operator-=(const Payload& rhs) -> Payload&
{
    int_value  [0] -= rhs.int_value  [0];
    int_value  [1] -= rhs.int_value  [1];
    int_value  [2] -= rhs.int_value  [2];
    int_value  [3] -= rhs.int_value  [3];
    float_value[0] -= rhs.float_value[0];
    float_value[1] -= rhs.float_value[1];
    float_value[2] -= rhs.float_value[2];
    float_value[3] -= rhs.float_value[3];
    return *this;
}

auto Payload::operator*=(const Payload& rhs) -> Payload&
{
    int_value  [0] *= rhs.int_value  [0];
    int_value  [1] *= rhs.int_value  [1];
    int_value  [2] *= rhs.int_value  [2];
    int_value  [3] *= rhs.int_value  [3];
    float_value[0] *= rhs.float_value[0];
    float_value[1] *= rhs.float_value[1];
    float_value[2] *= rhs.float_value[2];
    float_value[3] *= rhs.float_value[3];
    return *this;
}

auto Payload::operator/=(const Payload& rhs) -> Payload&
{
    int_value  [0] /= (rhs.int_value[0] != 0) ? rhs.int_value[0] : 1;
    int_value  [1] /= (rhs.int_value[1] != 0) ? rhs.int_value[1] : 1;
    int_value  [2] /= (rhs.int_value[2] != 0) ? rhs.int_value[2] : 1;
    int_value  [3] /= (rhs.int_value[3] != 0) ? rhs.int_value[3] : 1;
    float_value[0] /= (rhs.float_value[0] != 0.0f) ? rhs.int_value[0] : 1.0f;
    float_value[1] /= (rhs.float_value[1] != 0.0f) ? rhs.int_value[1] : 1.0f;
    float_value[2] /= (rhs.float_value[2] != 0.0f) ? rhs.int_value[2] : 1.0f;
    float_value[3] /= (rhs.float_value[3] != 0.0f) ? rhs.int_value[3] : 1.0f;
    return *this;
}

static constexpr std::size_t pin_key_todo = 1;

class Constant : public Shader_graph_node
{
public:
    Constant();

    void evaluate(Shader_graph&) override;
    void imgui   () override;

    Payload m_payload;
};

Constant::Constant()
    : Shader_graph_node{"Constant"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_output_pin(pin_key_todo, "out");
}

void Constant::evaluate(Shader_graph&)
{
    ERHE_VERIFY(get_output_pins().size() == 1);
    set_output(0, m_payload);
}

void Constant::imgui()
{
    ImGui::Text("Constant Scalar");
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##", &m_payload.int_value[0]);
}

class Add : public Shader_graph_node
{
public:
    Add();
    void evaluate(Shader_graph&) override;
    void imgui() override;
};

Add::Add()
    : Shader_graph_node{"Add"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Add::evaluate(Shader_graph&)
{
    const Payload& lhs    = accumulate_input_from_links(0);
    const Payload& rhs    = accumulate_input_from_links(1);
    const Payload  result = lhs + rhs;
    set_output(0, result);
}

void Add::imgui()
{
    ImGui::Text("Add");
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d + %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

class Sub : public Shader_graph_node
{
public:
    Sub();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

Sub::Sub()
    : Shader_graph_node{"Sub"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Sub::evaluate(Shader_graph&)
{
    const Payload lhs    = accumulate_input_from_links(0);
    const Payload rhs    = accumulate_input_from_links(1);
    const Payload result = lhs - rhs;
    set_output(0, result);
}

void Sub::imgui()
{
    ImGui::Text("Subtract");
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d - %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

class Mul : public Shader_graph_node
{
public:
    Mul();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

Mul::Mul()
    : Shader_graph_node{"Mul"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Mul::evaluate(Shader_graph&)
{
    const Payload lhs    = accumulate_input_from_links(0);
    const Payload rhs    = accumulate_input_from_links(1);
    const Payload result = lhs * rhs;
    set_output(0, result);
}

void Mul::imgui()
{
    ImGui::Text("Multiply");
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d * %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

class Div : public Shader_graph_node
{
public:
    Div();

    void evaluate(Shader_graph&) override;
    void imgui   () override;
};

Div::Div()
    : Shader_graph_node{"Div"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Div::evaluate(Shader_graph&)
{
    const Payload lhs    = accumulate_input_from_links(0);
    const Payload rhs    = accumulate_input_from_links(1);
    const Payload result = lhs / rhs;
    set_output(0, result);
}

void Div::imgui()
{
    ImGui::Text("Divide");
    const Payload lhs = accumulate_input_from_links(0);
    const Payload rhs = accumulate_input_from_links(1);
    const Payload out = get_output(0);
    ImGui::Text("%d / %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

class Load : public Shader_graph_node
{
public:
    Load();

    void evaluate(Shader_graph&) override;
    void imgui   () override;

private:
    int     m_row{0};
    int     m_col{0};
    Payload m_payload;
};

Load::Load()
    : Shader_graph_node{"Load"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_output_pin(pin_key_todo, "out");
}

void Load::evaluate(Shader_graph& graph)
{
    ERHE_VERIFY(get_output_pins().size() == 1);
    double double_value = graph.load(m_row, m_col);
    m_payload = Payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { static_cast<int>(double_value), static_cast<int>(double_value), static_cast<int>(double_value), static_cast<int>(double_value) },
        .float_value = { static_cast<float>(double_value), static_cast<float>(double_value), static_cast<float>(double_value), static_cast<float>(double_value) }
    };
    set_output(0, m_payload);
}

void Load::imgui()
{
    ImGui::Text("Load");
    ImGui::Text("%d", m_payload.int_value[0]); // TODO Handle format
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Row##", &m_row);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Col##", &m_col);
}

class Store : public Shader_graph_node
{
public:
    Store();

    void evaluate(Shader_graph& graph) override;
    void imgui   () override;

private:
    int     m_row{0};
    int     m_col{0};
    Payload m_payload;
};

Store::Store()
    : Shader_graph_node{"Store"}
    , m_payload{
        .format      = erhe::dataformat::Format::format_32_scalar_sint,
        .int_value   = { 0, 0, 0, 0 },
        .float_value = { 0.0f, 0.0f, 0.0f, 0.0f }
    }
{
    make_input_pin(pin_key_todo, "A");
}

void Store::evaluate(Shader_graph& graph)
{
    ERHE_VERIFY(get_input_pins().size() == 1);
    m_payload = accumulate_input_from_links(0); // TODO Handle format
    graph.store(m_row, m_col, static_cast<double>(m_payload.int_value[0]));
}

void Store::imgui()
{
    ImGui::Text("Store");
    ImGui::Text("%d", m_payload.int_value[0]); // TODO Handle format
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Row##", &m_row);
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("Col##", &m_col);
}

Shader_graph_node::Shader_graph_node(const char* label)
    : erhe::graph::Node{label}
{
}

// For given slot / pin, accumulates payload from all connected links
auto Shader_graph_node::accumulate_input_from_links(const std::size_t i) -> Payload
{
    erhe::graph::Pin& input_pin = get_input_pins().at(i);

    if (input_pin.is_sink()) { // && pull
        Payload sum{};
        for (erhe::graph::Link* link : input_pin.get_links()) {
            erhe::graph::Pin*  source_pin               = link->get_source();
            std::size_t        slot                     = source_pin->get_slot();
            erhe::graph::Node* source_node              = source_pin->get_owner_node();
            Shader_graph_node* source_shader_graph_node = dynamic_cast<Shader_graph_node*>(source_node);
            sum += source_shader_graph_node->get_output(slot);
        }
        return sum;
    }
    return {};
}

auto Shader_graph_node::get_input(std::size_t i) const -> Payload
{
    return m_input_payloads.at(i);
}

void Shader_graph_node::set_input(std::size_t i, Payload value)
{
    m_input_payloads.at(i) = value;
}

auto Shader_graph_node::get_output(const std::size_t i) const -> Payload
{
    return m_output_payloads.at(i);
}

void Shader_graph_node::set_output(const std::size_t i, Payload payload)
{
    m_output_payloads.at(i) = payload;
}

void Shader_graph_node::make_input_pin(std::size_t key, std::string_view name)
{
    m_input_payloads.emplace_back(std::move(Payload{}));
    base_make_input_pin(key, name);
}

void Shader_graph_node::make_output_pin(std::size_t key, std::string_view name)
{
    m_output_payloads.emplace_back(std::move(Payload{}));
    base_make_output_pin(key, name);
}

void Shader_graph_node::evaluate(Shader_graph&)
{
    // Overridden in derived classes
}

void Shader_graph_node::imgui()
{
    ImGui::TextUnformatted(m_name.c_str());
}

void Shader_graph::evaluate(Sheet* sheet)
{
    sort();
    m_sheet = sheet;
    for (erhe::graph::Node* node : m_nodes) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(node);
        shader_graph_node->evaluate(*this);
    }
    m_sheet = nullptr;
}

auto Shader_graph::load(int row, int column) const -> double
{
    if (m_sheet == nullptr) {
        return 0.0;
    }
    return m_sheet->get_value(row, column);
}

void Shader_graph::store(int row, int column, double value)
{
    if (m_sheet == nullptr) {
        return;
    }
    m_sheet->set_value(row, column, value);
    m_sheet->evaluate_expressions();
}

Graph_window::Graph_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Graph", "graph"}
    , m_context                {editor_context}
{
    static_cast<void>(commands); // TODO Keeping in case we need to add commands here

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

Graph_window::~Graph_window() noexcept
{
}

void Graph_window::on_message(Editor_message&)
{
    //// using namespace erhe::bit;
    //// if (test_any_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
    //// }
}

auto Graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

auto Graph_window::make_constant() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Constant>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_load() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Load>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}
auto Graph_window::make_store() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Store>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_add() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Add>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_sub() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Sub>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_mul() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Mul>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

auto Graph_window::make_div() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Div>());
    Shader_graph_node* node = m_nodes.back().get();
    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    node->enable_flag_bits(flags);
    return node;
}

void Graph_window::imgui()
{
    if (!m_node_editor) {
        ax::NodeEditor::Config config;
        m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);
    }  

                       if (ImGui::Button("+")) { m_graph.register_node(make_add()); }
    ImGui::SameLine(); if (ImGui::Button("-")) { m_graph.register_node(make_sub()); }
    ImGui::SameLine(); if (ImGui::Button("*")) { m_graph.register_node(make_mul()); }
    ImGui::SameLine(); if (ImGui::Button("/")) { m_graph.register_node(make_div()); }
    ImGui::SameLine(); if (ImGui::Button("1")) { m_graph.register_node(make_constant()); }
    ImGui::SameLine(); if (ImGui::Button("L")) { m_graph.register_node(make_load()); }
    ImGui::SameLine(); if (ImGui::Button("S")) { m_graph.register_node(make_store()); }

    Sheet_window* sheet_window = m_context.sheet_window;
    Sheet* sheet = (sheet_window != nullptr) ? sheet_window->get_sheet() : nullptr;
    m_graph.evaluate(sheet);

    m_node_editor->Begin("Graph", ImVec2{0.0f, 0.0f});

    for (erhe::graph::Node* node : m_graph.get_nodes()) {
        Shader_graph_node* shader_graph_node = dynamic_cast<Shader_graph_node*>(node);
        log_graph_editor->info("Shader_graph_node {} {}", node->get_name(), node->get_id());

        ImGui::PushID(static_cast<int>(node->get_id()));
        ERHE_DEFER( ImGui::PopID(); );

        m_node_editor->BeginNode(node->get_id());

        ImVec2 pin_table_size{100.0f, 0.0f};

        // Input pins
        ImGui::BeginTable("##InputPins", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("InputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("InputLabel", ImGuiTableColumnFlags_None);
        for (const erhe::graph::Pin& pin : node->get_input_pins()) {
            log_graph_editor->info("  Input Pin {} {}", pin.get_name(), pin.get_id());
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            m_node_editor->BeginPin(
                ax::NodeEditor::PinId{&pin},
                ax::NodeEditor::PinKind::Input
            );
            ImGui::Bullet();
            m_node_editor->EndPin();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(pin.get_name().data());
        }
        ImGui::EndTable();

        ImGui::SameLine();

        // Content
        shader_graph_node->imgui();

        ImGui::SameLine();

        // Output pins
        ImGui::BeginTable("##OutputPins", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("OutputLabel", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("OutputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        for (const erhe::graph::Pin& pin : node->get_output_pins()) {
            log_graph_editor->info("  Output Pin {} {}", pin.get_name(), pin.get_id());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const char* label = pin.get_name().data();
            float column_width = ImGui::GetColumnWidth();
            float text_width   = ImGui::CalcTextSize(label).x;
            float padding      = column_width - text_width;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding);
            ImGui::TextUnformatted(label);

            ImGui::TableSetColumnIndex(1);
            m_node_editor->BeginPin(
                ax::NodeEditor::PinId{&pin},
                ax::NodeEditor::PinKind::Output
            );
            ImGui::Bullet();
            m_node_editor->EndPin();
        }
        ImGui::EndTable(); // Outputs

        m_node_editor->EndNode();
        const bool item_selection   = node->is_selected();
        const bool editor_selection = m_node_editor->IsNodeSelected(node->get_id());
        if (item_selection != editor_selection) {
            if (editor_selection) {
                m_context.selection->add_to_selection(node->shared_from_this());
            } else {
                m_context.selection->remove_from_selection(node->shared_from_this());
            }
        }
    }

    // Links
    for (const std::unique_ptr<erhe::graph::Link>& link : m_graph.get_links()) {
        log_graph_editor->info(
            "  Link source {} {} sink {} {}",
            link->get_source()->get_name(), link->get_source()->get_id(),
            link->get_sink  ()->get_name(), link->get_sink  ()->get_id()
        );
        m_node_editor->Link(
            ax::NodeEditor::LinkId{link.get()},
            ax::NodeEditor::PinId{link->get_source()},
            ax::NodeEditor::PinId{link->get_sink()}
        );
    }

    if (m_node_editor->BeginCreate()) {
        ax::NodeEditor::PinId lhs_pin_handle;
        ax::NodeEditor::PinId rhs_pin_handle;
        if (m_node_editor->QueryNewLink(&lhs_pin_handle, &rhs_pin_handle)) {
            bool acceptable = false;
            if (rhs_pin_handle && lhs_pin_handle) {
                erhe::graph::Pin*  lhs_pin     = lhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  rhs_pin     = rhs_pin_handle.AsPointer<erhe::graph::Pin>();
                erhe::graph::Pin*  source_pin  = lhs_pin->is_source() ? lhs_pin : rhs_pin->is_source() ? rhs_pin : nullptr;
                erhe::graph::Pin*  sink_pin    = lhs_pin->is_sink  () ? lhs_pin : rhs_pin->is_sink  () ? rhs_pin : nullptr;
                erhe::graph::Node* source_node = (source_pin != nullptr) ? source_pin->get_owner_node() : nullptr;
                erhe::graph::Node* sink_node   = (sink_pin   != nullptr) ? sink_pin  ->get_owner_node() : nullptr;
                if ((source_pin != nullptr) && (sink_pin != nullptr) && (source_pin != sink_pin) && (source_node != sink_node)) {
                    acceptable = true;
                    if (m_node_editor->AcceptNewItem()) { // mouse released?
                        erhe::graph::Link* link = m_graph.connect(source_pin, sink_pin);
                        if (link != nullptr) {
                            m_node_editor->Link(
                                ax::NodeEditor::LinkId{link},
                                ax::NodeEditor::PinId{source_pin},
                                ax::NodeEditor::PinId{sink_pin}
                            );
                        }
                    }
                } 
            }
            if (!acceptable) {
                m_node_editor->RejectNewItem();
            }
        }
    }
    m_node_editor->EndCreate();

    if (m_node_editor->BeginDelete()) {
        ax::NodeEditor::NodeId node_handle = 0;
        while (m_node_editor->QueryDeletedNode(&node_handle)){
            if (m_node_editor->AcceptDeletedItem()) {
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::shared_ptr<Shader_graph_node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = ax::NodeEditor::NodeId{entry.get()};
                    return entry_node_id == node_handle;
                });
                if (i != m_nodes.end()) {
                    m_graph.unregister_node(i->get());
                    m_nodes.erase(i);
                }
            }
        }

        ax::NodeEditor::LinkId link_handle;
        while (m_node_editor->QueryDeletedLink(&link_handle)) {
            if (m_node_editor->AcceptDeletedItem()) {
                std::vector<std::unique_ptr<erhe::graph::Link>>& links = m_graph.get_links();
                auto i = std::find_if(links.begin(), links.end(), [link_handle](const std::unique_ptr<erhe::graph::Link>& entry){
                    ax::NodeEditor::LinkId entry_link_id = ax::NodeEditor::LinkId{entry.get()};
                    return entry_link_id == link_handle;
                });
                if (i != links.end()) {
                    m_graph.disconnect(i->get());
                }
            }
        }
    }
    m_node_editor->EndDelete();

    m_node_editor->End();
}

} // namespace editor
