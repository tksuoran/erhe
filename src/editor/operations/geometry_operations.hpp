#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/compound_operation.hpp"

namespace editor {

class Catmull_clark_subdivision_operation : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Sqrt3_subdivision_operation : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Triangulate_operation : public Mesh_operation
{
public:
    explicit Triangulate_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Join_operation : public Mesh_operation
{
public:
    explicit Join_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Kis_operation : public Mesh_operation
{
public:
    explicit Kis_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Subdivide_operation : public Mesh_operation
{
public:
    explicit Subdivide_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Meta_operation : public Mesh_operation
{
public:
    explicit Meta_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Gyro_operation : public Mesh_operation
{
public:
    explicit Gyro_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Chamfer_operation : public Mesh_operation
{
public:
    explicit Chamfer_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Dual_operation : public Mesh_operation
{
public:
    explicit Dual_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Ambo_operation : public Mesh_operation
{
public:
    explicit Ambo_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Truncate_operation : public Mesh_operation
{
public:
    explicit Truncate_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Reverse_operation : public Mesh_operation
{
public:
    explicit Reverse_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Normalize_operation : public Mesh_operation
{
public:
    explicit Normalize_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Generate_tangents_operation : public Mesh_operation
{
public:
    explicit Generate_tangents_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Bake_transform_operation : public Mesh_operation
{
public:
    explicit Bake_transform_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Repair_operation : public Mesh_operation
{
public:
    explicit Repair_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Weld_operation : public Mesh_operation
{
public:
    explicit Weld_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
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
    auto describe() const -> std::string override { return m_description; }

protected:
    auto make_operations(
        Mesh_operation_parameters&& parameters,
        std::function<void(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs,
            erhe::geometry::Geometry&       result
        )> operation
    ) -> Compound_operation::Parameters;
    std::string m_description;
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
