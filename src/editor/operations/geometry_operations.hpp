#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/compound_operation.hpp"

namespace editor {

class Catmull_clark_subdivision_operation : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context);
};

class Sqrt3_subdivision_operation : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Mesh_operation_parameters&& context);
};

class Triangulate_operation : public Mesh_operation
{
public:
    explicit Triangulate_operation(Mesh_operation_parameters&& context);
};

class Join_operation : public Mesh_operation
{
public:
    explicit Join_operation(Mesh_operation_parameters&& context);
};

class Kis_operation : public Mesh_operation
{
public:
    explicit Kis_operation(Mesh_operation_parameters&& context);
};

class Subdivide_operation : public Mesh_operation
{
public:
    explicit Subdivide_operation(Mesh_operation_parameters&& context);
};

class Meta_operation : public Mesh_operation
{
public:
    explicit Meta_operation(Mesh_operation_parameters&& context);
};

class Gyro_operation : public Mesh_operation
{
public:
    explicit Gyro_operation(Mesh_operation_parameters&& context);
};

class Chamfer_operation : public Mesh_operation
{
public:
    explicit Chamfer_operation(Mesh_operation_parameters&& context);
};

class Dual_operation : public Mesh_operation
{
public:
    explicit Dual_operation(Mesh_operation_parameters&& context);

};

class Ambo_operation : public Mesh_operation
{
public:
    explicit Ambo_operation(Mesh_operation_parameters&& context);
};

class Truncate_operation : public Mesh_operation
{
public:
    explicit Truncate_operation(Mesh_operation_parameters&& context);
};

class Reverse_operation : public Mesh_operation
{
public:
    explicit Reverse_operation(Mesh_operation_parameters&& context);
};

class Normalize_operation : public Mesh_operation
{
public:
    explicit Normalize_operation(Mesh_operation_parameters&& context);
};

class Generate_tangents_operation : public Mesh_operation
{
public:
    explicit Generate_tangents_operation(Mesh_operation_parameters&& context);
};

class Make_raytrace_operation : public Mesh_operation
{
public:
    explicit Make_raytrace_operation(Mesh_operation_parameters&& context);
};

class Bake_transform_operation : public Mesh_operation
{
public:
    explicit Bake_transform_operation(Mesh_operation_parameters&& context);
};

class Repair_operation : public Mesh_operation
{
public:
    explicit Repair_operation(Mesh_operation_parameters&& context);
};

class Weld_operation : public Mesh_operation
{
public:
    explicit Weld_operation(Mesh_operation_parameters&& context);
};

class Binary_mesh_operation : public Compound_operation
{
public:
    Binary_mesh_operation(
        Mesh_operation_parameters&& parameters,
        std::function<void(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs,
            erhe::geometry::Geometry&       result
        )> operation
    );

protected:
    auto make_operations(
        Mesh_operation_parameters&& parameters,
        std::function<void(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs,
            erhe::geometry::Geometry&       result
        )> operation
    ) -> Compound_operation::Parameters;
};

class Union_operation : public Binary_mesh_operation
{
public:
    explicit Union_operation(Mesh_operation_parameters&& parameters);
};

class Intersection_operation : public Binary_mesh_operation
{
public:
    explicit Intersection_operation(Mesh_operation_parameters&& parameters);
};

class Difference_operation : public Binary_mesh_operation
{
public:
    explicit Difference_operation(Mesh_operation_parameters&& parameters);
};

}
