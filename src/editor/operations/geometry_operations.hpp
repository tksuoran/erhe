#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
{

class Catmull_clark_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(const Context& context);

    virtual ~Catmull_clark_subdivision_operation();
};

class Sqrt3_subdivision_operation
    : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(const Context& context);

    virtual ~Sqrt3_subdivision_operation();
};

class Triangulate_operation
    : public Mesh_operation
{
public:
    explicit Triangulate_operation(const Context& context);

    virtual ~Triangulate_operation();
};

class Subdivide_operation
    : public Mesh_operation
{
public:
    explicit Subdivide_operation(const Context& context);

    virtual ~Subdivide_operation();
};

class Dual_operator
    : public Mesh_operation
{
public:
    explicit Dual_operator(const Context& context);

    virtual ~Dual_operator();
};

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(const Context& context);

    virtual ~Ambo_operator();
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(const Context& context);

    virtual ~Truncate_operator();
};

}
