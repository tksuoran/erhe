#pragma once

#include "operations/mesh_operation.hpp"

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

class Weld_operation : public Mesh_operation
{
public:
    explicit Weld_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

class Repair_operation : public Mesh_operation
{
public:
    explicit Repair_operation(Mesh_operation_parameters&& context);
    auto describe() const -> std::string override;
};

} // namespace editor
