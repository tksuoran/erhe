#pragma once

#include "erhe_item/item.hpp"

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace erhe {

class Hierarchy : public Item<Item_base, Item_base, Hierarchy>
{
public:
    Hierarchy();
    ~Hierarchy() noexcept override;

    explicit Hierarchy(const Hierarchy& src);
    Hierarchy& operator=(const Hierarchy& src);
    explicit Hierarchy(std::string_view name);

    Hierarchy(const Hierarchy& src, for_clone);

    [[nodiscard]] auto shared_hierarchy_from_this() -> std::shared_ptr<Hierarchy>;

    // Overrides Item_base
    static constexpr std::string_view static_type_name{"Hierarchy"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return 0; }

    virtual void set_parent          (const std::shared_ptr<Hierarchy>& parent);
    virtual void set_parent          (const std::shared_ptr<Hierarchy>& parent, std::size_t position);
    virtual void handle_add_child    (const std::shared_ptr<Hierarchy>& child_node, std::size_t position);
    virtual void handle_remove_child (Hierarchy* child_node);
    virtual void handle_parent_update(Hierarchy* old_parent, Hierarchy* new_parent);

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

    void remove                         ();
    void recursive_remove               ();
    void remove_all_children_recursively();
    void set_parent                     (Hierarchy* parent);
    void set_parent                     (Hierarchy* parent, std::size_t position);
    void adopt_orphan_children          ();
    void set_depth_recursive            (std::size_t depth);
    void hierarchy_sanity_check         (bool destruction_in_progress = false) const;
    void sanity_check_root_path         (const Hierarchy* node) const;
    void trace                          ();

    template <std::invocable<Hierarchy&> F>
    void for_each(const F& fun)
    {
        if (!fun(*this)) {
            return;
        }
        for (const auto& child : m_children) {
            child->for_each(fun);
        }
    }

    template <typename T, std::invocable<T&> F>
    auto for_each(const F& callback) -> bool
    {
        T* item = dynamic_cast<T*>(this);
        if (item != nullptr) {
            if (!callback(*item)) {
                return false;
            }
        }

        for (const std::shared_ptr<Hierarchy>& child : m_children) {
            if (!child->template for_each<T>(callback)) {
                return false;
            }
        }
        return true;
    }

    template <typename T, std::invocable<T&> F>
    auto for_each_child(const F& callback) -> bool
    {
        for (const std::shared_ptr<Hierarchy>& child : m_children) {
            if (!child->template for_each<T>(callback)) {
                return false;
            }
        }
        return true;
    }

    template <typename T, std::invocable<const T&> F>
    auto for_each_const(const F& callback) const -> bool
    {
        const T* item = dynamic_cast<const T*>(this);
        if (item != nullptr) {
            if (!callback(*item)) {
                return false;
            }
        }

        for (const std::shared_ptr<Hierarchy>& child : m_children) {
            if (!child->template for_each_const<T>(callback)) {
                return false;
            }
        }
        return true;
    }

    template <typename T, std::invocable<const T&> F>
    auto for_each_child_const(const F& callback) const -> bool
    {
        for (const std::shared_ptr<Hierarchy>& child : m_children) {
            if (!child->template for_each_const<T>(callback)) {
                return false;
            }
        }
        return true;
    }

protected:
    std::weak_ptr<Hierarchy>                m_parent{};
    std::vector<std::shared_ptr<Hierarchy>> m_children;
    std::size_t                             m_depth {0};
};

} // namespace erhe
