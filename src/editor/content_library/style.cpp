#include "content_library/style.hpp"

#include "content_library/content_library.hpp"

#include <set>

namespace editor {

Style::Style(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Style::~Style() noexcept = default;

auto Style::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return erhe::property::root_owner_type;
}

auto make_unique_style_name(const Content_library& library, const std::string_view base_name) -> std::string
{
    std::set<std::string> used_names;
    for (const std::shared_ptr<Style>& style : library.get_all<Style>()) {
        if (style) {
            used_names.insert(style->get_name());
        }
    }
    std::string final_name{base_name};
    for (std::size_t number = 2; used_names.contains(final_name); ++number) {
        final_name = std::string{base_name} + " (" + std::to_string(number) + ")";
    }
    return final_name;
}

}
