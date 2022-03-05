#pragma once

#include "operations/ioperation.hpp"

#include "erhe/scene/node.hpp"

#include <vector>

namespace erhe::scene
{
    class Scene;
}

namespace editor
{

class Selection_tool;

class Node_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        erhe::scene::Scene& scene;
        Selection_tool*     selection_tool;
    };

protected:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Node> node;
        erhe::scene::Node_data             before;
        erhe::scene::Node_data             after;
    };

    explicit Node_operation(Context&& context);
    ~Node_operation() override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute () const override;
    void undo    () const override;

    // Public API
    void add_entry   (Entry&& entry);
    void make_entries();

private:
    Selection_tool*    m_selection_tool{nullptr};

    Context            m_context;
    std::vector<Entry> m_entries;
};

}
