#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "windows/graph_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

namespace editor {

auto Link::get_id     () const -> int   { return m_id; }
auto Link::get_handle () const -> ax::NodeEditor::LinkId{ return ax::NodeEditor::LinkId{static_cast<std::size_t>(m_id)}; }
auto Link::get_source () const -> Pin*  { return m_source; }
auto Link::get_sink   () const -> Pin*  { return m_sink; }
auto Link::get_payload() const -> payload_t { return m_source->get_payload(); }
void Link::set_payload(payload_t value) { m_source->set_payload(value); }

auto Link::is_connected() const -> bool { return (m_source != nullptr) && (m_sink != nullptr); }

void Link::disconnect()
{
    m_source->remove_link(this);
    m_sink  ->remove_link(this);
    m_source = nullptr;
    m_sink = nullptr;
}

void Node::evaluate() {
    if (m_evaluate) {
        m_evaluate(*this);
    }
}

void Node::imgui() {
    if (m_imgui) {
        m_imgui(*this);
    } else {
        ImGui::TextUnformatted(m_name.c_str());
    }
}

void Node::make_input_pin(std::size_t key, std::string_view name)
{
    m_input_pins.emplace_back(std::move(Pin{this, true, key, name}));
}

void Node::make_output_pin(std::size_t key, std::string_view name)
{
    m_output_pins.emplace_back(std::move(Pin{this, false, key, name}));
}

void Graph::register_node(Node* node)
{
#if !defined(NDEBUG)
    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Node* entry) { return entry == node; });
    if (i != m_nodes.end()) {
        log_graph_editor->error("Node {} {} is already registered to Graph", node->get_name(), node->get_id());
        return;
    }
#endif
    m_nodes.push_back(node);
    log_graph_editor->trace("Registered Node {} {}", node->get_name(), node->get_id());
}

void Graph::unregister_node(Node* node)
{
    if (node == nullptr) {
        return;
    }

    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Node* entry) { return entry == node; });
    if (i == m_nodes.end()) {
        log_graph_editor->error("Graph::unregister_node(): Node {} {} is not registered", node->get_name(), node->get_id());
        return;
    }

    std::vector<Pin>& input_pins = node->get_input_pins();
    for (Pin& pin : input_pins) {
        for (Link* link : pin.get_links()) {
            disconnect(link);
        }
    }
    std::vector<Pin>& output_pins = node->get_output_pins();
    for (Pin& pin : output_pins) {
        for (Link* link : pin.get_links()) {
            disconnect(link);
        }
    }

    m_nodes.erase(i);

    log_graph_editor->trace("Unregistered Node {} {}", node->get_name(), node->get_id());
}

auto Graph::connect(Pin* source_pin, Pin* sink_pin) -> Link*
{
    ERHE_VERIFY(source_pin != nullptr);
    ERHE_VERIFY(sink_pin != nullptr);

    if (sink_pin->get_key() != source_pin->get_key()) {
        log_graph_editor->warn("Sink pin key {} does not match source pin key {}", sink_pin->get_key(), source_pin->get_key());
        return nullptr;
    }
    m_links.push_back(std::make_unique<Link>(source_pin, sink_pin));
    Link* link = m_links.back().get();
    sink_pin  ->add_link(link);
    source_pin->add_link(link);

    log_graph_editor->trace("Connected {} {} to {} {} ", source_pin->get_name(), source_pin->get_id(), sink_pin->get_name(), sink_pin->get_id());
    return link;
}

void Graph::disconnect(Link* link)
{
    ERHE_VERIFY(link != nullptr);

    auto i = std::find_if(m_links.begin(), m_links.end(), [link](const std::unique_ptr<Link>& entry){ return link == entry.get(); });
    if (i == m_links.end()) {
        log_graph_editor->error("Link not found");
        return;
    }
    link->disconnect();
    m_links.erase(i);
}

void Graph::evaluate()
{
    sort();
    for (Node* node : m_nodes) {
        node->evaluate();
    }
}

void Graph::sort()
{
    std::vector<Node*> unsorted_nodes = m_nodes;
    std::vector<Node*> sorted_nodes;

    while (!unsorted_nodes.empty()) {
        bool found_node{false};
        for (const auto& node : unsorted_nodes) {
            {
                bool any_missing_dependency{false};
                for (const Pin& input : node->get_input_pins()) {
                    for (Link* link : input.get_links()) {
                        // See if dependency is in already sorted nodes
                        const auto i = std::find_if(
                            sorted_nodes.begin(),
                            sorted_nodes.end(),
                            [&link](Node* entry) {
                                return entry == link->get_source()->get_owner_node();
                            }
                        );
                        if (i == sorted_nodes.end()) {
                            any_missing_dependency = true;
                            break;
                        }
                    }
                }
                if (any_missing_dependency) {
                    continue;
                }
            }

            SPDLOG_LOGGER_TRACE(log_graph_editor, "Sort: Selected node {} - all dependencies are met", node->get_name(), node->get_id());
            found_node = true;

            // Add selected node to sorted nodes
            sorted_nodes.push_back(node);

            // Remove from unsorted nodes
            const auto i = std::remove(unsorted_nodes.begin(), unsorted_nodes.end(), node);
            if (i == unsorted_nodes.end()) {
                log_graph_editor->error("Sort: Node {} {} is not in graph nodes", node->get_name(), node->get_id());
            } else {
                unsorted_nodes.erase(i, unsorted_nodes.end());
            }

            // restart loop
            break;
        }

        if (!found_node) {
            log_graph_editor->error("No node with met dependencies found. Graph is not acyclic:");
            return;
        }
    }

    std::swap(m_nodes, sorted_nodes);
}

Graph_window::Graph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Graph", "graph"}
    , m_context                {editor_context}
{
}

Graph_window::~Graph_window() noexcept
{
}

auto Graph_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

static constexpr std::size_t pin_key_scalar = 1;
static constexpr std::size_t pin_key_vector = 2;
static constexpr std::size_t pin_key_matrix = 3;
static constexpr std::size_t pin_key_tensor = 4;

auto Graph_window::make_unary_source() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Constant",
            [](Node& node){
                for (Pin& pin : node.get_output_pins()) {
                    payload_t value = pin.get_payload();
                    for (Link* link : pin.get_links()) {
                        link->get_sink()->set_payload(value);
                    }
                }
            },
            [](Node& node){
                for (Pin& pin : node.get_output_pins()) {
                    payload_t value = pin.get_payload();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::InputInt("##", &value);
                    if (ImGui::IsItemEdited()) {
                        pin.set_payload(value);
                    }
                }
            }
        )
    );

    Node* node = m_nodes.back().get();
    node->make_output_pin(pin_key_scalar, "out");
    return node;
}

auto Graph_window::make_unary_sink() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Result",
            [](Node&){},
            [](Node& node){
                ImGui::SetNextItemWidth(80.0f);
                ImGui::Text("%d", node.get_input_pins()[0].get_payload());
            }
        )
    );
    Node* node = m_nodes.back().get();
    node->make_input_pin(pin_key_scalar, "in");
    return node;
}

auto Graph_window::make_add() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Add",
            [](Node& node){
                const payload_t lhs    = node.get_input_pins()[0].get_payload();
                const payload_t rhs    = node.get_input_pins()[1].get_payload();
                const payload_t result = lhs + rhs;
                node.get_output_pins()[0].set_payload(result);
            },
            [](Node& node){
                const payload_t lhs = node.get_input_pins()[0].get_payload();
                const payload_t rhs = node.get_input_pins()[1].get_payload();
                const payload_t out = node.get_output_pins()[0].get_payload();
                ImGui::Text("%d + %d = %d", lhs, rhs, out);
            }
        )
    );
    Node* node = m_nodes.back().get();
    node->make_input_pin(pin_key_scalar, "lhs in");
    node->make_input_pin(pin_key_scalar, "rhs in");
    node->make_output_pin(pin_key_scalar, "out");
    return node;
}

auto Graph_window::make_sub() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Sub",
            [](Node& node){
                const payload_t lhs    = node.get_input_pins()[0].get_payload();
                const payload_t rhs    = node.get_input_pins()[1].get_payload();
                const payload_t result = lhs - rhs;
                node.get_output_pins()[0].set_payload(result);
            },
            [](Node& node){
                const payload_t lhs = node.get_input_pins()[0].get_payload();
                const payload_t rhs = node.get_input_pins()[1].get_payload();
                const payload_t out = node.get_output_pins()[0].get_payload();
                ImGui::Text("%d - %d = %d", lhs, rhs, out);
            }
        )
    );
    Node* node = m_nodes.back().get();
    node->make_input_pin(pin_key_scalar, "lhs in");
    node->make_input_pin(pin_key_scalar, "rhs in");
    node->make_output_pin(pin_key_scalar, "out");
    return node;
}



auto Graph_window::make_mul() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Mul",
            [](Node& node){
                const payload_t lhs    = node.get_input_pins()[0].get_payload();
                const payload_t rhs    = node.get_input_pins()[1].get_payload();
                const payload_t result = lhs * rhs;
                node.get_output_pins()[0].set_payload(result);
            },
            [](Node& node){
                const payload_t lhs = node.get_input_pins()[0].get_payload();
                const payload_t rhs = node.get_input_pins()[1].get_payload();
                const payload_t out = node.get_output_pins()[0].get_payload();
                ImGui::Text("%d * %d = %d", lhs, rhs, out);
            }
        )
    );
    Node* node = m_nodes.back().get();
    node->make_input_pin(pin_key_scalar, "lhs in");
    node->make_input_pin(pin_key_scalar, "rhs in");
    node->make_output_pin(pin_key_scalar, "out");
    return node;
}

auto Graph_window::make_div() -> Node*
{
    m_nodes.push_back(
        std::make_unique<Node>(
            "Div",
            [](Node& node){
                const payload_t lhs    = node.get_input_pins()[0].get_payload();
                const payload_t rhs    = node.get_input_pins()[1].get_payload();
                const payload_t result = (rhs != 0) ? lhs / rhs : lhs;
                node.get_output_pins()[0].set_payload(result);
            },
            [](Node& node){
                const payload_t lhs = node.get_input_pins()[0].get_payload();
                const payload_t rhs = node.get_input_pins()[1].get_payload();
                const payload_t out = node.get_output_pins()[0].get_payload();
                ImGui::Text("%d / %d = %d", lhs, rhs, out);
            }
        )
    );
    Node* node = m_nodes.back().get();
    node->make_input_pin(pin_key_scalar, "lhs in");
    node->make_input_pin(pin_key_scalar, "rhs in");
    node->make_output_pin(pin_key_scalar, "out");
    return node;
}


void Graph_window::imgui()
{
    if (!m_node_editor) {
        ax::NodeEditor::Config config;
        m_node_editor = std::make_unique<ax::NodeEditor::EditorContext>(nullptr);
    }  

    if (ImGui::Button("+")) { m_graph.register_node(make_add()); } ImGui::SameLine();
    if (ImGui::Button("-")) { m_graph.register_node(make_sub()); } ImGui::SameLine();
    if (ImGui::Button("*")) { m_graph.register_node(make_mul()); } ImGui::SameLine();
    if (ImGui::Button("/")) { m_graph.register_node(make_div()); } ImGui::SameLine();
    if (ImGui::Button("P")) { m_graph.register_node(make_unary_source()); } ImGui::SameLine();
    if (ImGui::Button("C")) { m_graph.register_node(make_unary_sink()); }

    m_graph.evaluate();

    m_node_editor->Begin("Graph", ImVec2{0.0f, 0.0f});

    for (Node* node : m_graph.get_nodes()) {
        log_graph_editor->info("Node {} {}", node->get_name(), node->get_id());

        ImGui::PushID(static_cast<int>(node->get_id()));
        ERHE_DEFER( ImGui::PopID(); );

        m_node_editor->BeginNode(node->get_id());

        ImVec2 pin_table_size{100.0f, 0.0f};

        // Input pins
        ImGui::BeginTable("##InputPins", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("InputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("InputLabel", ImGuiTableColumnFlags_None);
        for (const Pin& pin : node->get_input_pins()) {
            log_graph_editor->info("  Input Pin {} {}", pin.get_name(), pin.get_id());
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            m_node_editor->BeginPin(pin.get_handle(), ax::NodeEditor::PinKind::Input);
            ImGui::Bullet();
            m_node_editor->EndPin();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(pin.get_name().data());
        }
        ImGui::EndTable();

        ImGui::SameLine();

        // Content
        node->imgui();

        ImGui::SameLine();

        // Output pins
        ImGui::BeginTable("##OutputPins", 2, ImGuiTableFlags_None, pin_table_size);
        ImGui::TableSetupColumn("OutputLabel", ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn("OutputPin",   ImGuiTableColumnFlags_WidthFixed, 20.0f);
        for (const Pin& pin : node->get_output_pins()) {
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
            m_node_editor->BeginPin(pin.get_handle(), ax::NodeEditor::PinKind::Output);
            ImGui::Bullet();
            m_node_editor->EndPin();
        }
        ImGui::EndTable(); // Outputs

        m_node_editor->EndNode();
    }

    // Links
    for (const std::unique_ptr<Link>& link : m_graph.get_links()) {
        log_graph_editor->info(
            "  Link source {} {} sink {} {}",
            link->get_source()->get_name(), link->get_source()->get_id(),
            link->get_sink()->get_name(), link->get_sink()->get_id()
        );

        m_node_editor->Link(link->get_handle(), link->get_source()->get_handle(), link->get_sink()->get_handle());
    }

    if (m_node_editor->BeginCreate()) {
        ax::NodeEditor::PinId lhs_pin_handle;
        ax::NodeEditor::PinId rhs_pin_handle;
        if (m_node_editor->QueryNewLink(&lhs_pin_handle, &rhs_pin_handle)) {
            bool acceptable = false;
            if (rhs_pin_handle && lhs_pin_handle) {
                Pin*  lhs_pin     = lhs_pin_handle.AsPointer<Pin>();
                Pin*  rhs_pin     = rhs_pin_handle.AsPointer<Pin>();
                Pin*  source_pin  = lhs_pin->is_source() ? lhs_pin : rhs_pin->is_source() ? rhs_pin : nullptr;
                Pin*  sink_pin    = lhs_pin->is_sink  () ? lhs_pin : rhs_pin->is_sink  () ? rhs_pin : nullptr;
                Node* source_node = source_pin != nullptr ? source_pin->get_owner_node() : nullptr;
                Node* sink_node   = sink_pin   != nullptr ? sink_pin  ->get_owner_node() : nullptr;
                if ((source_pin != nullptr) && (sink_pin != nullptr) && (source_pin != sink_pin) && (source_node != sink_node)) {
                    acceptable = true;
                    if (m_node_editor->AcceptNewItem()) { // mouse released?
                        Link* link = m_graph.connect(source_pin, sink_pin);
                        if (link != nullptr) {
                            m_node_editor->Link(link->get_handle(), source_pin->get_handle(), sink_pin->get_handle());
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
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::unique_ptr<Node>& entry){
                    ax::NodeEditor::NodeId entry_node_id = entry->get_id();
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
                std::vector<std::unique_ptr<Link>>& links = m_graph.get_links();
                auto i = std::find_if(links.begin(), links.end(), [link_handle](const std::unique_ptr<Link>& entry){
                    ax::NodeEditor::LinkId entry_link_id = entry->get_id();
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
