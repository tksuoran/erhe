#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(const Context& context);
    ~Catmull_clark_subdivision_operation        () override;

    auto describe() const -> std::string override;
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(const Context& context);
    ~Sqrt3_subdivision_operation        () override;

    auto describe() const -> std::string override;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(const Context& context);
    ~Triangulate_operation        () override;

    auto describe() const -> std::string override;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(const Context& context);

    auto describe() const -> std::string override;
};

class Gyro_operation
    : public Mesh_operation
{
public:
    explicit Gyro_operation(const Context& context);

    auto describe() const -> std::string override;
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(const Context& context);
    ~Dual_operator        () override;

    auto describe() const -> std::string override;
};

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(const Context& context);
    ~Ambo_operator        () override;

    auto describe() const -> std::string override;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(const Context& context);
    ~Truncate_operator        () override;

    auto describe() const -> std::string override;
};

class Reverse_operation
    : public Mesh_operation
{
public:
    explicit Reverse_operation(const Context& context);
    ~Reverse_operation        () override;

    auto describe() const -> std::string override;
};

}
