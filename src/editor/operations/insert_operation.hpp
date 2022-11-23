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

class Node_attach_operation;
class Node_physics;
class Node_raytrace;
class Scene_root;
class Selection_tool;

class Node_transform_operation
    : public IOperation
{
public:
    class Parameters
    {
    public:
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

class Node_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Parameters
    {
    public:
        Selection_tool*                    selection_tool{nullptr};
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> parent;
        Mode                               mode;
    };

    class Entry
    {
    public:
    };

    explicit Node_insert_remove_operation(const Parameters& parameters);
    ~Node_insert_remove_operation() noexcept override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(const Operation_context& context) override;
    void undo   (const Operation_context& context) override;

private:
    Mode                                                m_mode;
    std::shared_ptr<erhe::scene::Node>                  m_node;
    std::shared_ptr<erhe::scene::Node>                  m_before_parent;
    std::shared_ptr<erhe::scene::Node>                  m_after_parent;
    std::vector<std::shared_ptr<Node_attach_operation>> m_parent_changes;

    erhe::scene::Scene_host*                            m_scene_host;
    std::vector<std::shared_ptr<erhe::scene::Node>>     m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Node>>     m_selection_after;
};

}
