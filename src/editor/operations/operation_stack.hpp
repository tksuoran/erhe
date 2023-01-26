#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

class IOperation;
class Operation_stack;

class Undo_command
    : public erhe::application::Command
{
public:
    Undo_command();

    auto try_call(erhe::application::Input_arguments& input) -> bool override;
};

class Redo_command
    : public erhe::application::Command
{
public:
    Redo_command();

    auto try_call(erhe::application::Input_arguments& input) -> bool override;
};

class Operation_stack
    : public erhe::components::Component
    , public Tool
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Operation_stack"};
    static constexpr std::string_view c_title{"Operation Stack"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Operation_stack ();
    ~Operation_stack() noexcept override;

    // Implements Component
    auto get_type_hash              () const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Window
    void imgui() override;

    // Public API
    [[nodiscard]] auto can_undo() const -> bool;
    [[nodiscard]] auto can_redo() const -> bool;
    void push(const std::shared_ptr<IOperation>& operation);
    void undo();
    void redo();

private:
    void imgui(
        const char*                                     stack_label,
        const std::vector<std::shared_ptr<IOperation>>& operations
    );

    Undo_command                             m_undo_command;
    Redo_command                             m_redo_command;

    std::vector<std::shared_ptr<IOperation>> m_executed;
    std::vector<std::shared_ptr<IOperation>> m_undone;
};

extern Operation_stack* g_operation_stack;

} // namespace editor
