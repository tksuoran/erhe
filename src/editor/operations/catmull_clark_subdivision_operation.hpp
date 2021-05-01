#pragma once

#include "operations/mesh_operation.hpp"

namespace sample
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Context& context);

    virtual ~Catmull_clark_subdivision_operation() = default;
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Context& context);

    virtual ~Sqrt3_subdivision_operation() = default;
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(Context& context);

    virtual ~Triangulate_operation() = default;
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(Context& context);

    virtual ~Subdivide_operation() = default;
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(Context& context);

    virtual ~Dual_operator() = default;
};

}
