#pragma once

#include "erhe/application/commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace erhe::application {

class Command;
class Command_context;

class Update_binding
    : public Command_binding
{
public:
    explicit Update_binding(Command* const command);
    ~Update_binding() noexcept override;

    Update_binding();
    Update_binding(const Update_binding&) = delete;
    Update_binding(Update_binding&& other) noexcept;
    auto operator=(const Update_binding&) -> Update_binding& = delete;
    auto operator=(Update_binding&& other) noexcept -> Update_binding&;

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Update; }

    virtual auto on_update(Command_context& context) -> bool;
};


} // namespace erhe::application

