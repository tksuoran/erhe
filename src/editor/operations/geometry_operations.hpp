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
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(const Context& context);
    ~Sqrt3_subdivision_operation        () override;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(const Context& context);
    ~Triangulate_operation        () override;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(const Context& context);
    ~Subdivide_operation        () override;
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(const Context& context);
    ~Dual_operator        () override;
};

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(const Context& context);
    ~Ambo_operator        () override;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(const Context& context);
    ~Truncate_operator        () override;
};

}
