#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

class Node_attach_operation;

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
        switch (mode) {
            //using enum Mode;
            case Mode::insert: return Mode::remove;
            case Mode::remove: return Mode::insert;
            default: {
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
        std::shared_ptr<erhe::scene::Node> node;
        std::shared_ptr<erhe::scene::Node> parent;
        Mode                               mode;
    };

    explicit Node_insert_remove_operation(const Parameters& parameters);
    ~Node_insert_remove_operation() noexcept override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute() override;
    void undo   () override;

private:
    Mode                                                m_mode;
    std::shared_ptr<erhe::scene::Node>                  m_node;
    std::shared_ptr<erhe::scene::Node>                  m_before_parent;
    std::shared_ptr<erhe::scene::Node>                  m_after_parent;
    std::vector<std::shared_ptr<Node_attach_operation>> m_parent_changes;

    erhe::scene::Scene_host*                            m_scene_host;
    std::vector<std::shared_ptr<erhe::scene::Item>>     m_selection_before;
    std::vector<std::shared_ptr<erhe::scene::Item>>     m_selection_after;
};

}
