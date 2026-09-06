#include "erhe_scene/boundable.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

Boundable::Boundable()           = default;
Boundable::~Boundable() noexcept = default;

// See Xform: the transform level's copy is not a clone.
Boundable::Boundable(const Boundable&) { ERHE_FATAL("TODO"); }
Boundable& Boundable::operator=(const Boundable&) { ERHE_FATAL("TODO"); }

Boundable::Boundable(const std::string_view name)
    : Item{name}
{
}

Boundable::Boundable(const Boundable& src, for_clone)
    : Item{src, erhe::for_clone{}}
{
}

} // namespace erhe::scene
