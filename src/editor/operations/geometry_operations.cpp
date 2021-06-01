#include "operations/geometry_operations.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/operation/dual.hpp"
#include "erhe/geometry/operation/ambo.hpp"
#include "erhe/geometry/operation/truncate.hpp"

namespace editor
{

Catmull_clark_subdivision_operation::~Catmull_clark_subdivision_operation() = default;

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(const Context& context)
{
    make_entries(context, erhe::geometry::operation::catmull_clark_subdivision);
}

Sqrt3_subdivision_operation::~Sqrt3_subdivision_operation() = default;

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(const Context& context)
{
    make_entries(context, erhe::geometry::operation::sqrt3_subdivision);
}

Triangulate_operation::~Triangulate_operation() = default;

Triangulate_operation::Triangulate_operation(const Context& context)
{
    make_entries(context, erhe::geometry::operation::triangulate);
}

Subdivide_operation::~Subdivide_operation() = default;

Subdivide_operation::Subdivide_operation(const Context& context)
{
    make_entries(context, erhe::geometry::operation::subdivide);
}

Dual_operator::~Dual_operator() = default;

Dual_operator::Dual_operator(const Context& context)
{
    make_entries(context, erhe::geometry::operation::dual);
}

Ambo_operator::~Ambo_operator() = default;

Ambo_operator::Ambo_operator(const Context& context)
{
    make_entries(context, erhe::geometry::operation::ambo);
}

Truncate_operator::~Truncate_operator() = default;

Truncate_operator::Truncate_operator(const Context& context)
{
    make_entries(context, erhe::geometry::operation::truncate);
}

} // namespace editor
