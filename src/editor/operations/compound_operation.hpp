#pragma once

#include "operations/ioperation.hpp"

#include <memory>
#include <string>
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
    auto describe() const -> std::string override;
    void execute (Editor_context& context) override;
    void undo    (Editor_context& context) override;

private:
    Parameters m_parameters;
};

} // namespace editor
