#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

class Mesh_memory;
class Operation_stack;
class Pointer_context;
class Selection_tool;
class Scene_root;
class Tool;

class Operations
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Operations"};
    static constexpr std::string_view c_title{"Operations"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Operations ();
    ~Operations() noexcept override;

    // Implements Component
    [[nodiscard]]
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Window
    void imgui() override;

    // Public API
    [[nodiscard]] auto get_active_tool() const -> Tool*;
    void register_active_tool(Tool* tool);

private:
    [[nodiscard]]auto count_selected_meshes() const -> size_t;

    // Component dependencies
    std::shared_ptr<Mesh_memory>     m_mesh_memory;
    std::shared_ptr<Operation_stack> m_operation_stack;
    std::shared_ptr<Pointer_context> m_pointer_context;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Selection_tool>  m_selection_tool;

    Tool*              m_current_active_tool{nullptr};
    std::vector<Tool*> m_active_tools;
};

} // namespace editor
