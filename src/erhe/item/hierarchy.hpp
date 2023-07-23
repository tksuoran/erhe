#pragma once

#include "erhe/item/item.hpp"

#include "erhe/toolkit/unique_id.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe
{

class Hierarchy
    : public Item
{
public:
    Hierarchy();
    explicit Hierarchy(std::size_t id);
    Hierarchy(const std::string_view name, std::size_t id);

    [[nodiscard]] auto shared_hierarchy_from_this() -> std::shared_ptr<Hierarchy>;

    // Overrides Item
    static constexpr std::string_view static_type_name{"Hierarchy"};
    [[nodiscard]] static auto get_static_type() -> uint64_t{ return 0; }
    [[nodiscard]] auto get_type     () const -> uint64_t         override { return get_static_type(); }
    [[nodiscard]] auto get_type_name() const -> std::string_view override { return static_type_name; }

    virtual void set_parent          (const std::shared_ptr<Hierarchy>& parent, std::size_t position = 0)    ;
    virtual void handle_add_child    (const std::shared_ptr<Hierarchy>& child_node, std::size_t position = 0);
    virtual void handle_remove_child (Hierarchy* child_node)                                                 ;
    virtual void handle_parent_update(Hierarchy* old_parent, Hierarchy* new_parent)                     ;

    [[nodiscard]] auto get_parent          () const -> std::weak_ptr<Hierarchy>;
    [[nodiscard]] auto get_depth           () const -> size_t;
    [[nodiscard]] auto get_children        () const -> const std::vector<std::shared_ptr<Hierarchy>>&;
    [[nodiscard]] auto get_mutable_children() -> std::vector<std::shared_ptr<Hierarchy>>&;
    [[nodiscard]] auto get_root            () -> std::weak_ptr<Hierarchy>;
    [[nodiscard]] auto get_child_count     () const -> std::size_t;
    [[nodiscard]] auto get_child_count     (const Item_filter& filter) const -> std::size_t;
    [[nodiscard]] auto get_index_in_parent () const -> std::size_t;
    [[nodiscard]] auto get_index_of_child  (const Hierarchy* child) const -> std::optional<std::size_t>;
    [[nodiscard]] auto is_ancestor         (const Hierarchy* ancestor_candidate) const -> bool;

    void remove                ();
    void recursive_remove      ();
    void set_parent            (Hierarchy* parent, std::size_t position = 0);
    void set_depth_recursive   (std::size_t depth);
    void hierarchy_sanity_check() const;
    void sanity_check_root_path(const Hierarchy* node) const;
    void trace                 ();

protected:
    std::weak_ptr<Hierarchy>                m_parent{};
    std::vector<std::shared_ptr<Hierarchy>> m_children;
    std::size_t                             m_depth {0};
};

} // namespace erhe
