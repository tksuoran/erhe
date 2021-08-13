#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::scene
{
    class Layer;
    class Scene;
};

namespace erhe::physics
{
    class World;
};

namespace editor
{

class Node_physics;
class Selection_tool;

class Node_transform_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        erhe::scene::Layer&                layer;
        erhe::scene::Scene&                scene;
        erhe::physics::World&              physics_world;
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Node::Transforms      before;
        erhe::scene::Node::Transforms      after;
    };

    explicit Node_transform_operation(const Context& context);
    ~Node_transform_operation        () override;

    // Implements IOperation
    void execute() override;
    void undo   () override;

private:
    Context m_context;
};

class Scene_item_operation
    : public IOperation
{
public:
    enum class Mode : unsigned int
    {
        insert = 0,
        remove
    };

    static auto inverse(const Mode mode) -> Mode
    {
        switch (mode)
        {
        case Mode::insert: return Mode::remove;
        case Mode::remove: return Mode::insert;
        default:
            FATAL("Bad Context::Mode");
            return Mode::insert;
        }
    }
};

class Mesh_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Context
    {
    public:
        std::shared_ptr<Selection_tool>    selection_tool;
        erhe::scene::Layer&                layer;
        erhe::scene::Scene&                scene;
        erhe::physics::World&              physics_world;
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<Node_physics>      node_physics;
        Mode                               mode;
    };

    explicit Mesh_insert_remove_operation(const Context& context);
    ~Mesh_insert_remove_operation        () override;

    // Implements IOperation
    void execute() override;
    void undo   () override;

private:
    void execute(const Mode mode);

    Context m_context;
};

class Light_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Context
    {
    public:
        erhe::scene::Layer&                 layer;
        erhe::scene::Scene&                 scene;
        std::shared_ptr<erhe::scene::Light> light;
        std::shared_ptr<erhe::scene::Node>  node;
        Mode                                mode;
    };

    explicit Light_insert_remove_operation(const Context& context);
    ~Light_insert_remove_operation        () override;

    void execute() override;
    void undo   () override;

private:
    void execute(const Mode mode);

    Context m_context;
};

}
