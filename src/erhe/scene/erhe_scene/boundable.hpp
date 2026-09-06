#pragma once

#include "erhe_scene/node.hpp"

#include <cstdint>
#include <string_view>

namespace erhe::scene {

// A boundable prim (doc/usd-compatibility-plan.md C5, USD
// `UsdGeomBoundable`): the level of the prim class hierarchy that has an
// extent, and so the level `extent` belongs to. It holds nothing until a
// step moves the bounds here.
//
// The level is never instantiated on its own: it exists so `Gprim` and,
// through it, every geometric prim sits below it.
class Boundable
    : public erhe::Item<
        Item_base,
        Xformable,
        Boundable,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Boundable();
    explicit Boundable(const Boundable& src);
    Boundable& operator=(const Boundable& src);
    explicit Boundable(std::string_view name);
    Boundable(const Boundable& src, for_clone);
    ~Boundable() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Boundable"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Xformable::get_static_type() | erhe::Item_type::boundable;
    }
};

} // namespace erhe::scene
