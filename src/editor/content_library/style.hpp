#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_property/owner_type.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace editor {

class Content_library_node;

// A style item of the content library's Styles category
// (doc/style-library.md D2): a named holder of property values of any item
// class. Its secondary owner type (doc/property-system.md D30) is the root
// owner type, so the Add Property picker offers every class's value
// properties by qualified name (`Material.roughness`, `Light.color`) and
// the values live in this item's own store. Any item uses it through its
// `style` property (Item_base::style_property); the style layer of every
// user reads this item's local values, live (D25).
class Style : public erhe::Item<erhe::Item_base, erhe::Typed, Style>
{
public:
    explicit Style(std::string_view name);
    explicit Style(const Style& other) = default;
    ~Style() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Style"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::style; }

    // Overrides erhe::Typed: the class fixes the token. USD has no prim type
    // for this kind, so the token is the erhe class name, written as a custom
    // typeName (doc/usd_compatibility.md).
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Style"; }

    // Overrides Dependency_object: every class's value properties are this
    // item's secondary properties.
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override;
};

// A name no style in the folder uses: `base_name`, else `base_name (N)`.
[[nodiscard]] auto make_unique_style_name(const Content_library_node& styles_folder, std::string_view base_name) -> std::string;

}
