#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <vector>

namespace editor {

// What the first execute of a Compound_operation does when a child is in
// error after its own first execute (doc/editor/operations.md
// "Compound_operation").
enum class Compound_child_error : unsigned int {
    // The child stays inert inside the compound, its siblings stay applied
    // and the compound is recorded.
    keep_siblings = 0,
    // The children executed before it are undone in reverse order and the
    // compound takes the child's error, so Operation_stack does not record
    // it: the compound is all or nothing.
    roll_back     = 1
};

class Compound_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::vector<std::shared_ptr<Operation>> operations;
        Compound_child_error                    child_error{Compound_child_error::keep_siblings};
    };

    explicit Compound_operation(Parameters&& parameters);
    ~Compound_operation() noexcept override;

    // Implements Operation
    void execute (App_context& context) override;
    void undo    (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    Parameters m_parameters;
    bool       m_executed{false};
};

}
