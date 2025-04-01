#pragma once

#include "erhe_commands/command.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_node_editor.h"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_host.hpp"

#include "tinyexpr/tinyexpr.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::commands {
    class Commands;
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

class Shader_graph_node;
class Pin;
class Link;
class Graph;

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

[[nodiscard]] inline auto make_graph_id() -> int
{
    static std::atomic<int> counter{1};
    return counter++;
}

class Link
{
public:
    Link() : m_id{make_graph_id()} {}
    Link(const Link& other) = delete;
    Link(Link&& old) = default;
    Link& operator=(const Link& other) = delete;
    Link& operator=(Link&& old) = default;
    Link(Pin* source, Pin* sink)
        : m_id    {make_graph_id()}
        , m_source{source}
        , m_sink  {sink}
    {
    }

    [[nodiscard]] auto get_id      () const -> int;
    [[nodiscard]] auto get_handle  () const -> ax::NodeEditor::LinkId;
    [[nodiscard]] auto get_source  () const -> Pin*;
    [[nodiscard]] auto get_sink    () const -> Pin*;
    [[nodiscard]] auto get_payload () const -> Payload;
                  void set_payload (Payload value);
    [[nodiscard]] auto is_connected() const -> bool;
    void disconnect();

private:
    int  m_id;
    Pin* m_source {nullptr};
    Pin* m_sink   {nullptr};
};

class Shader_graph_node : public erhe::Item<erhe::Item_base, erhe::Item_base, Shader_graph_node, erhe::Item_kind::clone_using_copy_constructor>
{
public:
    Shader_graph_node();
    Shader_graph_node(std::string_view name);
    explicit Shader_graph_node(const Shader_graph_node&);
    Shader_graph_node& operator=(const Shader_graph_node&);

    ~Shader_graph_node() noexcept override;

    [[nodiscard]] auto shared_shader_graph_node_from_this() -> std::shared_ptr<Shader_graph_node>;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Shader_graph_node"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    [[nodiscard]] auto get_graph_id   () const -> int                     { return m_graph_node_id; }
    [[nodiscard]] auto get_name       () const -> const std::string_view  { return m_name; }
    [[nodiscard]] auto get_input_pins () const -> const std::vector<Pin>& { return m_input_pins; }
    [[nodiscard]] auto get_input_pins ()       -> std::vector<Pin>&       { return m_input_pins; }
    [[nodiscard]] auto get_output_pins() const -> const std::vector<Pin>& { return m_output_pins; }
    [[nodiscard]] auto get_output_pins()       -> std::vector<Pin>&       { return m_output_pins; }

    virtual void evaluate();
    virtual void imgui();

    void make_input_pin (std::size_t key, std::string_view name);
    void make_output_pin(std::size_t key, std::string_view name);

private:
    int              m_graph_node_id;
    std::string      m_name;
    std::vector<Pin> m_input_pins;
    std::vector<Pin> m_output_pins;
};

class Pin
{
private:
    friend class Shader_graph_node;
    Pin(Shader_graph_node* owner_node, bool is_source, std::size_t key, std::string_view name)
        : m_id        {make_graph_id()}
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
                  void add_link      (Link* link)                          { m_links.push_back(link); }
                  void remove_link   (Link* link)                          { auto i = std::find_if(m_links.begin(), m_links.end(), [link](Link* entry) { return entry == link; }); m_links.erase(i);}
    [[nodiscard]] auto get_links     () const -> const std::vector<Link*>& { return m_links; }
    [[nodiscard]] auto get_links     ()       -> std::vector<Link*>&       { return m_links; }
    [[nodiscard]] auto get_owner_node() const -> Shader_graph_node*        { return m_owner_node; }
    [[nodiscard]] auto get_payload   () -> Payload;
    void               set_payload   (Payload value);

private:
    int                m_id;
    std::size_t        m_key;
    Shader_graph_node* m_owner_node{nullptr};
    bool               m_is_source{true};
    std::string        m_name;
    std::vector<Link*> m_links;
    Payload            m_payload {};
};

class Constant : public Shader_graph_node
{
public:
    Constant();

    void evaluate() override;
    void imgui   () override;
};

class Add : public Shader_graph_node
{
public:
    Add();
    void evaluate() override;
    void imgui() override;
};

class Sub : public Shader_graph_node
{
public:
    Sub();
    void evaluate() override;
    void imgui() override;
};

class Mul : public Shader_graph_node
{
public:
    Mul();
    void evaluate() override;
    void imgui() override;
};

class Div : public Shader_graph_node
{
public:
    Div();
    void evaluate() override;
    void imgui() override;
};


class Graph : public erhe::Item_host
{
public:
    // Implements Item_host
    auto get_host_name() const -> const char* override;

    void register_node  (Shader_graph_node* node);
    void unregister_node(Shader_graph_node* node);
    auto connect        (Pin* source_pin, Pin* sink_pin) -> Link*;
    void disconnect     (Link* link);
    void sort           ();
    void evaluate       ();

    [[nodiscard]] auto get_nodes() const -> const std::vector<Shader_graph_node*>& { return m_nodes; }
    [[nodiscard]] auto get_nodes()       -> std::vector<Shader_graph_node*>&       { return m_nodes; }
    [[nodiscard]] auto get_links()       -> std::vector<std::unique_ptr<Link>>&    { return m_links; }

    std::vector<Shader_graph_node*>    m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;
    bool                               m_is_sorted{false};
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
    auto make_add     () -> Shader_graph_node*;
    auto make_sub     () -> Shader_graph_node*;
    auto make_mul     () -> Shader_graph_node*;
    auto make_div     () -> Shader_graph_node*;

    void on_message(Editor_message& message);

    Editor_context&                                m_context;
    Graph                                          m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;

    std::vector<std::shared_ptr<Shader_graph_node>> m_nodes;
};

class Sheet_window : public erhe::imgui::Imgui_window
{
public:
    Sheet_window(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void on_message(Editor_message& message);

    auto at(int row, int col) -> std::string&;

    Editor_context& m_context;

    //std::array<Payload, 64> m_data;
    bool                        m_show_expression{false};
    std::array<std::string, 64> m_data;
    std::array<char[3], 64>     m_te_labels;
    std::array<double, 64>      m_te_values;
    std::array<te_variable, 64> m_te_variables;
    std::array<te_expr*, 64>    m_te_expressions;
};

} // namespace editor
