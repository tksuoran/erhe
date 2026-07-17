#pragma once

#include "assets/asset_reference.hpp"
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
    // R5.4 (asset-manager plan resolution 7, mechanism a): while recorded,
    // this operation is a DECLARED user of the material - request_unload
    // refuses naming it. Adopted at first execute (the constructor has no
    // Asset_manager; m_material carries the pin from construction on).
    Asset_reference                            m_usership;
};

}
