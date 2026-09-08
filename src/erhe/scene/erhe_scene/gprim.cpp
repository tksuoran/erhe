#include "erhe_scene/gprim.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

// USD `doubleSided` (doc/usd_compatibility.md, geometry attributes). It
// inherits like the other render-state properties, so a node or a style can
// hold `Gprim.double_sided` for the geometry below it (D30).
const erhe::property::Property<bool> Gprim::double_sided_property = erhe::property::Property<bool>::register_property(
    "double_sided", Gprim::property_owner_type(),
    erhe::property::Property_metadata{
        .default_value    = false,
        .property_changed = Gprim::on_render_state_property_changed,
        .inherits         = true,
        .ui               = erhe::property::Property_ui{.group = "Rendering", .label = "Double Sided"}
    }
);

// The callback runs on geometry prims only: deliver skips a metadata callback
// on a holder (a node or a style holding Gprim.double_sided, D30).
void Gprim::on_render_state_property_changed(
    erhe::property::Dependency_object&           object,
    const erhe::property::Property_changed_args&
)
{
    static_cast<Gprim&>(object).handle_gprim_render_state_changed();
}

Gprim::Gprim()           = default;
Gprim::~Gprim() noexcept = default;

// See Xform: the transform level's copy is not a clone.
Gprim::Gprim(const Gprim&) { ERHE_FATAL("TODO"); }
Gprim& Gprim::operator=(const Gprim&) { ERHE_FATAL("TODO"); }

Gprim::Gprim(const std::string_view name)
    : Item{name}
{
}

Gprim::Gprim(const Gprim& src, for_clone)
    : Item{src, erhe::for_clone{}}
{
}

} // namespace erhe::scene
