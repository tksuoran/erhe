#pragma once

#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

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

    // Implements erhe::property::Dependency_object: inherits-flagged
    // properties flow down the parent / child tree.
    [[nodiscard]] auto get_inheritance_parent() const -> const erhe::property::Dependency_object* override;
    void for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback) override;

    // Computed (doc/property-system.md D26): get_child_count(), pushed
    // to expressions from handle_add_child / handle_remove_child.
    static const erhe::property::Property<int> child_count_property;

    // Overrides Item_base: an item in a hierarchy is named by its path
    // (get_path()); a root with no parent has an empty path and is named
    // by its name, as items outside a hierarchy are.
    [[nodiscard]] auto get_reference_path() const -> std::string override;

    // Namespace path (doc/usd-compatibility-plan.md M1): the names of this
    // item and of its ancestors below the root, outermost first, separated
    // by '/'. The root's own name is not part of the path, so a child of
    // the root is named by its name alone and a deeper item by
    // "Parent/Child"; the root itself has an empty path. This is the form
    // the ERHE_scene library_folders entries store for content-library
    // folders, so one form addresses scene nodes and library folders
    // alike. Built on demand - never call it per frame.
    [[nodiscard]] auto get_path() const -> std::string;

    // Sibling-unique names (doc/usd-compatibility-plan.md M2): the name
    // `wanted_name` can be attached to `parent` with, which is `wanted_name`
    // itself when no child of `parent` other than `exclude` holds it, and
    // otherwise the first free `<base>_<number>` counting from 1. The base is
    // `wanted_name` without a trailing `_<digits>`, so a colliding `Cube_1`
    // yields `Cube_2` rather than `Cube_1_1`; a name that is nothing but
    // `_<digits>` is its own base. A null `parent` imposes no namespace, so
    // `wanted_name` comes back unchanged.
    [[nodiscard]] static auto make_sibling_unique_name(
        const Hierarchy* parent,
        std::string_view wanted_name,
        const Hierarchy* exclude
    ) -> std::string;

    // Overrides Item_base: an item in a hierarchy shares one namespace with
    // its siblings, so a name held by another child of the same parent is
    // refused.
    [[nodiscard]] auto is_name_available(std::string_view name) const -> bool override;

    // Sibling-unique names: called by handle_add_child when the name this
    // item is attached with is already held by a sibling. The base renames
    // this item; Content_library_node also renames the item an owning entry
    // wraps, because that item's name is the one the user sees.
    virtual void handle_sibling_unique_rename(const std::string& unique_name);

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

// The item `path` names below `root`, in the form get_path() returns: an
// empty path is the root itself, and every other path is a sequence of
// child names separated by '/', each naming a child of the item the
// previous name reached. nullptr when a name matches no child.
[[nodiscard]] auto find_by_path(Hierarchy& root, std::string_view path) -> Hierarchy*;

} // namespace erhe
