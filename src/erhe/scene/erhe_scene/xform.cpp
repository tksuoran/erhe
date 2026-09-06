#include "erhe_scene/xform.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

Xform::Xform()           = default;
Xform::~Xform() noexcept = default;

// The transform level owns attachments and a scene host, so a plain copy is
// not a clone; Xform(src, for_clone) is the clone path.
Xform::Xform(const Xform&) { ERHE_FATAL("TODO"); }
Xform& Xform::operator=(const Xform&) { ERHE_FATAL("TODO"); }

Xform::Xform(const std::string_view name)
    : Item{name}
{
}

Xform::Xform(const Xform& src, for_clone)
    : Item{src, erhe::for_clone{}}
{
}

} // namespace erhe::scene
