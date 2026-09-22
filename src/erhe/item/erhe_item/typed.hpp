#pragma once

#include "erhe_item/composition_arc.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/dependency_property.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe {

// A typed prim of the one object model (doc/erhe/usd_compatibility_design.md C5,
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
    // (doc/erhe/property_system.md D18) over the accessors above.
    static const erhe::property::Property<std::string> type_name_property;

    // The composition arcs applied to this prim, in authored order
    // (doc/erhe/item.md "Composition arcs"). A prim carrying none holds no
    // storage at all, so the carrier test is a null check and a prim without
    // arcs costs one pointer. The list sits on `Typed` rather than a
    // transformable level because a USD arc is applied to a prim of any type
    // - a `Scope`, a `Material`, a typeless `def`.
    [[nodiscard]] auto has_composition_arcs() const -> bool;
    [[nodiscard]] auto get_composition_arcs() const -> std::span<const Composition_arc>;

    // Authors the list. An empty list releases the storage.
    void set_composition_arcs(std::vector<Composition_arc> arcs);

    // The list as read-only computed text (doc/erhe/property_system.md D26,
    // section 4.27), one line per arc; the row is shown only while the prim
    // carries an arc.
    static const erhe::property::Property<std::string> composition_arcs_property;

    // Overrides Hierarchy: a prim's item host is the host of the prim it is
    // parented to, so attaching a prim anywhere in a hosted tree carries the
    // host to every prim below it, and detaching it takes the host away
    // again. The hook lives at this level so a prim with no transform - a
    // `Scope` - carries the host through to the transformable prims below it
    // (doc/erhe/usd_compatibility_design.md C5).
    void handle_parent_update(Hierarchy* old_parent, Hierarchy* new_parent) override;

    // The prim's item host changed: adopt the new host and carry it down the
    // subtree. `erhe::scene::Xformable` overrides this with the scene
    // registration a transformable prim needs.
    virtual void handle_item_host_update(Item_host* old_item_host, Item_host* new_item_host);

private:
    std::string m_prim_type_name{};

    // Null while the prim carries no arc; never an empty list.
    std::unique_ptr<std::vector<Composition_arc>> m_composition_arcs;
};

} // namespace erhe
