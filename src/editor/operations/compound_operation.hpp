#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <vector>

namespace editor {

class Compound_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::vector<std::shared_ptr<Operation>> operations;
    };

    explicit Compound_operation(Parameters&& parameters);
    ~Compound_operation() noexcept override;

    // Implements Operation
    void execute (App_context& context) override;
    void undo    (App_context& context) override;

private:
    Parameters m_parameters;
};

}
