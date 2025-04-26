#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe {

using namespace erhe::item;

Hierarchy::Hierarchy()           = default;
Hierarchy::~Hierarchy() noexcept = default;

Hierarchy::Hierarchy(const Hierarchy& src)
    : Item{src} // m_parent is not copied from other
{
    m_children.reserve(src.m_children.size());
    for (const auto& src_child : src.m_children) {
        std::shared_ptr<erhe::Item_base> base      = src_child->clone();
        std::shared_ptr<erhe::Hierarchy> dst_child = std::dynamic_pointer_cast<erhe::Hierarchy>(base);
        if (dst_child) {
            m_children.push_back(dst_child);
            dst_child->set_parent(this);
        }
    }
}

Hierarchy::Hierarchy(const Hierarchy& src, for_clone) : Hierarchy{src} {}

Hierarchy& Hierarchy::operator=(const Hierarchy& src)
{
    Item::operator=(src);
    m_children.reserve(src.m_children.size());
    m_parent.reset();
    m_depth = 0;
    for (const auto& src_child : src.m_children) {
        auto dst_child = std::dynamic_pointer_cast<erhe::Hierarchy>(src_child->clone());
        if (dst_child) {
            m_children.push_back(dst_child);
            dst_child->set_parent(this);
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
    Hierarchy* old_parent = m_parent.lock().get();
    m_parent = new_parent_;
    Hierarchy* new_parent = m_parent.lock().get();

    if (old_parent == new_parent) {
        return;
    }

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

    log->trace("Adding child '{}' to '{}'", child->describe(), describe());

    position = std::min(m_children.size(), position);
    m_children.insert(m_children.begin() + position, child);
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

void Hierarchy::for_each(const std::function<bool(Hierarchy& hierarchy)>& fun)
{
    if (!fun(*this)) {
        return;
    }
    for (const auto& child : m_children) {
        child->for_each(fun);
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
