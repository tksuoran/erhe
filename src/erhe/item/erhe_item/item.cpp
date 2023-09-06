#include "erhe_item/item.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe
{

using namespace erhe::item;

[[nodiscard]] auto Item_flags::to_string(const uint64_t flags) -> std::string
{
    std::stringstream ss;

    using Item_flags = Item_flags;
    bool first = true;
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = (uint64_t{1} << bit_position);
        const bool     value    = erhe::bit::test_all_rhs_bits_set(flags, bit_mask);
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

Item_base::Item_base() = default;

Item_base::Item_base(const std::string_view name)
    : m_name {name}
    , m_label{fmt::format("{}##{}", name, get_id())}
{
}

Item_base::Item_base(const Item_base& other)
    : m_flag_bits  {other.m_flag_bits}
    , m_name       {std::format("{} Copy", other.m_name)}
    , m_source_path{other.m_source_path}
{
    m_label = fmt::format("{}##{}", m_name, get_id());
}

auto Item_base::operator=(const Item_base& other) -> Item_base&
{
    m_flag_bits   = other.m_flag_bits;
    m_name        = std::format("{} Copy", other.m_name);
    m_source_path = other.m_source_path;
    m_label       = fmt::format("{}##{}", m_name, get_id());
    return *this;
}

Item_base::~Item_base() noexcept = default;

auto Item_base::get_name() const -> const std::string&
{
    return m_name;
}

auto Item_base::get_label() const -> const std::string&
{
    return m_label;
}

auto Item_base::get_flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

void Item_base::set_flag_bits(const uint64_t mask, const bool value)
{
    const auto old_flag_bits = m_flag_bits;
    if (value) {
        m_flag_bits = m_flag_bits | mask;
    } else {
        m_flag_bits = m_flag_bits & ~mask;
    }

    if (m_flag_bits != old_flag_bits) {
        handle_flag_bits_update(old_flag_bits, m_flag_bits);
    }
}

void Item_base::enable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, true);
}

void Item_base::disable_flag_bits(const uint64_t mask)
{
    set_flag_bits(mask, false);
}

auto Item_base::is_no_transform_update() const -> bool
{
    return (m_flag_bits & Item_flags::no_transform_update) == Item_flags::no_transform_update;
}

auto Item_base::is_transform_world_normative() const -> bool
{
    return (m_flag_bits & Item_flags::transform_world_normative) == Item_flags::transform_world_normative;
}

auto Item_base::is_selected() const -> bool
{
    return (m_flag_bits & Item_flags::selected) == Item_flags::selected;
}

void Item_base::set_selected(const bool selected)
{
    set_flag_bits(Item_flags::selected, selected);
}

void Item_base::set_visible(const bool value)
{
    set_flag_bits(Item_flags::visible, value);
}

void Item_base::show()
{
    set_flag_bits(Item_flags::visible, true);
}

void Item_base::hide()
{
    set_flag_bits(Item_flags::visible, false);
}

auto Item_base::is_visible() const -> bool
{
    return (m_flag_bits & Item_flags::visible) == Item_flags::visible;
}

auto Item_base::is_shown_in_ui() const -> bool
{
    return (m_flag_bits & Item_flags::show_in_ui) == Item_flags::show_in_ui;
}

auto Item_base::is_hidden() const -> bool
{
    return !is_visible();
}

void Item_base::set_source_path(const std::filesystem::path& path)
{
    m_source_path = path;
}

auto Item_base::get_source_path() const -> const std::filesystem::path&
{
    return m_source_path;
}

void Item_base::set_name(const std::string_view name)
{
    m_name = name;
    m_label = fmt::format("{}##{}", name, get_id());
}

auto Item_base::describe(int level) const -> std::string
{
    switch (level) {
        case 0:  return get_name();
        case 1:  return fmt::format("{} {}", get_type_name(), get_name());
        case 2:  return fmt::format("{} {}, id = {}", get_type_name(), get_name(), get_id());
        default: return fmt::format("{} {}, id = {}, flags = {}", get_type_name(), get_name(), get_id(), Item_flags::to_string(get_flag_bits()));
    }
}

auto Item_base::get_id() const -> std::size_t
{
    return m_id.get_id();
}

} // namespace erhe

