#include "operations/geometry_operations.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/ambo.hpp"
#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/dual.hpp"
#include "erhe/geometry/operation/gyro.hpp"
#include "erhe/geometry/operation/reverse.hpp"
#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/operation/truncate.hpp"

#include <sstream>

namespace editor
{

auto Catmull_clark_subdivision_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Catmull_clark " << Mesh_operation::describe();
    return ss.str();
}

Catmull_clark_subdivision_operation::~Catmull_clark_subdivision_operation() = default;

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::catmull_clark_subdivision);
}

auto Sqrt3_subdivision_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Sqrt3 " << Mesh_operation::describe();
    return ss.str();
}

Sqrt3_subdivision_operation::~Sqrt3_subdivision_operation() = default;

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::sqrt3_subdivision);
}

auto Triangulate_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Triangulate " << Mesh_operation::describe();
    return ss.str();
}

Triangulate_operation::~Triangulate_operation() = default;

Triangulate_operation::Triangulate_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::triangulate);
}

auto Subdivide_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Subdivide " << Mesh_operation::describe();
    return ss.str();
}

Subdivide_operation::Subdivide_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::subdivide);
}

auto Gyro_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Gyro " << Mesh_operation::describe();
    return ss.str();
}

Gyro_operation::Gyro_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::gyro);
}

auto Dual_operator::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Dual " << Mesh_operation::describe();
    return ss.str();
}

Dual_operator::~Dual_operator() = default;

Dual_operator::Dual_operator(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::dual);
}

auto Ambo_operator::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Ambo " << Mesh_operation::describe();
    return ss.str();
}

Ambo_operator::~Ambo_operator() = default;

Ambo_operator::Ambo_operator(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::ambo);
}

auto Truncate_operator::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Truncate " << Mesh_operation::describe();
    return ss.str();
}

Truncate_operator::~Truncate_operator() = default;

Truncate_operator::Truncate_operator(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::truncate);
}

auto Reverse_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Reverse " << Mesh_operation::describe();
    return ss.str();
}

Reverse_operation::~Reverse_operation() = default;

Reverse_operation::Reverse_operation(Context&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::reverse);
}

} // namespace editor
