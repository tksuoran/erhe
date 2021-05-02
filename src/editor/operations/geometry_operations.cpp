#include "operations/geometry_operations.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/operation/conway_dual_operator.hpp"
#include "erhe/geometry/operation/conway_ambo_operator.hpp"
#include "erhe/geometry/operation/conway_truncate_operator.hpp"

namespace editor
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

Ambo_operator::Ambo_operator(Context& context)
{
    make_entries(context, erhe::geometry::operation::ambo);
}

Truncate_operator::Truncate_operator(Context& context)
{
    make_entries(context, erhe::geometry::operation::truncate);
}

} // namespace editor
