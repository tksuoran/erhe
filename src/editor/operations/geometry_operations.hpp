#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    Catmull_clark_subdivision_operation (Context&& context);
    ~Catmull_clark_subdivision_operation() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    Sqrt3_subdivision_operation (Context&& context);
    ~Sqrt3_subdivision_operation() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    Triangulate_operation (Context&& context);
    ~Triangulate_operation() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    Subdivide_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Gyro_operation
    : public Mesh_operation
{
public:
    Gyro_operation(Context&& context);

    [[nodiscard]] auto describe() const -> std::string override;
};

class Dual_operator
    : public Mesh_operation
{
public:
    Dual_operator (Context&& context);
    ~Dual_operator() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

class Ambo_operator
    : public Mesh_operation
{
public:
    Ambo_operator (Context&& context);
    ~Ambo_operator() override;

    auto describe() const -> std::string override;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    Truncate_operator (Context&& context);
    ~Truncate_operator() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

class Reverse_operation
    : public Mesh_operation
{
public:
    Reverse_operation (Context&& context);
    ~Reverse_operation() override;

    [[nodiscard]] auto describe() const -> std::string override;
};

}
