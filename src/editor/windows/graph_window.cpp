#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "windows/graph_window.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor {

auto Link::get_id     () const -> int   { return m_id; }
auto Link::get_handle () const -> ax::NodeEditor::LinkId{ return ax::NodeEditor::LinkId{static_cast<std::size_t>(m_id)}; }
auto Link::get_source () const -> Pin*  { return m_source; }
auto Link::get_sink   () const -> Pin*  { return m_sink; }
auto Link::get_payload() const -> Payload { return m_source->get_payload(); }
void Link::set_payload(Payload value) { m_source->set_payload(value); }

auto Link::is_connected() const -> bool { return (m_source != nullptr) && (m_sink != nullptr); }

void Link::disconnect()
{
    m_source->remove_link(this);
    m_sink  ->remove_link(this);
    m_source = nullptr;
    m_sink = nullptr;
}

auto Pin::get_payload() -> Payload                         { 
    if (is_sink()) { // && pull
        Payload sum{};
        for (Link* link : m_links) {
            sum += link->get_source()->get_payload();
        }
        m_payload = sum;
    }
    return m_payload;
}

void Pin::set_payload(Payload value)                     { 
    m_payload = value;
    if (m_is_source) {
        return;
    }
    for (Link* link : m_links) {
        link->get_sink()->set_payload(value);
    }
}

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
            lhs.int_value[0] / rhs.int_value[0],
            lhs.int_value[1] / rhs.int_value[1],
            lhs.int_value[2] / rhs.int_value[2],
            lhs.int_value[3] / rhs.int_value[3]
        },
        .float_value = { 
            lhs.float_value[0] / rhs.float_value[0],
            lhs.float_value[1] / rhs.float_value[1],
            lhs.float_value[2] / rhs.float_value[2],
            lhs.float_value[3] / rhs.float_value[3]
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
    int_value  [0] /= rhs.int_value  [0];
    int_value  [1] /= rhs.int_value  [1];
    int_value  [2] /= rhs.int_value  [2];
    int_value  [3] /= rhs.int_value  [3];
    float_value[0] /= rhs.float_value[0];
    float_value[1] /= rhs.float_value[1];
    float_value[2] /= rhs.float_value[2];
    float_value[3] /= rhs.float_value[3];
    return *this;
}


//////////


auto Shader_graph_node::get_static_type() -> uint64_t
{
    return erhe::Item_type::shader_graph_node;
}

auto Shader_graph_node::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Shader_graph_node::get_type_name() const -> std::string_view
{
    return static_type_name;
}

Shader_graph_node::Shader_graph_node(const Shader_graph_node&) = default;
Shader_graph_node& Shader_graph_node::operator=(const Shader_graph_node&) = default;

Shader_graph_node::Shader_graph_node()
    : Item           {}
    , m_graph_node_id{make_graph_id()}
{
}

Shader_graph_node::Shader_graph_node(const std::string_view name)
    : Item           {name}
    , m_graph_node_id{make_graph_id()}
{
}

Shader_graph_node::~Shader_graph_node() noexcept
{
}

void Shader_graph_node::evaluate()
{
}

auto Shader_graph_node::shared_shader_graph_node_from_this() -> std::shared_ptr<Shader_graph_node>
{
    return std::static_pointer_cast<Shader_graph_node>(shared_from_this());
}


//////////


void Shader_graph_node::imgui() {
    ImGui::TextUnformatted(m_name.c_str());
}

void Shader_graph_node::make_input_pin(std::size_t key, std::string_view name)
{
    m_input_pins.emplace_back(std::move(Pin{this, true, key, name}));
}

void Shader_graph_node::make_output_pin(std::size_t key, std::string_view name)
{
    m_output_pins.emplace_back(std::move(Pin{this, false, key, name}));
}

void Graph::register_node(Shader_graph_node* node)
{
#if !defined(NDEBUG)
    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Shader_graph_node* entry) { return entry == node; });
    if (i != m_nodes.end()) {
        log_graph_editor->error("Shader_graph_node {} {} is already registered to Graph", node->get_name(), node->get_id());
        return;
    }
#endif
    m_nodes.push_back(node);
    log_graph_editor->trace("Registered Shader_graph_node {} {}", node->get_name(), node->get_id());
}

void Graph::unregister_node(Shader_graph_node* node)
{
    if (node == nullptr) {
        return;
    }

    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Shader_graph_node* entry) { return entry == node; });
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

    log_graph_editor->trace("Unregistered Shader_graph_node {} {}", node->get_name(), node->get_id());
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
    for (Shader_graph_node* node : m_nodes) {
        node->evaluate();
    }
}

auto Graph::get_host_name() const -> const char*
{
    return "Graph";
}

void Graph::sort()
{
    if (m_is_sorted) {
        return;
    }

    std::vector<Shader_graph_node*> unsorted_nodes = m_nodes;
    std::vector<Shader_graph_node*> sorted_nodes;

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
                            [&link](Shader_graph_node* entry) {
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
                log_graph_editor->error("Sort: Shader_graph_node {} {} is not in graph nodes", node->get_name(), node->get_id());
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
    m_is_sorted = true;
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

static constexpr std::size_t pin_key_todo = 1;

auto Graph_window::make_constant() -> Shader_graph_node*
{
    m_nodes.push_back(std::make_shared<Constant>());
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

    m_graph.evaluate();

    m_node_editor->Begin("Graph", ImVec2{0.0f, 0.0f});

    for (Shader_graph_node* node : m_graph.get_nodes()) {
        log_graph_editor->info("Shader_graph_node {} {}", node->get_name(), node->get_id());

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
                Pin*               lhs_pin     = lhs_pin_handle.AsPointer<Pin>();
                Pin*               rhs_pin     = rhs_pin_handle.AsPointer<Pin>();
                Pin*               source_pin  = lhs_pin->is_source() ? lhs_pin : rhs_pin->is_source() ? rhs_pin : nullptr;
                Pin*               sink_pin    = lhs_pin->is_sink  () ? lhs_pin : rhs_pin->is_sink  () ? rhs_pin : nullptr;
                Shader_graph_node* source_node = source_pin != nullptr ? source_pin->get_owner_node() : nullptr;
                Shader_graph_node* sink_node   = sink_pin   != nullptr ? sink_pin  ->get_owner_node() : nullptr;
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
                auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node_handle](const std::shared_ptr<Shader_graph_node>& entry){
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

Constant::Constant()
    : Shader_graph_node{"Constant"}
{
    make_output_pin(pin_key_todo, "out");
}

void Constant::evaluate()
{
    for (Pin& pin : get_output_pins()) {
        Payload value = pin.get_payload();
        for (Link* link : pin.get_links()) {
            link->get_sink()->set_payload(value);
        }
    }
}

void Constant::imgui()
{
    for (Pin& pin : get_output_pins()) {
        Payload value = pin.get_payload();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##", &value.int_value[0]);
        if (ImGui::IsItemEdited()) {
            pin.set_payload(value);
        }
    }
}

Add::Add()
    : Shader_graph_node{"Add"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Add::evaluate()
{
    const Payload lhs    = get_input_pins()[0].get_payload();
    const Payload rhs    = get_input_pins()[1].get_payload();
    const Payload result = lhs + rhs;
    get_output_pins()[0].set_payload(result);
}

void Add::imgui()
{
    const Payload lhs = get_input_pins()[0].get_payload();
    const Payload rhs = get_input_pins()[1].get_payload();
    const Payload out = get_output_pins()[0].get_payload();
    ImGui::Text("%d + %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

Sub::Sub()
    : Shader_graph_node{"Sub"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Sub::evaluate()
{
    const Payload lhs    = get_input_pins()[0].get_payload();
    const Payload rhs    = get_input_pins()[1].get_payload();
    const Payload result = lhs - rhs;
    get_output_pins()[0].set_payload(result);
}

void Sub::imgui()
{
    const Payload lhs = get_input_pins()[0].get_payload();
    const Payload rhs = get_input_pins()[1].get_payload();
    const Payload out = get_output_pins()[0].get_payload();
    ImGui::Text("%d - %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

Mul::Mul()
    : Shader_graph_node{"Mul"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Mul::evaluate()
{
    const Payload lhs    = get_input_pins()[0].get_payload();
    const Payload rhs    = get_input_pins()[1].get_payload();
    const Payload result = lhs * rhs;
    get_output_pins()[0].set_payload(result);
}

void Mul::imgui()
{
    const Payload lhs = get_input_pins()[0].get_payload();
    const Payload rhs = get_input_pins()[1].get_payload();
    const Payload out = get_output_pins()[0].get_payload();
    ImGui::Text("%d * %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

Div::Div()
    : Shader_graph_node{"Div"}
{
    make_input_pin(pin_key_todo, "A");
    make_input_pin(pin_key_todo, "B");
    make_output_pin(pin_key_todo, "out");
}

void Div::evaluate()
{
    const Payload lhs    = get_input_pins()[0].get_payload();
    const Payload rhs    = get_input_pins()[1].get_payload();
    const Payload result = lhs / rhs;
    get_output_pins()[0].set_payload(result);
}

void Div::imgui()
{
    const Payload lhs = get_input_pins()[0].get_payload();
    const Payload rhs = get_input_pins()[1].get_payload();
    const Payload out = get_output_pins()[0].get_payload();
    ImGui::Text("%d / %d = %d", lhs.int_value[0], rhs.int_value[0], out.int_value[0]); // TODO Handle format
}

Sheet_window::Sheet_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Sheet", "sheet"}
    , m_context                {editor_context}
{
    static_cast<void>(commands); // TODO Keeping in case we need to add commands here

    // Initialize tinyexpr variables
    const char col_char[8] = {'A','B','C','D','E','F','G','H'};
    const char row_char[8] = {'1','2','3','4','5','6','7','8'};
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            char* label = m_te_labels.at(row * 8 + col);
            label[0] = col_char[col];
            label[1] = row_char[row];
            label[2] = '\0';
            double& te_value = m_te_values.at(row * 8 + col);
            te_variable& te_variable = m_te_variables.at(row * 8 + col);
            te_variable.name = label;
            te_variable.address = &te_value;
            te_variable.type = TE_VARIABLE;
            te_variable.context = nullptr;
            te_expr*& te_expression = m_te_expressions.at(row * 8 + col);
            te_expression = nullptr;
        }
    }

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

void Sheet_window::on_message(Editor_message&)
{
    //// using namespace erhe::bit;
    //// if (test_any_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
    //// }
}

auto Sheet_window::at(int row, int col) -> std::string&
{
    return m_data.at(row * 8 + col);
}

void Sheet_window::imgui()
{
    bool dirty = false;

    if (ImGui::Button("Compute")) {
        dirty = true;
    }

    ImGui::SameLine();

    if (m_show_expression) {
        if (ImGui::Button("Show Values")) {
            m_show_expression = false;
        }
    } else if (ImGui::Button("Show Expressions")) {
        m_show_expression = true;
    }

    ImGui::BeginTable("sheet", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable);
    ImGui::TableSetupColumn("##rows", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("C", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("E", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("F", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("G", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("H", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableHeadersRow();
    for (int row = 0; row < 8; ++row) {
        ImGui::PushID(row);
        ERHE_DEFER( ImGui::PopID(); );
        ImGui::TableNextRow(ImGuiTableRowFlags_None);
        ImGui::TableNextColumn();
        ImGui::Text("%d", row + 1);
        for (int col = 0; col < 8; ++col) {
            ImGui::PushID(col);
            ERHE_DEFER( ImGui::PopID(); );
            ImGui::TableNextColumn();
            std::string& cell_string = at(row, col);
            double& te_value = m_te_values.at(row * 8 + col);
            //te_variable& te_variable = m_te_variables.at(row * 8 + col);
            te_expr*& te_expression = m_te_expressions.at(row * 8 + col);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (m_show_expression) {
                ImGui::InputText("##", &cell_string);
                if (ImGui::IsItemEdited()) {
                    dirty = true;
                    if (te_expression != nullptr) {
                        te_free(te_expression);
                    }
                    int error = 0;
                    te_expression = te_compile(cell_string.c_str(), m_te_variables.data(), static_cast<int>(m_te_variables.size()), &error);
                }
            } else {
                if (te_expression != nullptr) {
                    std::string value_string = fmt::format("{}", te_value);
                    ImGui::InputText("##", &value_string, ImGuiInputTextFlags_ReadOnly);
                } else {
                    std::string value_string{};
                    ImGui::InputText("##", &value_string, ImGuiInputTextFlags_ReadOnly);
                }
            }
        }
    }
    ImGui::EndTable();

    if (dirty) {
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col) {
                double& te_value = m_te_values.at(row * 8 + col);
                //te_variable& te_variable = m_te_variables.at(row * 8 + col);
                te_expr*& te_expression = m_te_expressions.at(row * 8 + col);
                if (te_expression != nullptr) {
                    te_value = te_eval(te_expression);
                }
            }
        }
        dirty = false;
    }
}

} // namespace editor
