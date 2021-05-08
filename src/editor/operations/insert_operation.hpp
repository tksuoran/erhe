#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace editor
{

class Node_transform_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>     scene_manager;
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Node::Transforms      before;
        erhe::scene::Node::Transforms      after;
    };

    explicit Node_transform_operation(Context context);

    void execute() override;
    void undo() override;

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

    static auto inverse(Mode mode) -> Mode
    {
        switch (mode)
        {
        case Mode::insert: return Mode::remove;
        case Mode::remove: return Mode::insert;
        default: FATAL("Bad Context::Mode");
            return Mode::insert;
        }
    }
};

class Mesh_insert_remove_operation
    : public Scene_item_operation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>     scene_manager;
        std::shared_ptr<erhe::scene::Mesh> item;
        Mode                               mode;
    };

    explicit Mesh_insert_remove_operation(Context& context);

    void execute() override;
    void undo() override;

private:
    void execute(Mode mode);

    Context m_context;
};

class Light_insert_remove_operation
    : public Scene_item_operation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>      scene_manager;
        std::shared_ptr<erhe::scene::Light> item;
        Mode                                mode;
    };

    explicit Light_insert_remove_operation(Context& context);

    void execute() override;
    void undo() override;

private:
    void execute(Mode mode);

    Context m_context;
};

}
