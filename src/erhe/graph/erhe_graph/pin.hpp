#pragma once

#include "erhe_item/item.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace erhe::graph {

class Node;
class Pin;
class Link;

class Pin
{
private:
    friend class Node;
    Pin(Node* owner_node, std::size_t slot, bool is_source, std::size_t key, std::string_view name);

public:
    virtual ~Pin() noexcept;

    [[nodiscard]] auto is_sink       () const -> bool;
    [[nodiscard]] auto is_source     () const -> bool;
    [[nodiscard]] auto get_id        () const -> int;
    [[nodiscard]] auto get_key       () const -> std::size_t;
    [[nodiscard]] auto get_name      () const -> const std::string_view;
                  void add_link      (Link* link);
                  void remove_link   (Link* link);
    [[nodiscard]] auto get_links     () const -> const std::vector<Link*>&;
    [[nodiscard]] auto get_links     ()       -> std::vector<Link*>&;
    [[nodiscard]] auto get_owner_node() const -> Node*;
    [[nodiscard]] auto get_slot      () const -> std::size_t;

    // Whether this (input) pin accepts multiple simultaneous links, with the
    // payload accumulating over them (Blender-style multi-input socket, e.g.
    // the geometry graph's Join node). Editors treat single-link pins (the
    // default) as replace-on-connect: connecting over an existing link first
    // drops the old one.
    [[nodiscard]] auto allows_multiple_links() const -> bool;
                  void set_multi_link       (bool multi_link);

protected:
    int                m_id;
    std::size_t        m_key;
    Node*              m_owner_node{nullptr};
    std::size_t        m_slot;
    bool               m_is_source {true};
    bool               m_multi_link{false};
    std::string        m_name;
    std::vector<Link*> m_links;
};

} // namespace erhe::graph
