#include "operations/geometry_operations.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/operation/ambo.hpp"
#include "erhe/geometry/operation/catmull_clark_subdivision.hpp"
#include "erhe/geometry/operation/dual.hpp"
#include "erhe/geometry/operation/gyro.hpp"
#include "erhe/geometry/operation/join.hpp"
#include "erhe/geometry/operation/kis.hpp"
#include "erhe/geometry/operation/meta.hpp"
#include "erhe/geometry/operation/normalize.hpp"
#include "erhe/geometry/operation/reverse.hpp"
#include "erhe/geometry/operation/sqrt3_subdivision.hpp"
#include "erhe/geometry/operation/subdivide.hpp"
#include "erhe/geometry/operation/triangulate.hpp"
#include "erhe/geometry/operation/truncate.hpp"
#include "erhe/geometry/operation/weld.hpp"

#include <fmt/format.h>

namespace editor
{

auto Catmull_clark_subdivision_operation::describe() const -> std::string
{
    return fmt::format("Catmull_clark {}", Mesh_operation::describe());
}

Catmull_clark_subdivision_operation::Catmull_clark_subdivision_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::catmull_clark_subdivision);
}

auto Sqrt3_subdivision_operation::describe() const -> std::string
{
    return fmt::format("Sqrt3 {}", Mesh_operation::describe());
}

Sqrt3_subdivision_operation::Sqrt3_subdivision_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::sqrt3_subdivision);
}

auto Triangulate_operation::describe() const -> std::string
{
    return fmt::format("Triangulate {}", Mesh_operation::describe());
}

Triangulate_operation::Triangulate_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::triangulate);
}

auto Join_operation::describe() const -> std::string
{
    return fmt::format("Join {}", Mesh_operation::describe());
}

Join_operation::Join_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::join);
}

auto Kis_operation::describe() const -> std::string
{
    return fmt::format("Kis {}", Mesh_operation::describe());
}

Kis_operation::Kis_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::kis);
}

auto Subdivide_operation::describe() const -> std::string
{
    return fmt::format("Subdivide {}", Mesh_operation::describe());
}

Subdivide_operation::Subdivide_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::subdivide);
}

auto Meta_operation::describe() const -> std::string
{
    return fmt::format("Meta {}", Mesh_operation::describe());
}

Meta_operation::Meta_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::meta);
}

auto Gyro_operation::describe() const -> std::string
{
    return fmt::format("Gyro {}", Mesh_operation::describe());
}

Gyro_operation::Gyro_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::gyro);
}

auto Dual_operator::describe() const -> std::string
{
    return fmt::format("Dual {}", Mesh_operation::describe());
}

Dual_operator::Dual_operator(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::dual);
}

auto Ambo_operator::describe() const -> std::string
{
    return fmt::format("Ambo {}", Mesh_operation::describe());
}

Ambo_operator::Ambo_operator(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::ambo);
}

auto Truncate_operator::describe() const -> std::string
{
    return fmt::format("Truncate {}", Mesh_operation::describe());
}

Truncate_operator::Truncate_operator(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::truncate);
}

auto Reverse_operation::describe() const -> std::string
{
    return fmt::format("Reverse {}", Mesh_operation::describe());
}

Reverse_operation::Reverse_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::reverse);
}

auto Normalize_operation::describe() const -> std::string
{
    return fmt::format("Normalize {}", Mesh_operation::describe());
}

Normalize_operation::Normalize_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::normalize);
}

auto Weld_operation::describe() const -> std::string
{
    return fmt::format("Weld {}", Mesh_operation::describe());
}

Weld_operation::Weld_operation(Parameters&& context)
    : Mesh_operation{std::move(context)}
{
    make_entries(erhe::geometry::operation::weld);
}

} // namespace editor
