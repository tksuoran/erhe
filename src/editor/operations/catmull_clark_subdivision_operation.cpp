#include "operations/catmull_clark_subdivision_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/operation/conway_dual_operator.hpp"

namespace sample
{

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Context& context)
{
    make_entries(context, erhe::geometry::operation::catmull_clark_subdivision);
}

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Context& context)
{
    make_entries(context, erhe::geometry::operation::sqrt3_subdivision);
}

Triangulate_operation::Triangulate_operation(Context& context)
{
    make_entries(context, erhe::geometry::operation::triangulate);
}

Subdivide_operation::Subdivide_operation(Context& context)
{
    make_entries(context, erhe::geometry::operation::subdivide);
}

Dual_operator::Dual_operator(Context& context)
{
    make_entries(context, erhe::geometry::operation::dual);
}

} // namespace sample   
