#pragma once

#include "operations/ioperation.hpp"

#include <vector>

namespace editor {

class Compound_operation
    : public IOperation
{
public:
    struct Context
    {
        std::vector<std::shared_ptr<IOperation>> operations;
    };

    explicit Compound_operation(const Context& context);
    ~Compound_operation        () override;

    // Implements IOperation
    void execute() override;
    void undo   () override;

private:
    Context m_context;
};

}