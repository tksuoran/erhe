#include "erhe/scene/item.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::scene
{

[[nodiscard]] auto Item_flags::to_string(const uint64_t flags) -> std::string
{
    std::stringstream ss;

    using namespace erhe::toolkit;
    using Item_flags = erhe::scene::Item_flags;

    bool first = true;
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        const bool     value    = test_all_rhs_bits_set(flags, bit_mask);
        if (value) {
            if (!first) {
                ss << " | ";
            }
            ss << Item_flags::c_bit_labels[bit_position];
            first = false;
        }
    }
    return ss.str();
}

auto Item_filter::operator()(const uint64_t visibility_mask) const -> bool
{
    if ((visibility_mask & require_all_bits_set) != require_all_bits_set) {
        return false;
    }
    if (require_at_least_one_bit_set != 0u) {
        if ((visibility_mask & require_at_least_one_bit_set) == 0u) {
            return false;
        }
    }
    if ((visibility_mask & require_all_bits_clear) != 0u) {
        return false;
    }
    if (require_at_least_one_bit_clear != 0u) {
        if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear) {
            return false;
        }
    }
    return true;
}

auto Item_filter::describe() const -> std::string
{
    bool first = true;
    std::stringstream ss;
    if (require_all_bits_set != 0) {
        ss << "require_all_bits_set = " << Item_flags::to_string(this->require_all_bits_set);
        first = false;
    }
    if (require_at_least_one_bit_set != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_set = " << Item_flags::to_string(this->require_at_least_one_bit_set);
        first = false;
    }
    if (require_all_bits_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_all_bits_clear = " << Item_flags::to_string(this->require_all_bits_clear);
        first = false;
    }
    if (require_at_least_one_bit_clear != 0) {
        if (!first) {
            ss << ", ";
        }
        ss << "require_at_least_one_bit_clear = " << Item_flags::to_string(this->require_at_least_one_bit_clear);
    }
    return ss.str();
}

// -----------------------------------------------------------------------------

Item::Item()
{
    item_data.label = fmt::format("##Item{}", m_id.get_id());
}

Item::Item(const std::string_view name)
    : item_data{
        .name  = std::string{name.begin(), name.end()},
        .label = fmt::format("{}##Item{}", name, m_id.get_id())
    }
{
}

Item::~Item() noexcept
{
    log->trace(
        "~Item '{}' depth = {} child count = {}",
        get_name(),
        get_depth(),
        item_data.children.size()
    );
    remove();
}

void Item::remove()
{
    log->trace(
        "Item::remove '{}' depth = {} child count = {}",
        get_name(),
        get_depth(),
        item_data.children.size()
    );

    item_sanity_check();

    std::shared_ptr<Item> parent = item_data.parent.lock();
    while (!item_data.children.empty()) {
        item_data.children.back()->set_parent(parent);
    }

    set_parent({});

    item_sanity_check();
}

void Item::recursive_remove()
{
    while (!item_data.children.empty()) {
        item_data.children.back()->recursive_remove();
    }

    set_parent({});
}

void Item::handle_flag_bits_update(
    const uint64_t old_flag_bits,
    const uint64_t new_flag_bits
)
{
    static_cast<void>(old_flag_bits);
    static_cast<void>(new_flag_bits);
}

auto Item::get_name() const -> const std::string&
{
    return item_data.name;
}

void Item::set_name(const std::string_view name)
{
    item_data.name = name;
    item_data.label = fmt::format("{}##Item{}", name, m_id.get_id());
}

auto Item::get_label() const -> const std::string&
{
    return item_data.label;
}

auto Item::get_item_host() const -> Item_host*
{
    return nullptr;
}

auto Item::get_type() const -> uint64_t
{
    return Item_type::none;
}

auto Item::get_type_name() const -> const char*
{
    return "Item";
}

auto Item::get_flag_bits() const -> uint64_t
{
    return item_data.flag_bits;
}

void Item::set_flag_bits(const uint64_t mask, const bool value)
{
    const auto old_flag_bits = item_data.flag_bits;
    if (value) {
        item_data.flag_bits = item_data.flag_bits | mask;
    } else {
        item_data.flag_bits = item_data.flag_bits & ~mask;
    }

    if (item_data.flag_bits != old_flag_bits) {
        handle_flag_bits_update(old_flag_bits, item_data.flag_bits);
    }
}

void Item::enable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, true);
}

void Item::disable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, false);
}

auto Item::get_id() const -> erhe::toolkit::Unique_id<Item>::id_type
{
    return m_id.get_id();
}

auto Item::is_no_transform_update() const -> bool
{
    return (item_data.flag_bits & Item_flags::no_transform_update) == Item_flags::no_transform_update;
}

auto Item::is_transform_world_normative() const -> bool
{
    return (item_data.flag_bits & Item_flags::transform_world_normative) == Item_flags::transform_world_normative;
}

auto Item::is_selected() const -> bool
{
    return (item_data.flag_bits & Item_flags::selected) == Item_flags::selected;
}

void Item::set_selected(const bool selected)
{
    set_flag_bits(Item_flags::selected, selected);
}

void Item::set_visible(const bool value)
{
    set_flag_bits(Item_flags::visible, value);
}

void Item::show()
{
    set_flag_bits(Item_flags::visible, true);
}

void Item::hide()
{
    set_flag_bits(Item_flags::visible, false);
}

auto Item::is_visible() const -> bool
{
    return (item_data.flag_bits & Item_flags::visible) == Item_flags::visible;
}

auto Item::is_shown_in_ui() const -> bool
{
    return (item_data.flag_bits & Item_flags::show_in_ui) == Item_flags::show_in_ui;
}

auto Item::is_hidden() const -> bool
{
    return !is_visible();
}

auto Item::describe() const -> std::string
{
    return fmt::format(
        "type = {}, name = {}, id = {}, flags = {}",
        get_type_name(),
        get_name(),
        get_id(),
        Item_flags::to_string(get_flag_bits())
    );
}

void Item::set_wireframe_color(const glm::vec4& color)
{
    item_data.wireframe_color = color;
}

auto Item::get_wireframe_color() const -> glm::vec4
{
    return item_data.wireframe_color;
}

void Item::set_source_path(const std::filesystem::path& path)
{
    item_data.source_path = path;
}

auto Item::get_source_path() const -> const std::filesystem::path&
{
    return item_data.source_path;
}

auto Item::get_child_count() const -> std::size_t
{
    return item_data.children.size();
}

auto Item::get_child_count(const Item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& child : item_data.children) {
        if (filter(child->get_flag_bits())) {
            ++result;
        }
    }
    return result;
}

auto Item::get_index_in_parent() const -> std::size_t
{
    const auto& current_parent = get_parent().lock();
    if (current_parent) {
        const auto index = current_parent->get_index_of_child(this);
        return index.has_value() ? index.value() : 0;
    }
    return 0;
}

auto Item::get_index_of_child(const Item* child) const -> std::optional<std::size_t>
{
    for (
        std::size_t i = 0, end = item_data.children.size();
        i < end;
        ++i
    ) {
        if (item_data.children[i].get() == child) {
            return i;
        }
    }
    return {};
}

auto Item::is_ancestor(const Item* ancestor_candidate) const -> bool
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

void Item::set_parent(
    const std::shared_ptr<Item>& new_parent_item,
    const std::size_t            position
)
{
    Item* old_parent = item_data.parent.lock().get();
    item_data.parent = new_parent_item;
    Item* new_parent = item_data.parent.lock().get();

    if (old_parent == new_parent) {
        return;
    }

    log->trace(
        "Parent change for '{}' old parent = '{}', new parent = '{}'",
        get_name(),
        (old_parent != nullptr) ? old_parent->get_name() : "none",
        (new_parent != nullptr) ? new_parent->get_name() : "none"
    );

    // Keep this alive until end of scope.
    // - We need to keep this alive while being removed from old parent before being added to new parent
    // - Also in case new_parent is empty, for any other access to this
    auto shared_this = weak_from_this().lock();

    if (old_parent) {
        old_parent->handle_remove_child(this);
    }

    if (new_parent) {
        new_parent->handle_add_child(
            shared_this,
            position
        );
    } else {
        log->trace("Now orphan: '{}'", get_name());
    }

    set_depth_recursive(
        new_parent
            ? new_parent->get_depth() + 1
            : 0
    );
    handle_parent_update(old_parent, new_parent);
    item_sanity_check();
}

void Item::set_parent(
    Item* const       new_parent_item,
    const std::size_t position
)
{
    if (new_parent_item != nullptr) {
        set_parent(
            new_parent_item->shared_from_this(),
            position
        );
    } else {
        set_parent(std::shared_ptr<Item>{}, position);
    }
}

void Item::handle_add_child(
    const std::shared_ptr<Item>& child,
    const std::size_t            position
)
{
    ERHE_VERIFY(child);

#ifndef NDEBUG
    const auto i = std::find(item_data.children.begin(), item_data.children.end(), child);
    if (i != item_data.children.end()) {
        log->error("{} already has child {}", describe(), child->describe());
        return;
    }
#endif

    log->trace("Adding child '{}' to '{}'", child->get_name(), get_name());
    item_data.children.insert(item_data.children.begin() + position, child);
}

void Item::handle_remove_child(
    Item* const child
)
{
    ERHE_VERIFY(child != nullptr);

    const auto i = std::remove_if(
        item_data.children.begin(),
        item_data.children.end(),
        [child](const std::shared_ptr<Item>& item) {
            return item.get() == child;
        }
    );
    if (i != item_data.children.end()) {
        log->trace("Removing child '{}' from '{}'", child->get_name(), get_name());
        item_data.children.erase(i, item_data.children.end());
    } else {
        log->error(
            "child '{}' cannot be removed from parent '{}': child not found",
            child->describe(),
            get_name()
        );
    }
}

void Item::handle_parent_update(
    Item* const old_parent,
    Item* const new_parent
)
{
    static_cast<void>(old_parent);
    static_cast<void>(new_parent);
}

void Item::set_depth_recursive(const std::size_t depth)
{
    if (item_data.depth == depth)  {
        return;
    }
    item_data.depth = depth;
    for (const auto& child : item_data.children) {
        child->set_depth_recursive(depth + 1);
    }
}

auto Item::get_root() -> std::weak_ptr<Item>
{
    const auto& current_parent = get_parent().lock();
    if (!current_parent) {
        return shared_from_this();
    }
    return current_parent->get_root();
}

auto Item::get_parent() const -> std::weak_ptr<Item>
{
    return item_data.parent;
}

auto Item::get_depth() const -> std::size_t
{
    return item_data.depth;
}

auto Item::get_children() const -> const std::vector<std::shared_ptr<Item>>&
{
    return item_data.children;
}

auto Item::get_mutable_children() -> std::vector<std::shared_ptr<Item>>&
{
    return item_data.children;
}

void Item::item_sanity_check() const
{
#if 1
    sanity_check_root_path(this);

    const auto& current_parent = item_data.parent.lock();
    if (current_parent) {
        bool child_found_in_parent = false;
        for (const auto& child : current_parent->get_children()) {
            if (child.get() == this) {
                child_found_in_parent = true;
                break;
            }
        }
        if (!child_found_in_parent) {
            log->error(
                "Node {} parent {} does not have node as child",
                get_name(),
                current_parent->get_name()
            );
        }
    }

    for (const auto& child : item_data.children) {
        if (child->get_parent().lock().get() != this) {
            log->error(
                "Node {} child {} parent == {}",
                get_name(),
                child->get_name(),
                (child->get_parent().lock())
                    ? child->get_parent().lock()->get_name()
                    : "(none)"
            );
        }
        if (child->get_depth() != get_depth() + 1) {
            log->error(
                "Node {} depth = {}, child {} depth = {}",
                get_name(),
                get_depth(),
                child->get_name(),
                child->get_depth()
            );
        }
        child->item_sanity_check();
    }
#endif
}

void Item::sanity_check_root_path(const Item* item) const
{
    const auto& current_parent = item_data.parent.lock();
    if (current_parent) {
        if (current_parent.get() == item) {
            log->error(
                "Node {} has itself as an ancestor",
                item->get_name()
            );
        }
        current_parent->sanity_check_root_path(item);
    }
}

void Item::trace()
{
    std::stringstream ss;
    for (int i = 0; i < item_data.depth; ++i) {
        ss << "  ";
    }
    log->trace("{}{} (depth = {})", ss.str(), describe(), get_depth());
    for (const auto& child : item_data.children) {
        child->trace();
    }
}

} // namespace erhe::scene
