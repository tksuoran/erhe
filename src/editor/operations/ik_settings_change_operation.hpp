#pragma once

#include "operations/operation.hpp"
#include "scene/node_ik_settings.hpp"

#include <memory>

namespace editor {

// One completed Properties edit session of an Ik_settings attachment =
// one undoable before/after value swap (the Material_change_operation
// pattern; see doc/ik-settings-requirements.md section 5).
class Ik_settings_change_operation : public Operation
{
public:
    Ik_settings_change_operation(
        const std::shared_ptr<Ik_settings>& ik_settings,
        const Ik_settings_data&             before,
        const Ik_settings_data&             after
    );
    ~Ik_settings_change_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

protected:
    std::shared_ptr<Ik_settings> m_ik_settings;
    Ik_settings_data             m_before;
    Ik_settings_data             m_after;
};

}
