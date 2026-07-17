#pragma once

#include "tools/tool.hpp"
#include "assets/asset_reference.hpp"

#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>

namespace erhe            { class Item_host; }
namespace erhe::imgui     { class Imgui_windows; }
namespace erhe::primitive { class Material; }

namespace editor {

class App_context;
class App_message_bus;
class Hover_entry;
class Headset_view;
class Icon_set;
class Material_paint_tool;
class Operations;
class Scene_views;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_view;
#endif

class Material_paint_command : public erhe::commands::Command
{
public:
    Material_paint_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready      ()         override;
    auto try_call       () -> bool override;

private:
    App_context& m_context;
};

class Material_pick_command : public erhe::commands::Command
{
public:
    Material_pick_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready      ()         override;
    auto try_call       () -> bool override;

private:
    App_context& m_context;
};

class Material_paint_tool : public Tool
{
public:
    static constexpr int c_priority{2};

    Material_paint_tool(
        erhe::commands::Commands& commands,
        App_context&              app_context,
        App_message_bus&          app_message_bus,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_properties       (erhe::imgui::Imgui_window&)         override;

    // Commands
    void set_active_command(const int command);

    auto on_paint_ready() -> bool;
    auto on_paint      () -> bool;

    auto on_pick_ready() -> bool;
    auto on_pick      () -> bool;

    void from_drag_and_drop(const std::shared_ptr<erhe::primitive::Material>& material);

    // Set / clear the paint material (records the tool as a declared asset
    // user; null clears). Pick and the properties combo route through this;
    // it is also the MCP set_tool_asset entry point.
    void set_material(const std::shared_ptr<erhe::primitive::Material>& material);

private:
    [[nodiscard]] auto get_hover_mesh() const -> const Hover_entry*;

    // Scene close: drop the paint material when the closing scene's content
    // library hosts it, so the tool does not keep a dead scene's material
    // alive (and paint it into other scenes). The material is an
    // Asset_reference (declared usership, R3), so keeping it would only
    // report as intentionally pinned - but transient tool state is not a
    // persistence mechanism (the inventory is), so it is still cleared here
    // as belt-and-braces.
    void on_close_scene(erhe::Item_host* closing_host);

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Close_scene_message>      m_close_scene_subscription;
    Material_paint_command m_paint_command;
    Material_pick_command  m_pick_command;

    static const int c_command_paint{0};
    static const int c_command_pick {1};
    static const int c_command_both {2};

    int m_active_command{c_command_paint};

    Asset_reference m_material;
};

}
