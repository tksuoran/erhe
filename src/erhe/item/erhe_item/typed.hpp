#pragma once

#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace erhe {

// A typed prim of the one object model (doc/usd-compatibility-plan.md C5,
// USD `UsdTyped`): the level of the class hierarchy that carries the USD
// `typeName` token. It is instantiated as itself for a prim whose type has
// no erhe class of its own (`Cube`, `PointInstancer`, `SkelRoot`, a typeless
// `def`), so that prim's name, place in the tree and children survive a
// round trip.
//
// The token is the `type_name` property. It is bridged (D18), so it is
// always a local value of the prim: a class that fixes its own token
// (`get_class_type_name`) reports that constant and refuses every write,
// and a class that fixes none - a plain `Typed` - carries the token the
// importer sets in `m_prim_type_name`.
class Typed : public Item<Item_base, Hierarchy, Typed>
{
public:
    Typed();
    explicit Typed(const Typed& other);
    Typed& operator=(const Typed& other);
    explicit Typed(std::string_view name);
    Typed(std::string_view name, std::string_view prim_type_name);
    Typed(const Typed& src, for_clone);
    ~Typed() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Typed"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return Item_type::typed; }

    // The USD `typeName` token this CLASS fixes, empty when the class fixes
    // none and the token is authored per prim. A class whose token is fixed
    // reports it here and holds no value of its own.
    [[nodiscard]] virtual auto get_class_type_name() const -> std::string_view { return {}; }

    // The prim's `typeName` token: the class's fixed token when it has one,
    // the authored token otherwise (empty for a typeless `def`).
    [[nodiscard]] auto get_prim_type_name() const -> std::string_view;

    // Authors the token. Refused, with a logged error, on a class that fixes
    // its own token.
    void set_prim_type_name(std::string_view prim_type_name);

    // The `typeName` token as a bridged string property
    // (doc/property-system.md D18) over the accessors above.
    static const erhe::property::Property<std::string> type_name_property;

private:
    std::string m_prim_type_name{};
};

} // namespace erhe
