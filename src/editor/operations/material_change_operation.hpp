#pragma once

#include "operations/operation.hpp"
#include "erhe_primitive/material.hpp"

namespace editor {

class App_context;

class Material_change_operation : public Operation
{
public:
    explicit Material_change_operation(
        const std::shared_ptr<erhe::primitive::Material>& material,
        const erhe::primitive::Material_data&             before,
        const erhe::primitive::Material_data&             after
    );
    ~Material_change_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

protected:
    std::shared_ptr<erhe::primitive::Material> m_material{};
    erhe::primitive::Material_data             m_before{};
    erhe::primitive::Material_data             m_after{};
};

}
