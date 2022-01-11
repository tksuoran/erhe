#pragma once

#include "operations/ioperation.hpp"

#include <memory>
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

    explicit Compound_operation(Context&& context);
    ~Compound_operation        () override;

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute () const override;
    void undo    () const override;

private:
    Context m_context;
};

}
