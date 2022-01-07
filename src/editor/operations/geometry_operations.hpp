#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Gyro_operation
    : public Mesh_operation
{
public:
    explicit Gyro_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(Context&& context);

    auto describe() const -> std::string override;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Reverse_operation
    : public Mesh_operation
{
public:
    explicit Reverse_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

}
