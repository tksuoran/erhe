#include "erhe_scene/gprim.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

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
