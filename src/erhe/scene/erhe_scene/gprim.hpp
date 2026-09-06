#pragma once

#include "erhe_scene/boundable.hpp"

#include <cstdint>
#include <string_view>

namespace erhe::scene {

// A geometric prim (doc/usd-compatibility-plan.md C5, USD `UsdGeomGprim`):
// the level of the prim class hierarchy that draws geometry, and so the
// level `doubleSided` and `displayColor` belong to. It holds nothing until a
// step moves them here.
//
// The level is never instantiated on its own: it exists so `Mesh` and its
// siblings sit below it.
class Gprim
    : public erhe::Item<
        Item_base,
        Boundable,
        Gprim,
        erhe::Item_kind::clone_using_custom_clone_constructor
    >
{
public:
    Gprim();
    explicit Gprim(const Gprim& src);
    Gprim& operator=(const Gprim& src);
    explicit Gprim(std::string_view name);
    Gprim(const Gprim& src, for_clone);
    ~Gprim() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Gprim"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return Boundable::get_static_type() | erhe::Item_type::gprim;
    }
};

} // namespace erhe::scene
