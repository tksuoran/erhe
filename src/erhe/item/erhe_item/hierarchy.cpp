#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <charconv>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace erhe {

using namespace erhe::item;

const erhe::property::Property<int> Hierarchy::child_count_property = erhe::property::Property<int>::register_computed(
    "child_count", Hierarchy::property_owner_type(),
    [](const erhe::property::Dependency_object& object) -> erhe::property::Property_value {
        return static_cast<int>(static_cast<const Hierarchy&>(object).get_child_count());
    },
    erhe::property::Property_metadata{
        .flags = erhe::property::Property_flags::none,
        .ui    = erhe::property::Property_ui{.tooltip = "Number of direct children (computed)", .developer_only = true, .label = "Child Count"}
    }
);

namespace {

// A transparent hash, so the maps below take a std::string_view lookup without making a std::string.
class Name_hash
{
public:
    using is_transparent = void;
    [[nodiscard]] auto operator()(const std::string_view name) const noexcept -> std::size_t
    {
        return std::hash<std::string_view>{}(name);
    }
};

// A name of the form <base>_<n>, n >= 1 written as std::to_string writes it: the form the suffix rule makes.
[[nodiscard]] auto parse_series_member(const std::string_view name, std::string_view& out_base, std::size_t& out_number) -> bool
{
    const std::size_t underscore = name.rfind('_');
    if ((underscore == std::string_view::npos) || ((underscore + 1) >= name.size()) || (name[underscore + 1] == '0')) {
        return false;
    }
    const char* const digits_end = name.data() + name.size();
    std::size_t number = 0;
    const std::from_chars_result parsed = std::from_chars(name.data() + underscore + 1, digits_end, number);
    if ((parsed.ec != std::errc{}) || (parsed.ptr != digits_end)) {
        return false;
    }
    out_base   = name.substr(0, underscore);
    out_number = number;
    return true;
}

[[nodiscard]] auto make_series_name(const std::string_view base, const std::size_t number) -> std::string
{
    return fmt::format("{}_{}", base, number);
}

} // anonymous namespace

// The names of a parent's children (doc/erhe/item.md "Sibling-unique names").
class Hierarchy::Child_name_index
{
public:
    class Series
    {
    public:
        std::size_t first_free{1};   // every <base>_<n> below it is held, and <base>_<first_free> is free
        std::size_t member_count{0}; // distinct names <base>_<n> held
    };
    using Name_counts = std::unordered_map<std::string, std::size_t, Name_hash, std::equal_to<>>;
    using Series_map  = std::unordered_map<std::string, Series, Name_hash, std::equal_to<>>;

    Name_counts name_counts; // children holding each name (more than one only where a name was set past the rule)
    Series_map  series;      // per base of a held <base>_<n>

    [[nodiscard]] auto count(const std::string_view name) const -> std::size_t
    {
        const Name_counts::const_iterator i = name_counts.find(name);
        return (i != name_counts.end()) ? i->second : 0;
    }

    [[nodiscard]] auto get_first_free(const std::string_view base) const -> std::size_t
    {
        const Series_map::const_iterator i = series.find(base);
        return (i != series.end()) ? i->second.first_free : 1;
    }

    void add(const std::string_view name)
    {
        const Name_counts::iterator i = name_counts.find(name);
        if (i != name_counts.end()) {
            ++i->second;
            return;
        }
        name_counts.emplace(std::string{name}, std::size_t{1});
        std::string_view base;
        std::size_t      number = 0;
        if (!parse_series_member(name, base, number)) {
            return;
        }
        Series_map::iterator entry = series.find(base);
        if (entry == series.end()) {
            entry = series.emplace(std::string{base}, Series{}).first;
        }
        Series& member_series = entry->second;
        ++member_series.member_count;
        // The first free suffix was taken: move past the held suffixes after it.
        if (number == member_series.first_free) {
            do {
                ++member_series.first_free;
            } while (count(make_series_name(base, member_series.first_free)) > 0);
        }
    }

    void remove(const std::string_view name)
    {
        const Name_counts::iterator i = name_counts.find(name);
        ERHE_VERIFY(i != name_counts.end());
        if (--i->second > 0) {
            return;
        }
        name_counts.erase(i);
        std::string_view base;
        std::size_t      number = 0;
        if (!parse_series_member(name, base, number)) {
            return;
        }
        const Series_map::iterator entry = series.find(base);
        ERHE_VERIFY(entry != series.end());
        if (--entry->second.member_count == 0) {
            series.erase(entry);
            return;
        }
        entry->second.first_free = std::min(entry->second.first_free, number);
    }
};

Hierarchy::Hierarchy() = default;

Hierarchy::~Hierarchy() noexcept
{
    // A child may outlive this parent: it must not reach this index later.
    drop_child_name_index();
}

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
    if (m_children.size() > c_child_name_index_build_count) {
        build_child_name_index();
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

    // The name changes below without set_name(): leave the old parent's index first.
    if (m_name_index_owner != nullptr) {
        m_name_index_owner->unindex_child_name(*this);
    }
    Item::operator=(src);

    // Detach from old parent before resetting m_parent
    std::shared_ptr<Hierarchy> old_parent = m_parent.lock();
    if (old_parent) {
        old_parent->handle_remove_child(this);
    }

    drop_child_name_index();
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
    if (m_children.size() > c_child_name_index_build_count) {
        build_child_name_index();
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
    ERHE_PROFILE_SCOPE("Hierarchy::set_parent");

    ERHE_VERIFY(new_parent_.get() != this);
    std::shared_ptr<Hierarchy> old_parent_shared = m_parent.lock();
    Hierarchy*                 old_parent         = old_parent_shared.get();
    Hierarchy*                 new_parent         = new_parent_.get();

    if (old_parent == new_parent) {
        m_parent = new_parent_;
        return;
    }

    // The index that holds this item's name is left first, whatever m_parent says (a clone's children have none).
    if (m_name_index_owner != nullptr) {
        m_name_index_owner->unindex_child_name(*this);
    }

    // Inherited property values of this subtree may change with the parent;
    // capture them while the old parent is still in place so the change
    // notifications after the move carry the right old values.
    const erhe::property::Inheritance_snapshot inheritance_snapshot = capture_inheritance_snapshot(
        // A parentless item inherits from the container that holds it.
        (new_parent != nullptr) ? new_parent : Item_base::get_inheritance_parent()
    );
    m_parent = new_parent_;

    // describe() formats a string: only when the line is going to be written.
    const bool log_trace = log->should_log(spdlog::level::trace);
    if (log_trace) {
        log->trace(
            "Parent change for '{}' old parent = '{}', new parent = '{}'",
            describe(),
            (old_parent != nullptr) ? old_parent->describe() : "none",
            (new_parent != nullptr) ? new_parent->describe() : "none"
        );
    }

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
    } else if (log_trace) {
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

auto Hierarchy::is_pruned_by_parent() const -> bool
{
    // The proxy a pruning parent supplies is the one child the pruning does
    // not reach: it stands in for the subtree that left, so pruning it would
    // leave nothing drawn at all (Item_flags::draw_mode_proxy).
    if ((m_flag_bits & Item_flags::draw_mode_proxy) != 0) {
        return false;
    }
    const std::shared_ptr<Hierarchy> parent = m_parent.lock();
    return parent && parent->prunes_children();
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
    // it (doc/editor/content_library_folders.md D1).
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

    // Sibling-unique names (doc/erhe/usd_compatibility_design.md M2): every producer
    // - node creation, paste, duplicate, glTF import, prefab instantiation -
    // reaches a parent through here, so the numeric suffix is applied once,
    // here, and no producer needs code of its own. A site that needs the name
    // the item was created with looks the item up by path or id, not by name.
    const std::string unique_name = make_sibling_unique_name(this, child->get_name(), child.get());
    if (unique_name != child->get_name()) {
        // A bulk insert renames every item it adds: trace only.
        if (log->should_log(spdlog::level::trace)) {
            log->trace("renaming '{}' to '{}': a child of '{}' already has that name", child->get_name(), unique_name, describe());
        }
        child->handle_sibling_unique_rename(unique_name);
    }

    if (log->should_log(spdlog::level::trace)) {
        log->trace("Adding child '{}' to '{}'", child->describe(), describe());
    }

    position = std::min(m_children.size(), position);
    m_children.insert(m_children.begin() + position, child);
    if (m_child_names) {
        index_child_name(*child);
    } else if (m_children.size() > c_child_name_index_build_count) {
        build_child_name_index();
    }
    bump_item_mutation_serial();
    invalidate_dependents(child_count_property.get()); // D26
}

void Hierarchy::handle_remove_child(Hierarchy* const child)
{
    ERHE_VERIFY(child != nullptr);

    if (child->m_name_index_owner == this) {
        unindex_child_name(*child);
    }
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
        if (m_child_names && (m_children.size() < c_child_name_index_drop_count)) {
            drop_child_name_index();
        }
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
    if (!parent->m_child_names) {
        // A few children: the scan over them is cheap and gives the same name the index would.
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

    const Child_name_index& index           = *parent->m_child_names;
    const bool              exclude_indexed = (exclude != nullptr) && (exclude->m_name_index_owner == parent);
    const std::string_view  excluded_name   = exclude_indexed ? std::string_view{exclude->get_name()} : std::string_view{};
    const auto is_taken = [&index, exclude_indexed, excluded_name](const std::string_view candidate) -> bool {
        const std::size_t holders = index.count(candidate);
        return holders > ((exclude_indexed && (candidate == excluded_name)) ? std::size_t{1} : std::size_t{0});
    };
    if (!is_taken(wanted_name)) {
        return std::string{wanted_name};
    }

    const std::string_view base       = get_unique_name_base(wanted_name);
    const std::size_t      first_free = index.get_first_free(base);
    // Below the first free suffix, only the excluded child's own suffix can be free.
    std::string_view excluded_base;
    std::size_t      excluded_number = 0;
    if (exclude_indexed && parse_series_member(excluded_name, excluded_base, excluded_number) &&
        (excluded_base == base) && (excluded_number < first_free) && !is_taken(excluded_name)) {
        return std::string{excluded_name};
    }
    for (std::size_t number = first_free; ; ++number) {
        std::string candidate = make_series_name(base, number);
        if (!is_taken(candidate)) {
            return candidate;
        }
    }
}

auto Hierarchy::get_unique_name_base(const std::string_view wanted_name) -> std::string_view
{
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
    return base;
}

void Hierarchy::index_child_name(Hierarchy& child)
{
    ERHE_VERIFY(m_child_names && (child.m_name_index_owner == nullptr));
    m_child_names->add(child.get_name());
    child.m_name_index_owner = this;
}

void Hierarchy::unindex_child_name(Hierarchy& child)
{
    ERHE_VERIFY(m_child_names && (child.m_name_index_owner == this));
    m_child_names->remove(child.get_name());
    child.m_name_index_owner = nullptr;
}

void Hierarchy::build_child_name_index()
{
    m_child_names = std::make_unique<Child_name_index>();
    for (const std::shared_ptr<Hierarchy>& child : m_children) {
        // A child another parent indexes (a clone's child reparented before the clone was attached) is not this one's.
        if (child && (child->m_name_index_owner == nullptr)) {
            index_child_name(*child);
        }
    }
}

void Hierarchy::drop_child_name_index()
{
    for (const std::shared_ptr<Hierarchy>& child : m_children) {
        if (child && (child->m_name_index_owner == this)) {
            child->m_name_index_owner = nullptr;
        }
    }
    m_child_names.reset();
}

auto Hierarchy::is_child_name_index_consistent() const -> bool
{
    if (!m_child_names) {
        for (const std::shared_ptr<Hierarchy>& child : m_children) {
            if (child && (child->m_name_index_owner == this)) {
                return false;
            }
        }
        return m_children.size() <= c_child_name_index_build_count;
    }
    if (m_children.size() < c_child_name_index_drop_count) {
        return false;
    }
    Child_name_index expected;
    for (const std::shared_ptr<Hierarchy>& child : m_children) {
        if (!child || (child->m_name_index_owner != this)) {
            return false;
        }
        expected.add(child->get_name());
    }
    if (expected.name_counts != m_child_names->name_counts) {
        return false;
    }
    if (expected.series.size() != m_child_names->series.size()) {
        return false;
    }
    for (const std::pair<const std::string, Child_name_index::Series>& entry : expected.series) {
        const Child_name_index::Series_map::const_iterator other = m_child_names->series.find(entry.first);
        if (
            (other == m_child_names->series.end()) ||
            (other->second.first_free != entry.second.first_free) ||
            (other->second.member_count != entry.second.member_count)
        ) {
            return false;
        }
    }
    return true;
}

void Hierarchy::handle_name_change(const std::string_view new_name)
{
    if (m_name_index_owner != nullptr) {
        Child_name_index& index = *m_name_index_owner->m_child_names;
        index.remove(get_name());
        index.add(new_name);
    }
}

auto Hierarchy::make_unique_name(
    const std::string_view                       wanted_name,
    const std::function<bool(std::string_view)>& is_taken
) -> std::string
{
    if (!is_taken(wanted_name)) {
        return std::string{wanted_name};
    }

    const std::string_view base = get_unique_name_base(wanted_name);
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
    if (!parent->m_child_names) {
        for (const std::shared_ptr<Hierarchy>& sibling : parent->m_children) {
            if (sibling && (sibling.get() != this) && (sibling->get_name() == name)) {
                return false;
            }
        }
        return true;
    }
    // This item's own name does not count against it.
    const std::size_t own = ((m_name_index_owner == parent.get()) && (name == get_name())) ? std::size_t{1} : std::size_t{0};
    return parent->m_child_names->count(name) <= own;
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
#if !defined(NDEBUG)
    if (!is_child_name_index_consistent()) {
        log->error("Item {}: its child name index does not match its children", describe());
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
