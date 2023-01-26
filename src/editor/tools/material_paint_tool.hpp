#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Material_paint_command
    : public erhe::application::Command
{
public:
    explicit Material_paint_command();

    auto try_call (erhe::application::Input_arguments& input) -> bool override;
    void try_ready(erhe::application::Input_arguments& input) override;
};

class Material_pick_command
    : public erhe::application::Command
{
public:
    explicit Material_pick_command();

    auto try_call (erhe::application::Input_arguments& input) -> bool override;
    void try_ready(erhe::application::Input_arguments& input) override;
};

class Material_paint_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority{2};
    static constexpr std::string_view c_type_name   {"Material_paint_tool"};
    static constexpr std::string_view c_title   {"Material Paint"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Material_paint_tool();
    ~Material_paint_tool();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void tool_properties() override;

    // Commands
    void set_active_command(const int command);

    auto on_paint_ready() -> bool;
    auto on_paint      () -> bool;

    auto on_pick_ready() -> bool;
    auto on_pick      () -> bool;

private:
    // Commands
    Material_paint_command m_paint_command;
    Material_pick_command  m_pick_command;

    static const int c_command_paint{0};
    static const int c_command_pick {1};

    int m_active_command{c_command_paint};

    std::shared_ptr<erhe::primitive::Material> m_material;
    //std::shared_ptr<erhe::scene::Mesh>         m_target_mesh;
    //size_t                                     m_target_primitive{0};
};

extern Material_paint_tool* g_material_paint_tool;

} // namespace editor
