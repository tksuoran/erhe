#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace editor
{

class Mesh_memory;
class Operation_stack;
class Pointer_context;
class Selection_tool;
class Scene_root;

class Operations
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Operations"};
    static constexpr std::string_view c_title{"Operations"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Operations ();
    ~Operations() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Window
    void imgui() override;

private:
    auto count_selected_meshes() const -> size_t;

    Mesh_memory*     m_mesh_memory    {nullptr};
    Operation_stack* m_operation_stack{nullptr};
    Pointer_context* m_pointer_context{nullptr};
    Scene_root*      m_scene_root     {nullptr};
    Selection_tool*  m_selection_tool {nullptr};
};

} // namespace editor
