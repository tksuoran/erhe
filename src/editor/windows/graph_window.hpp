#pragma once

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}
namespace ax::NodeEditor {
    class EditorContext;
}

namespace editor {

class Editor_context;

class Node;
class Pin;
class Link;
class Graph;

using payload_t = int;

[[nodiscard]] inline auto make_id() -> int
{
    static std::atomic<int> counter{1};
    return counter++;
}

class Link
{
public:
    Link() : m_id{make_id()} {}
    Link(const Link& other) = delete;
    Link(Link&& old) = default;
    Link& operator=(const Link& other) = delete;
    Link& operator=(Link&& old) = default;
    Link(Pin* source, Pin* sink)
        : m_id    {make_id()}
        , m_source{source}
        , m_sink  {sink}
    {
    }

    [[nodiscard]] auto get_id      () const -> int;
    [[nodiscard]] auto get_handle  () const -> ax::NodeEditor::LinkId;
    [[nodiscard]] auto get_source  () const -> Pin*;
    [[nodiscard]] auto get_sink    () const -> Pin*;
    [[nodiscard]] auto get_payload () const -> payload_t;
    [[nodiscard]] void set_payload (payload_t value);
    [[nodiscard]] auto is_connected() const -> bool;
    void disconnect();

private:
    int  m_id;
    Pin* m_source {nullptr};
    Pin* m_sink   {nullptr};
};

class Node
{
public:
    Node(
        std::string_view           name,
        std::function<void(Node&)> evaluate = {},
        std::function<void(Node&)> imgui = {}
    )
        : m_id      {make_id()}
        , m_name    {name}
        , m_evaluate{evaluate}
        , m_imgui   {imgui}
    {
    }

    [[nodiscard]] auto get_id         () const -> int                     { return m_id; }
    [[nodiscard]] auto get_name       () const -> const std::string_view  { return m_name; }
    [[nodiscard]] auto get_input_pins () const -> const std::vector<Pin>& { return m_input_pins; }
    [[nodiscard]] auto get_input_pins ()       -> std::vector<Pin>&       { return m_input_pins; }
    [[nodiscard]] auto get_output_pins() const -> const std::vector<Pin>& { return m_output_pins; }
    [[nodiscard]] auto get_output_pins()       -> std::vector<Pin>&       { return m_output_pins; }

    void evaluate();
    void imgui();

    void make_input_pin (std::size_t key, std::string_view name);
    void make_output_pin(std::size_t key, std::string_view name);

private:
    int                        m_id;
    std::string                m_name;
    std::function<void(Node&)> m_evaluate;
    std::function<void(Node&)> m_imgui;
    std::vector<Pin>           m_input_pins;
    std::vector<Pin>           m_output_pins;
};

class Pin
{
private:
    friend class Node;
    Pin(Node* owner_node, bool is_source, std::size_t key, std::string_view name)
        : m_id        {make_id()}
        , m_key       {key}
        , m_owner_node{owner_node}
        , m_is_source {is_source}
        , m_name      {name}
    {
    }

public:
    [[nodiscard]] auto is_sink       () const -> bool                      { return m_is_source; }
    [[nodiscard]] auto is_source     () const -> bool                      { return !m_is_source; }
    [[nodiscard]] auto get_id        () const -> int                       { return m_id; }
    [[nodiscard]] auto get_handle    () const -> ax::NodeEditor::PinId     { return ax::NodeEditor::PinId{this}; }
    [[nodiscard]] auto get_key       () const -> std::size_t               { return m_key; }
    [[nodiscard]] auto get_name      () const -> const std::string_view    { return m_name; }
    [[nodiscard]] void add_link      (Link* link)                          { m_links.push_back(link); }
    [[nodiscard]] void remove_link   (Link* link)                          { auto i = std::find_if(m_links.begin(), m_links.end(), [link](Link* entry) { return entry == link; }); m_links.erase(i);}
    [[nodiscard]] auto get_links     () const -> const std::vector<Link*>& { return m_links; }
    [[nodiscard]] auto get_links     ()       -> std::vector<Link*>&       { return m_links; }
    [[nodiscard]] auto get_owner_node() const -> Node*                     { return m_owner_node; }
    [[nodiscard]] auto get_payload   () -> payload_t                       { 
        if (is_sink()) { // && pull
            payload_t sum{};
            for (Link* link : m_links) {
                sum += link->get_source()->get_payload();
            }
            m_payload = sum;
        }
        return m_payload;
    }
    [[nodiscard]] void set_payload   (payload_t value)                     { 
        m_payload = value;
        if (m_is_source) {
            return;
        }
        for (Link* link : m_links) {
            link->get_sink()->set_payload(value);
        }
    }

private:
    int                m_id;
    std::size_t        m_key;
    Node*              m_owner_node{nullptr};
    bool               m_is_source{true};
    std::string        m_name;
    std::vector<Link*> m_links;
    payload_t          m_payload {};
};

class Graph
{
public:
    void register_node  (Node* node);
    void unregister_node(Node* node);
    auto connect        (Pin* source_pin, Pin* sink_pin) -> Link*;
    void disconnect     (Link* link);
    void sort           ();
    void evaluate       ();

    [[nodiscard]] auto get_nodes() const -> const std::vector<Node*>&           { return m_nodes; }
    [[nodiscard]] auto get_nodes()       -> std::vector<Node*>&                 { return m_nodes; }
    [[nodiscard]] auto get_links()       -> std::vector<std::unique_ptr<Link>>& { return m_links; }

    std::vector<Node*> m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;
};

class Graph_window : public erhe::imgui::Imgui_window
{
public:
    Graph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context);
    ~Graph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    [[nodiscard]] auto make_unary_source() -> Node*;
    [[nodiscard]] auto make_unary_sink  () -> Node*;
    [[nodiscard]] auto make_add         () -> Node*;
    [[nodiscard]] auto make_sub         () -> Node*;
    [[nodiscard]] auto make_mul         () -> Node*;
    [[nodiscard]] auto make_div         () -> Node*;

private:
    Editor_context&                                m_context;
    Graph                                          m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;

    std::vector<std::unique_ptr<Node>> m_nodes;
};

} // namespace editor
