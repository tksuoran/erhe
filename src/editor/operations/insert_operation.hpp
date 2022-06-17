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
    class Light_layer;
    class Mesh_layer;
    class Scene;
};

namespace erhe::physics
{
    class IWorld;
};

namespace erhe::raytrace
{
    class IScene;
};

namespace editor
{

class Node_physics;
class Node_raytrace;
class Selection_tool;

class Node_transform_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
        erhe::scene::Mesh_layer&           layer;
        erhe::scene::Scene&                scene;
        erhe::physics::IWorld&             physics_world;
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Transform             parent_from_node_before;
        erhe::scene::Transform             parent_from_node_after;
    };

    explicit Node_transform_operation(const Parameters& parameters);
    ~Node_transform_operation() noexcept override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    Parameters m_parameters;
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
            //using enum Mode;
            case Mode::insert: return Mode::remove;
            case Mode::remove: return Mode::insert;
            default:
            {
                ERHE_FATAL("Bad Context::Mode %04x", static_cast<unsigned int>(mode));
                // unreachable return Mode::insert;
            }
        }
    }
};

class Mesh_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Parameters
    {
    public:
        erhe::scene::Scene&                scene;
        erhe::scene::Mesh_layer&           layer;
        erhe::physics::IWorld&             physics_world;
        erhe::raytrace::IScene&            raytrace_scene;
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::shared_ptr<Node_physics>      node_physics;
        std::shared_ptr<Node_raytrace>     node_raytrace;
        std::shared_ptr<erhe::scene::Node> parent;
        Mode                               mode;
    };

    explicit Mesh_insert_remove_operation(const Parameters& parameters);
    ~Mesh_insert_remove_operation() noexcept override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    void execute(
        const Operation_context& context,
        const Mode               mode
    ) const;

    Parameters m_parameters;
};

class Light_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Parameters
    {
    public:
        erhe::scene::Scene&                 scene;
        erhe::scene::Light_layer&           layer;
        std::shared_ptr<erhe::scene::Light> light;
        std::shared_ptr<erhe::scene::Node>  parent;
        Mode                                mode;
    };

    explicit Light_insert_remove_operation(const Parameters& parameters);
    ~Light_insert_remove_operation() noexcept override;

    // Public API
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    void execute(
        const Operation_context& context,
        const Mode               mode
    );

    Parameters                                      m_parameters;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_selection_after;
};

class Camera_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Parameters
    {
    public:
        erhe::scene::Scene&                  scene;
        std::shared_ptr<erhe::scene::Camera> camera;
        std::shared_ptr<erhe::scene::Node>   parent;
        Mode                                 mode;
    };

    explicit Camera_insert_remove_operation(const Parameters& parameters);
    ~Camera_insert_remove_operation() noexcept override;

    // Public API
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    void execute(
        const Operation_context& context,
        const Mode               mode
    ) const;

    Parameters                                      m_parameters;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Node>> m_selection_after;
};

}
