#pragma once

#include "erhe_graph/pin.hpp"
#include "erhe_item/item.hpp"

#include <etl/vector.h>

#include <memory>
#include <string_view>

namespace erhe::graph {

class Graph;

// Maximum number of input or output pins per node.
// Pins are stored in etl::vector for element address stability
// (Links store raw Pin pointers that must not be invalidated).
constexpr std::size_t max_pin_count = 16;

class Node : public erhe::Item<erhe::Item_base, erhe::Item_base, Node, erhe::Item_kind::clone_using_copy_constructor>
{
public:
    Node();
    explicit Node(std::string_view name);
    explicit Node(const Node&);
    Node& operator=(const Node&);

    ~Node() noexcept override;

    [[nodiscard]] auto node_from_this() -> std::shared_ptr<Node>;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Node"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::graph_node; }

    [[nodiscard]] auto get_graph_id   () const -> int;
    [[nodiscard]] auto get_input_pins () const -> const etl::vector<Pin, max_pin_count>&;
    [[nodiscard]] auto get_input_pins ()       -> etl::vector<Pin, max_pin_count>&;
    [[nodiscard]] auto get_output_pins() const -> const etl::vector<Pin, max_pin_count>&;
    [[nodiscard]] auto get_output_pins()       -> etl::vector<Pin, max_pin_count>&;

    void base_make_input_pin (std::size_t key, std::string_view name);
    void base_make_output_pin(std::size_t key, std::string_view name);

protected:
    int                              m_graph_node_id;
    etl::vector<Pin, max_pin_count>  m_input_pins;
    etl::vector<Pin, max_pin_count>  m_output_pins;
};

}
