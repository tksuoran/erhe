#pragma once

#include "erhe_item/typed.hpp"
#include "erhe_property/owner_type.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace erhe {

// A `Scope` prim (doc/usd-compatibility-plan.md C5, USD `Scope`): children
// and nothing else - no transform exists on it, so a transform composes
// through it to the nearest transformable ancestor. It is the prim
// resources are conventionally gathered under.
//
// Its secondary property owner type (doc/property-system.md D30) is the root
// owner type, as an editor `Style` item's is, so a scope holds any class's
// value properties by qualified name (`Material.roughness` on a materials
// scope) and its descendants inherit them - the content-library folder rule.
class Scope : public Item<Item_base, Typed, Scope>
{
public:
    Scope();
    explicit Scope(const Scope& other);
    Scope& operator=(const Scope& other);
    explicit Scope(std::string_view name);
    ~Scope() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Scope"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Item_type::typed | Item_type::scope; }

    // Overrides Typed: the class fixes the token.
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Scope"; }

    // Overrides Dependency_object: every class's value properties are this
    // prim's secondary properties.
    [[nodiscard]] auto get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type> override;
};

} // namespace erhe
