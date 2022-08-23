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
    explicit Undo_command(Operation_stack& operation_stack)
        : Command          {"undo"}
        , m_operation_stack{operation_stack}
    {
    }
    //~Undo_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Operation_stack& m_operation_stack;
};

class Redo_command
    : public erhe::application::Command
{
public:
    explicit Redo_command(Operation_stack& operation_stack)
        : Command{"redo"}
        , m_operation_stack{operation_stack} {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Operation_stack& m_operation_stack;
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

    // Implements Tool
    [[nodiscard]] auto description() -> const char* override;

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

} // namespace editor
