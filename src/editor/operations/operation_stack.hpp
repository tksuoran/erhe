#pragma once

#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <memory>

namespace editor
{

class IOperation;
class Operation_stack;

class Undo_command
    : public Command
{
public:
    explicit Undo_command(Operation_stack& operation_stack)
        : Command          {"undo"}
        , m_operation_stack{operation_stack}
    {
    }

    auto try_call(Command_context& context) -> bool override;

private:
    Operation_stack& m_operation_stack;
};

class Redo_command
    : public Command
{
public:
    explicit Redo_command(Operation_stack& operation_stack)
        : Command{"redo"}
        , m_operation_stack{operation_stack} {}
    auto try_call(Command_context& context) -> bool override;
private:
    Operation_stack& m_operation_stack;
};

class Operation_stack
    : public erhe::components::Component
    , public Tool
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Operation_stack"};
    static constexpr std::string_view c_description{"Operations"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Operation_stack ();
    ~Operation_stack() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void initialize_component() override;

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

}
