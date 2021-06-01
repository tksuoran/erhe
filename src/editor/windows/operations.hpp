#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Mesh_memory;
class Operation_stack;
class Selection_tool;
class Scene_root;

class Operations
    : public erhe::components::Component
    , public Window
{
public:
    static constexpr const char* c_name = "Operations";
    Operations() : erhe::components::Component{c_name} {}
    virtual ~Operations() = default;

    // Implements Component
    void connect() override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Mesh_memory>     m_mesh_memory;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Selection_tool>  m_selection_tool;
    std::shared_ptr<Scene_root>      m_scene_root;
};

} // namespace editor
