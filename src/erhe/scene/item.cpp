#include "erhe/scene/item.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::scene
{

[[nodiscard]] auto Item_flags::to_string(uint64_t mask) -> std::string
{
    if (mask == none) return std::string{"no flags "};
    std::stringstream ss;
    if (mask & no_message               ) ss << "no_message ";
    if (mask & show_debug_visualizations) ss << "show_debug_visualizations ";
    if (mask & shadow_cast              ) ss << "shadow_cast ";
    if (mask & selected                 ) ss << "selected ";
    if (mask & visible                  ) ss << "visible ";
    if (mask & render_wireframe         ) ss << "render_wireframe ";
    if (mask & render_bounding_box      ) ss << "render_bounding_box ";
    if (mask & content                  ) ss << "content ";
    if (mask & id                       ) ss << "id ";
    if (mask & tool                     ) ss << "tool ";
    if (mask & brush                    ) ss << "brush ";
    return ss.str();
}

//    if (mask & node                     ) ss << "node ";
//    if (mask & attachment               ) ss << "attachment ";
//    if (mask & physics                  ) ss << "physics ";
//    if (mask & raytrace                 ) ss << "raytrace ";
//    if (mask & frame_controller         ) ss << "frame_controller ";
//    if (mask & grid                     ) ss << "grid ";
//    if (mask & light                    ) ss << "light ";
//    if (mask & camera                   ) ss << "camera ";
//    if (mask & mesh                     ) ss << "mesh ";
//    if (mask & rendertarget             ) ss << "rendertarget ";
//    if (mask & controller               ) ss << "controller ";

auto Item_filter::operator()(const uint64_t visibility_mask) const -> bool
{
    if ((visibility_mask & require_all_bits_set) != require_all_bits_set)
    {
        return false;
    }
    if (require_at_least_one_bit_set != 0u)
    {
        if ((visibility_mask & require_at_least_one_bit_set) == 0u)
        {
            return false;
        }
    }
    if ((visibility_mask & require_all_bits_clear) != 0u)
    {
        return false;
    }
    if (require_at_least_one_bit_clear != 0u)
    {
        if ((visibility_mask & require_at_least_one_bit_clear) == require_at_least_one_bit_clear)
        {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------

Item::Item()
{
    m_label = fmt::format("##Item{}", m_id.get_id());
}

Item::Item(const std::string_view name)
    : m_name{name}
{
    m_label = fmt::format("{}##Item{}", name, m_id.get_id());
}

Item::~Item() noexcept = default;

auto Item::get_name() const -> const std::string&
{
    return m_name;
}

void Item::set_name(const std::string_view name)
{
    m_name = name;
    m_label = fmt::format("{}##Item{}", name, m_id.get_id());
}

auto Item::get_label() const -> const std::string&
{
    return m_label;
}

auto Item::get_item_host() const -> Scene_host*
{
    return nullptr;
}

auto Item::get_type() const -> uint64_t
{
    return Item_type::none;
}

auto Item::type_name() const -> const char*
{
    return "Item";
}

auto Item::get_flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

void Item::set_flag_bits(const uint64_t mask, const bool value)
{
    const auto old_flag_bits = m_flag_bits;
    if (value)
    {
        m_flag_bits = m_flag_bits | mask;
    }
    else
    {
        m_flag_bits = m_flag_bits & ~mask;
    }

    if (m_flag_bits != old_flag_bits)
    {
        handle_flag_bits_update(old_flag_bits, m_flag_bits);
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

auto Item::is_selected() const -> bool
{
    return (m_flag_bits & Item_flags::selected) == Item_flags::selected;
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
    return (m_flag_bits & Item_flags::visible) == Item_flags::visible;
}

auto Item::is_shown_in_ui() const -> bool
{
    return (m_flag_bits & Item_flags::show_in_ui) == Item_flags::show_in_ui;
}

auto Item::is_hidden() const -> bool
{
    return !is_visible();
}

auto Item::describe() -> std::string
{
    return fmt::format(
        "type = {}, name = {}, id = {}, flags = {}",
        type_name(),
        get_name(),
        get_id(),
        Item_flags::to_string(get_flag_bits())
    );
}

void Item::set_wireframe_color(const glm::vec4& color)
{
    m_wireframe_color = color;
}

 auto Item::get_wireframe_color() const -> glm::vec4
{
    return m_wireframe_color;
}

} // namespace erhe::scene
