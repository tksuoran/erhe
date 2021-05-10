#pragma once

#include "operations/mesh_operation.hpp"

namespace editor
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

class Ambo_operator
    : public Mesh_operation
{
public:
    explicit Ambo_operator(Context& context);

    virtual ~Ambo_operator() = default;
};

class Truncate_operator
    : public Mesh_operation
{
public:
    explicit Truncate_operator(Context& context);

    virtual ~Truncate_operator() = default;
};

class Snub_operator
    : public Mesh_operation
{
public:
    explicit Snub_operator(Context& context);

    virtual ~Snub_operator() = default;
};

class Merge_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>  scene_manager;
        std::shared_ptr<Selection_tool> selection_tool;
    };

    explicit Merge_operation(Context& context);

    // Implements IOperation
    void execute() override;
    void undo() override;

private:
    struct Source_entry
    {
        std::shared_ptr<erhe::scene::Mesh>      mesh;
        std::vector<erhe::primitive::Primitive> primitives;
    };
    Context                                              m_context;
    std::vector<Source_entry>                            m_source_entries;
    std::shared_ptr<erhe::primitive::Primitive_geometry> m_combined_primitive_geometry;
};

}
