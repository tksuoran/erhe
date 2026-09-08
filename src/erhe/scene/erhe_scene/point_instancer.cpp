#include "erhe_scene/point_instancer.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

Point_instancer::Point_instancer()           = default;
Point_instancer::~Point_instancer() noexcept = default;

// See Xform: the transform level owns attachments and a scene host, so a
// plain copy is not a clone.
Point_instancer::Point_instancer(const Point_instancer&) { ERHE_FATAL("TODO"); }
Point_instancer& Point_instancer::operator=(const Point_instancer&) { ERHE_FATAL("TODO"); }

Point_instancer::Point_instancer(const std::string_view name)
    : Item{name}
{
}

Point_instancer::Point_instancer(const Point_instancer& src, for_clone)
    : Item{src, erhe::for_clone{}}
{
}

} // namespace erhe::scene
