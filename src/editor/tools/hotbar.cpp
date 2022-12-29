#include "tools/hotbar.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/content_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif
#include "windows/operations.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

auto Hotbar_trackpad_command::try_call(erhe::application::Command_context& context) -> bool
{
    return m_hotbar.try_call(context);
}

Hotbar::Hotbar()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
    , m_trackpad_command             {*this}
{
}

Hotbar::~Hotbar() noexcept
{
}

void Hotbar::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Editor_message_bus>();
    require<Scene_builder     >();
    require<Viewport_windows  >();
    m_icon_set = require<Icon_set>();
    m_tools    = require<Tools   >();
}

void Hotbar::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& configuration = get<erhe::application::Configuration>();
    const auto& hotbar        = configuration->hotbar;
    if (!hotbar.enabled)
    {
        return;
    }

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_trackpad_command);
    commands->bind_command_to_controller_trackpad(&m_trackpad_command, true);

    const erhe::application::Scoped_gl_context gl_context{
        get<erhe::application::Gl_context_provider>()
    };

    set_description(c_title);
    set_flags      (Tool_flags::background);
    get<Tools>()->register_tool(this);

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    m_show = hotbar.show;
    m_x    = hotbar.x;
    m_y    = hotbar.y;
    m_z    = hotbar.z;

    const auto& imgui_windows = get<erhe::application::Imgui_windows>();
    const auto& scene_builder = get<Scene_builder>();
    const auto& scene_root    = scene_builder->get_scene_root();

    const auto& icon_rasterization = m_icon_set->get_hotbar_rasterization();
    const int   icon_size          = icon_rasterization.get_size();

    m_rendertarget_mesh = std::make_shared<Rendertarget_mesh>(
        *m_components,
        icon_size * 8,
        icon_size,
        4000.0
    );
    m_rendertarget_mesh->mesh_data.layer_id = scene_root->layers().rendertarget()->id;

    m_rendertarget_mesh->enable_flag_bits(
        erhe::scene::Item_flags::visible
    );

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_mesh.get(),
        "Hotbar Viewport",
        *m_components,
        false
    );

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hotbar RT node");
    m_rendertarget_node->attach(m_rendertarget_mesh);
    auto node_raytrace = m_rendertarget_mesh->get_node_raytrace();
    if (node_raytrace)
    {
        m_rendertarget_node->attach(node_raytrace);
        m_rendertarget_node->show();
    }

    m_rendertarget_imgui_viewport->set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

    // Also registers rendertarget node
    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    this->set_viewport(m_rendertarget_imgui_viewport.get());

    m_rendertarget_imgui_viewport->set_enabled(m_show);
    m_rendertarget_mesh->set_visible(m_show);
    set_visibility(m_show);

    get<Editor_message_bus>()->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
}

void Hotbar::post_initialize()
{
    m_imgui_renderer   = get<erhe::application::Imgui_renderer>();
    m_rendergraph      = get<erhe::application::Rendergraph   >();
    m_viewport_windows = get<Viewport_windows>();

    m_slot_first = 0;
    m_slot_last  = 0;
    const auto& tools = m_tools->get_tools();
    for (Tool* tool : tools)
    {
        const auto opt_icon = tool->get_icon();
        if (!opt_icon.has_value())
        {
            continue;
        }
        if (erhe::toolkit::test_all_rhs_bits_set(tool->get_flags(), Tool_flags::toolbox))
        {
            m_slot_last = m_slots.size();
            m_slots.push_back(tool);
        }
    }
}

void Hotbar::on_message(Editor_message& message)
{
    Tool::on_message(message);

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view))
    {
        Scene_view* const old_scene_view = m_hover_scene_view;
        if (message.scene_view != old_scene_view)
        {
            using Rendergraph_node = erhe::application::Rendergraph_node;
            auto old_node = (old_scene_view     != nullptr) ? old_scene_view    ->get_rendergraph_node() : std::shared_ptr<Rendergraph_node>{};
            auto new_node = (message.scene_view != nullptr) ? message.scene_view->get_rendergraph_node() : std::shared_ptr<Rendergraph_node>{};
            if (old_node != new_node)
            {
                if (old_node)
                {
                    m_rendergraph->disconnect(
                        erhe::application::Rendergraph_node_key::rendertarget_texture,
                        m_rendertarget_imgui_viewport,
                        old_node
                    );
                }
                if (new_node)
                {
                    m_hover_scene_view = message.scene_view;
                    m_rendergraph->connect(
                        erhe::application::Rendergraph_node_key::rendertarget_texture,
                        m_rendertarget_imgui_viewport,
                        new_node
                    );
                }
            }
        }
    }

    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view))
    {
        const bool should_show = (m_hover_scene_view != nullptr) && (m_hover_scene_view == message.scene_view);
        set_visibility(should_show);
        if (should_show)
        {
            if (message.scene_view != nullptr)
            {
                const auto& camera = message.scene_view->get_camera();
                if (camera)
                {
                    const auto* camera_node = camera->get_node();
                    if (camera_node != nullptr)
                    {
                        const auto& world_from_camera = camera_node->world_from_node();
                        update_node_transform(world_from_camera);
                        auto scene_root = message.scene_view->get_scene_root();
                        if (scene_root)
                        {
                            auto* scene = scene_root->get_hosted_scene();
                            if (scene != nullptr)
                            {
                                m_rendertarget_node->set_parent(scene->get_root_node());
                            }
                        }
                    }
                }
            }
        }
    }
}

auto Hotbar::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    {
        const auto& headset_view = get<editor::Headset_view>();
        if (headset_view)
        {
            return headset_view->get_camera();
        }
    }
#endif
    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return {};
    }
    return viewport_window->get_camera();
}

void Hotbar::update_node_transform(const glm::mat4& world_from_camera)
{
    if (!m_rendertarget_node || m_locked)
    {
        return;
    }

    const glm::vec3 target_position{world_from_camera * glm::vec4{0.0, 0.0, 0.0, 1.0}};
    const glm::vec3 eye_position{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}};
    const glm::vec3 up_direction{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}};

    const glm::mat4 m = erhe::toolkit::create_look_at(
        eye_position,
        target_position,
        up_direction
    );

    m_rendertarget_node->set_world_from_node(m);
}

void Hotbar::tool_render(
    const Render_context& /*context*/
)
{
}

auto Hotbar::flags() -> ImGuiWindowFlags
{
    return
        ImGuiWindowFlags_NoBackground      |
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoNavInputs       |
        ImGuiWindowFlags_NoNavFocus;
}

void Hotbar::on_begin()
{
    m_min_size[0] = static_cast<float>(m_rendertarget_mesh->width());
    m_min_size[1] = static_cast<float>(m_rendertarget_mesh->height());
    m_max_size[0] = m_min_size[0];
    m_max_size[1] = m_min_size[1];
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
}

void Hotbar::handle_slot_update()
{
    if ((m_slot >= m_slot_first) && (m_slot <= m_slot_last))
    {
        Tool* new_tool = m_slots.at(m_slot);
        m_tools->set_priority_tool(new_tool);
    }
}

auto Hotbar::try_call(erhe::application::Command_context& context) -> bool
{
    if (context.get_button_bits() == 0)
    {
        return false;
    }

    const auto position = context.get_vec2_absolute_value();

    const auto old_slot = m_slot;
    if (position.x < -0.2f)
    {
        m_slot = (m_slot == m_slot_first)
            ? m_slot_last
            : --m_slot;
    }

    if (position.x > 0.2f)
    {
        m_slot = (m_slot == m_slot_last)
            ? m_slot_first
            : ++m_slot;
    }

    if (m_slot != old_slot)
    {
        handle_slot_update();
    }

    return true;
}

auto Hotbar::get_color(const int color) -> glm::vec4&
{
    switch (color)
    {
        case 0:  return m_color_inactive;
        case 1:  return m_color_hover;
        case 2:  return m_color_active;
        default: return m_color_inactive;
    }
}

auto Hotbar::get_position() const -> glm::vec3
{
    return glm::vec3{m_x, m_y, m_z};
}

void Hotbar::set_position(glm::vec3 position)
{
    m_x = position.x;
    m_y = position.y;
    m_z = position.z;
}

auto Hotbar::get_locked() const -> bool
{
    return m_locked;
}
void Hotbar::set_locked(bool value)
{
    m_locked = value;
}

void Hotbar::tool_button(Tool* tool)
{
    const glm::vec4 background_color  {0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec4 tint_color        {1.0f, 1.0f, 1.0f, 1.0f};
    const int       frame_padding     {0};
    const auto&     icon_rasterization{m_icon_set->get_hotbar_rasterization()};
    const bool      linear            {false};
    const bool      is_boosted        {tool->get_priority_boost() > 0};
    const auto      opt_icon          {tool->get_icon()};

    if (!opt_icon)
    {
        return;
    }

    if (is_boosted)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    }

    const bool is_pressed = icon_rasterization.icon_button(
        opt_icon.value(),
        frame_padding,
        background_color,
        tint_color,
        linear
    );

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", tool->get_description());
    }

    if (is_boosted)
    {
        ImGui::PopStyleColor();
    }

    if (is_pressed)
    {
        m_tools->set_priority_tool(is_boosted ? nullptr : tool);
    }

}

void Hotbar::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    //const ImVec2 button_size{64.0f, 64.0f};
    //ImGui::SliderFloat("X", &m_x, -2.0f, 2.0f);
    //ImGui::SliderFloat("Y", &m_y, -0.3f, 0.0f);
    //ImGui::SliderFloat("Z", &m_z, -2.0f, 2.0f);

    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  m_color_active);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_color_hover);
    ImGui::PushStyleColor(ImGuiCol_Button,        m_color_inactive);

    for (auto* tool : m_slots)
    {
        tool_button(tool);
    }
    ImGui::PopStyleColor(3);

#endif
}

void Hotbar::set_visibility(const bool value)
{
    Imgui_window::set_visibility(value);

    if (!m_rendertarget_mesh)
    {
        return;
    }

    m_rendertarget_imgui_viewport->set_enabled(m_is_visible);
    m_rendertarget_mesh->set_visible(m_is_visible);
}

} // namespace editor
