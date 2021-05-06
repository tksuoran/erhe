#pragma once

#include "operations/ioperation.hpp"
#include "erhe/scene/mesh.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace editor
{

class Mesh_remove_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>     scene_manager;
        std::shared_ptr<erhe::scene::Mesh> mesh;
    };

    Mesh_insert_operation(Context& context);

    void execute() override;
    void undo() override;

private:
    Context m_context;
};

class Light_insert_operation
    : public IOperation
{
public:
    struct Context
    {
        std::shared_ptr<Scene_manager>      scene_manager;
        std::shared_ptr<erhe::scene::Light> light;
    };

    Light_insert_operation(Context& context);

    void execute() override;
    void undo() override;

private:
    Context m_context;
};

}
