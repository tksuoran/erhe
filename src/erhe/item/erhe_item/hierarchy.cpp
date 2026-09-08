#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>
#include <string>

namespace erhe {

using namespace erhe::item;

const erhe::property::Property<int> Hierarchy::child_count_property = erhe::property::Property<int>::register_computed(
    "child_count", Hierarchy::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return static_cast<int>(static_cast<const Hierarchy&>(object).get_child_count());
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.tooltip = "Number of direct children (computed)", .label = "Child Count"}
    }
);

Hierarchy::Hierarchy()           = default;
Hierarchy::~Hierarchy() noexcept = default;

Hierarchy::Hierarchy(const Hierarchy& src)
    : Item{src} // m_parent is not copied from other
{
    // Don't use set_parent() here: `this` is not yet managed by a shared_ptr,
    // so shared_from_this() would throw. Also set_parent() would double-add
    // children (we already push_back below). Wire members directly instead.
    m_children.reserve(src.m_children.size());
    for (const auto& src_child : src.m_children) {
        std::shared_ptr<erhe::Item_base> base      = src_child->clone();
        std::shared_ptr<erhe::Hierarchy> dst_child = std::dynamic_pointer_cast<erhe::Hierarchy>(base);
        if (dst_child) {
            dst_child->m_parent = {}; // can't point to `this` yet - no shared_ptr exists
            dst_child->set_depth_recursive(m_depth + 1);
            m_children.push_back(dst_child);
        }
    }
}

Hierarchy::Hierarchy(const Hierarchy& src, for_clone) : Hierarchy{src} {}

void Hierarchy::adopt_orphan_children()
{
    auto self = shared_hierarchy_from_this();
    for (const auto& child : m_children) {
        if (child->m_parent.expired()) {
            child->m_parent = self;
        }
        child->adopt_orphan_children();
    }
}

Hierarchy& Hierarchy::operator=(const Hierarchy& src)
{
    if (this == &src) {
        return *this;
    }

    Item::operator=(src);

    // Detach from old parent before resetting m_parent
    std::shared_ptr<Hierarchy> old_parent = m_parent.lock();
    if (old_parent) {
        old_parent->handle_remove_child(this);
    }

    m_children.clear();
    m_children.reserve(src.m_children.size());
    m_parent.reset();
    m_depth = 0;

    auto self = shared_hierarchy_from_this();
    for (const auto& src_child : src.m_children) {
        auto dst_child = std::dynamic_pointer_cast<erhe::Hierarchy>(src_child->clone());
        if (dst_child) {
            dst_child->m_parent = self;
            dst_child->set_depth_recursive(m_depth + 1);
            dst_child->adopt_orphan_children();
            m_children.push_back(dst_child);
        }
    }
    return *this;
}

Hierarchy::Hierarchy(const std::string_view name)
    : Item<Item_base, Item_base, Hierarchy>{name}
{
}

auto Hierarchy::shared_hierarchy_from_this() -> std::shared_ptr<Hierarchy>
{
    return std::static_pointer_cast<Hierarchy>(shared_from_this());
}

void Hierarchy::remove()
{
    log->trace("Hierarchy::remove() '{}' depth = {} child count = {}", describe(), get_depth(), m_children.size());

    hierarchy_sanity_check();

    std::shared_ptr<Hierarchy> parent = m_parent.lock();
    while (!m_children.empty()) {
        m_children.back()->set_parent(parent);
    }

    set_parent({});

    hierarchy_sanity_check();
}

void Hierarchy::recursive_remove()
{
    while (!m_children.empty()) {
        m_children.back()->recursive_remove();
    }

    set_parent({});
}

void Hierarchy::remove_all_children_recursively()
{
    while (!m_children.empty()) {
        m_children.back()->recursive_remove();
    }
}

auto Hierarchy::get_child_count() const -> std::size_t
{
    return m_children.size();
}

auto Hierarchy::get_child_count(const Item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& child : m_children) {
        if (filter(child->get_flag_bits())) {
            ++result;
        }
    }
    return result;
}

auto Hierarchy::get_index_in_parent() const -> std::size_t
{
    const auto& current_parent = get_parent().lock();
    if (current_parent) {
        const auto index = current_parent->get_index_of_child(this);
        return index.has_value() ? index.value() : 0;
    }
    return 0;
}

auto Hierarchy::get_index_of_child(const Hierarchy* child) const -> std::optional<std::size_t>
{
    for (std::size_t i = 0, end = m_children.size(); i < end; ++i) {
        if (m_children[i].get() == child) {
            return i;
        }
    }
    return {};
}

auto Hierarchy::is_ancestor(const Hierarchy* ancestor_candidate) const -> bool
{
    const auto& current_parent = get_parent().lock();
    if (!current_parent) {
        return false;
    }
    if (current_parent.get() == ancestor_candidate) {
        return true;
    }
    return current_parent->is_ancestor(ancestor_candidate);
}

void Hierarchy::set_parent(const std::shared_ptr<Hierarchy>& parent)
{
    set_parent(parent, std::numeric_limits<std::size_t>::max());
}

void Hierarchy::set_parent(const std::shared_ptr<Hierarchy>& new_parent_, const std::size_t position)
{
    ERHE_VERIFY(new_parent_.get() != this);
    std::shared_ptr<Hierarchy> old_parent_shared = m_parent.lock();
    Hierarchy*                 old_parent         = old_parent_shared.get();
    Hierarchy*                 new_parent         = new_parent_.get();

    if (old_parent == new_parent) {
        m_parent = new_parent_;
        return;
    }

    // Inherited property values of this subtree may change with the parent;
    // capture them while the old parent is still in place so the change
    // notifications after the move carry the right old values.
    const erhe::property::Inheritance_snapshot inheritance_snapshot = capture_inheritance_snapshot();
    m_parent = new_parent_;

    log->trace(
        "Parent change for '{}' old parent = '{}', new parent = '{}'",
        describe(),
        (old_parent != nullptr) ? old_parent->describe() : "none",
        (new_parent != nullptr) ? new_parent->describe() : "none"
    );

    // Keep this alive until end of scope.
    // - We need to keep this alive while being removed from old parent before being added to new parent
    // - Also in case new_parent is empty, for any other access to this
    auto shared_this = std::static_pointer_cast<Hierarchy>(weak_from_this().lock());

    // The copy constructor cannot set children's m_parent (shared_from_this()
    // is not available during construction). Fix up the back-links now that
    // this object is managed by a shared_ptr.
    adopt_orphan_children();

    if (old_parent) {
        old_parent->handle_remove_child(this);
    }

    if (new_parent) {
        new_parent->handle_add_child(shared_this, position);
    } else {
        log->trace("Now orphan: '{}'", describe());
    }

    set_depth_recursive(new_parent ? new_parent->get_depth() + 1 : 0);
    handle_parent_update(old_parent, new_parent);
    hierarchy_sanity_check();
    apply_inheritance_snapshot(inheritance_snapshot);
    // The effective active state (X2) is not an inherited property value:
    // it is the derived Item_flags::active bit, so the parent change has to
    // recompute it for this item and, when it moved, for the subtree.
    rederive_active_flag_bits();
}

auto Hierarchy::get_inheritance_parent() const -> const erhe::property::Dependency_object*
{
    const std::shared_ptr<Hierarchy> parent = m_parent.lock();
    if (parent) {
        return parent.get();
    }
    // No parent of its own: the item inherits from the container that holds
    // it, which is what Item_base answers. A content-library item is held by
    // its Content_library_node entry and inherits the folder's values through
    // it (doc/content-library-folders.md D1).
    return Item_base::get_inheritance_parent();
}

void Hierarchy::for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback)
{
    for (const std::shared_ptr<Hierarchy>& child : m_children) {
        if (child) {
            callback(*child);
        }
    }
}

void Hierarchy::set_parent(Hierarchy* parent)
{
    set_parent(parent, std::numeric_limits<std::size_t>::max());
}

void Hierarchy::set_parent(Hierarchy* const new_parent, const std::size_t position)
{
    ERHE_VERIFY(new_parent != this);
    if (new_parent != nullptr) {
        set_parent(new_parent->shared_hierarchy_from_this(), position   );
    } else {
        set_parent(std::shared_ptr<Hierarchy>{}, position);
    }
}

void Hierarchy::handle_add_child(const std::shared_ptr<Hierarchy>& child, std::size_t position)
{
    ERHE_VERIFY(child);
    ERHE_VERIFY(child.get() != this);

#ifndef NDEBUG
    const auto i = std::find(m_children.begin(), m_children.end(), child);
    if (i != m_children.end()) {
        log->error("{} already has child {}", describe(), child->describe());
        return;
    }
#endif

    // Sibling-unique names (doc/usd-compatibility-plan.md M2): every producer
    // - node creation, paste, duplicate, glTF import, prefab instantiation -
    // reaches a parent through here, so the numeric suffix is applied once,
    // here, and no producer needs code of its own. A site that needs the name
    // the item was created with looks the item up by path or id, not by name.
    const std::string unique_name = make_sibling_unique_name(this, child->get_name(), child.get());
    if (unique_name != child->get_name()) {
        log->info("renaming '{}' to '{}': a child of '{}' already has that name", child->get_name(), unique_name, describe());
        child->handle_sibling_unique_rename(unique_name);
    }

    log->trace("Adding child '{}' to '{}'", child->describe(), describe());

    position = std::min(m_children.size(), position);
    m_children.insert(m_children.begin() + position, child);
    bump_item_mutation_serial();
    invalidate_dependents(child_count_property.get()); // D26
}

void Hierarchy::handle_remove_child(Hierarchy* const child)
{
    ERHE_VERIFY(child != nullptr);

    const auto i = std::remove_if(
        m_children.begin(),
        m_children.end(),
        [child](const std::shared_ptr<Hierarchy>& item) {
            return item.get() == child;
        }
    );
    if (i != m_children.end()) {
        log->trace("Removing child '{}' from '{}'", child->describe(), describe());
        m_children.erase(i, m_children.end());
        bump_item_mutation_serial();
        invalidate_dependents(child_count_property.get()); // D26
    } else {
        log->error("child '{}' cannot be removed from parent '{}': child not found", child->describe(), describe());
    }
}

void Hierarchy::handle_parent_update(Hierarchy* const old_parent, Hierarchy* const new_parent)
{
    static_cast<void>(old_parent);
    static_cast<void>(new_parent);
}

void Hierarchy::set_depth_recursive(const std::size_t depth)
{
    if (m_depth == depth)  {
        return;
    }
    m_depth = depth;
    for (const auto& child : m_children) {
        ERHE_VERIFY(child.get() != this);
        child->set_depth_recursive(depth + 1);
    }
}

auto Hierarchy::get_root() -> std::weak_ptr<Hierarchy>
{
    const auto& current_parent = get_parent().lock();
    if (!current_parent) {
        return shared_hierarchy_from_this();
    }
    return current_parent->get_root();
}

auto Hierarchy::get_path() const -> std::string
{
    // Cold path: object references, expression paths, lookups and
    // diagnostics. Names are collected leaf first and joined in reverse.
    std::vector<const std::string*> names;
    const Hierarchy* node = this;
    for (;;) {
        const std::shared_ptr<Hierarchy> parent = node->m_parent.lock();
        if (!parent) {
            break; // node is the root: its name is not part of the path
        }
        names.push_back(&node->get_name());
        node = parent.get();
    }
    std::string path;
    std::size_t length = 0;
    for (const std::string* const name : names) {
        length += name->size() + 1;
    }
    path.reserve(length);
    for (std::size_t i = names.size(); i > 0; --i) {
        if (!path.empty()) {
            path.push_back('/');
        }
        path.append(*names[i - 1]);
    }
    return path;
}

auto Hierarchy::make_sibling_unique_name(
    const Hierarchy* const parent,
    const std::string_view wanted_name,
    const Hierarchy* const exclude
) -> std::string
{
    if (parent == nullptr) {
        return std::string{wanted_name};
    }

    return make_unique_name(
        wanted_name,
        [parent, exclude](const std::string_view candidate) -> bool {
            for (const std::shared_ptr<Hierarchy>& child : parent->m_children) {
                if (child && (child.get() != exclude) && (child->get_name() == candidate)) {
                    return true;
                }
            }
            return false;
        }
    );
}

auto Hierarchy::make_unique_name(
    const std::string_view                       wanted_name,
    const std::function<bool(std::string_view)>& is_taken
) -> std::string
{
    if (!is_taken(wanted_name)) {
        return std::string{wanted_name};
    }

    // The base is the wanted name without a trailing '_<digits>', so a
    // colliding 'Cube_1' continues the 'Cube' series. A name that is nothing
    // but '_<digits>' has no base of its own and is used whole.
    std::string_view base = wanted_name;
    std::size_t digits_begin = base.size();
    while ((digits_begin > 0) && (base[digits_begin - 1] >= '0') && (base[digits_begin - 1] <= '9')) {
        --digits_begin;
    }
    if ((digits_begin < base.size()) && (digits_begin >= 2) && (base[digits_begin - 1] == '_')) {
        base = base.substr(0, digits_begin - 1);
    }

    for (std::size_t number = 1; ; ++number) {
        std::string candidate = std::string{base} + "_" + std::to_string(number);
        if (!is_taken(candidate)) {
            return candidate;
        }
    }
}

auto Hierarchy::is_name_available(const std::string_view name) const -> bool
{
    const std::shared_ptr<Hierarchy> parent = m_parent.lock();
    if (!parent) {
        // No parent of its own: the namespace is the one the inheritance
        // container imposes, which is what Item_base answers - a
        // content-library item shares the namespace of its entry node.
        return Item_base::is_name_available(name);
    }
    for (const std::shared_ptr<Hierarchy>& sibling : parent->m_children) {
        if (sibling && (sibling.get() != this) && (sibling->get_name() == name)) {
            return false;
        }
    }
    return true;
}

void Hierarchy::handle_sibling_unique_rename(const std::string& unique_name)
{
    set_name(unique_name);
}

auto Hierarchy::get_reference_path() const -> std::string
{
    std::string path = get_path();
    return path.empty() ? get_name() : path;
}

auto find_by_path(Hierarchy& root, const std::string_view path) -> Hierarchy*
{
    Hierarchy* node  = &root;
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view name = (slash == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, slash - start);
        start = (slash == std::string_view::npos) ? path.size() : slash + 1;
        Hierarchy* next = nullptr;
        for (const std::shared_ptr<Hierarchy>& child : node->get_children()) {
            if (child && (child->get_name() == name)) {
                next = child.get();
                break;
            }
        }
        if (next == nullptr) {
            return nullptr;
        }
        node = next;
    }
    return node;
}

auto Hierarchy::get_parent() const -> std::weak_ptr<Hierarchy>
{
    return m_parent;
}

auto Hierarchy::get_depth() const -> std::size_t
{
    return m_depth;
}

auto Hierarchy::get_children() const -> const std::vector<std::shared_ptr<Hierarchy>>&
{
    return m_children;
}

auto Hierarchy::get_mutable_children() -> std::vector<std::shared_ptr<Hierarchy>>&
{
    // Mutable access bypasses the add/remove child hooks (e.g. reorder within
    // the same parent); conservatively assume the caller mutates the children.
    bump_item_mutation_serial();
    return m_children;
}

void Hierarchy::hierarchy_sanity_check(bool destruction_in_progress) const
{
#if 1
    sanity_check_root_path(this);

    const auto& current_parent = m_parent.lock();
    if (current_parent) {
        bool child_found_in_parent = false;
        for (const auto& child : current_parent->get_children()) {
            if (child.get() == this) {
                child_found_in_parent = true;
                break;
            }
        }
        if (!child_found_in_parent) {
            log->error("Item {0} parent {1} does not have item {0} as child", describe(), current_parent->describe());
        }
    }

    for (const auto& child : m_children) {
        if (destruction_in_progress) {
            std::shared_ptr<Hierarchy> expected_missing_parent = child->get_parent().lock();
            if (expected_missing_parent) {
                log->error(
                    "Item {} child {} parent == {} (expected missing parent, as it is being destroyed)",
                    describe(),
                    child->describe(),
                    expected_missing_parent->describe()
                );
            }
        } else if (child->get_parent().lock().get() != this) {
            log->error(
                "Item {} child {} parent == {}",
                describe(),
                child->describe(),
                (child->get_parent().lock()) ? child->get_parent().lock()->describe() : "(none)"
            );
        }
        if (child->get_depth() != get_depth() + 1) {
            log->error("Item {} depth = {}, child {} depth = {}", describe(), get_depth(), child->describe(), child->get_depth());
        }
        child->hierarchy_sanity_check();
    }
#endif
}

void Hierarchy::sanity_check_root_path(const Hierarchy* item) const
{
    const auto& current_parent = m_parent.lock();
    if (current_parent) {
        if (current_parent.get() == item) {
            log->error("Item {} has itself as an ancestor", item->describe());
        }
        current_parent->sanity_check_root_path(item);
    }
}

void Hierarchy::trace()
{
    std::stringstream ss;
    for (int i = 0; i < m_depth; ++i) {
        ss << "  ";
    }
    log->trace("{}{} (depth = {})", ss.str(), describe(), get_depth());
    for (const auto& child : m_children) {
        child->trace();
    }
}

} // namespace erhe
