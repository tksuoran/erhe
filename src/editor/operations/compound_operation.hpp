#pragma once

#include "operations/ioperation.hpp"

#include <string>
#include <vector>

namespace editor {

class Compound_operation
    : public IOperation
{
public:
    class Context
    {
    public:
        std::vector<std::shared_ptr<IOperation>> operations;
    };

    explicit Compound_operation(const Context& context);
    ~Compound_operation        () override;

    // Implements IOperation
    void execute () const override;
    void undo    () const override;
    auto describe() const -> std::string override;

private:
    Context m_context;
};

}