#pragma once

#include "erhe_item/item.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graph {

class Pin;
class Graph;

class Node : public erhe::Item<erhe::Item_base, erhe::Item_base, Node, erhe::Item_kind::clone_using_copy_constructor>
{
public:
    Node();
    Node(std::string_view name);
    explicit Node(const Node&);
    Node& operator=(const Node&);

    ~Node() noexcept override;

    [[nodiscard]] auto node_from_this() -> std::shared_ptr<Node>;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    [[nodiscard]] auto get_graph_id   () const -> int;
    [[nodiscard]] auto get_name       () const -> const std::string_view;
    [[nodiscard]] auto get_input_pins () const -> const std::vector<Pin>&;
    [[nodiscard]] auto get_input_pins ()       -> std::vector<Pin>&;
    [[nodiscard]] auto get_output_pins() const -> const std::vector<Pin>&;
    [[nodiscard]] auto get_output_pins()       -> std::vector<Pin>&;

    void base_make_input_pin (std::size_t key, std::string_view name);
    void base_make_output_pin(std::size_t key, std::string_view name);

protected:
    int              m_graph_node_id;
    std::string      m_name;
    std::vector<Pin> m_input_pins;
    std::vector<Pin> m_output_pins;
};


} // namespace editor
