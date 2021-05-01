#ifndef clone_hpp_erhe_geometry_operation
#define clone_hpp_erhe_geometry_operation

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Clone
    : public Geometry_operation
{
public:
    explicit Clone(Geometry& src, Geometry& destination);
};

} // namespace erhe::geometry::operation

#endif
