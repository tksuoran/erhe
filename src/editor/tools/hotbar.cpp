#include "tools/hotbar.hpp"
#include "brushes/brush_tool.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
#include "scene/content_library.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/trs_tool.hpp"
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
    require<Fly_camera_tool   >();
    require<Scene_builder     >();
    require<Tools             >();
    require<Viewport_windows  >();
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

    get<Tools                           >()->register_background_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    m_show = hotbar.show;
    m_x    = hotbar.x;
    m_y    = hotbar.y;
    m_z    = hotbar.z;

    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& scene_builder           = get<Scene_builder   >();
    const auto& primary_viewport_window = scene_builder->get_primary_viewport_window();
    const auto& scene_root              = scene_builder->get_scene_root();

    m_rendertarget_mesh = scene_root->create_rendertarget_mesh(
        *m_components,
        *primary_viewport_window.get(),
        128 * 5,
        128,
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
    }

    m_rendertarget_imgui_viewport->set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

    // Also registers rendertarget node
    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    this->set_viewport(m_rendertarget_imgui_viewport.get());

    m_rendertarget_imgui_viewport->set_enabled(m_show);
    m_rendertarget_mesh->set_visible(m_show);
    if (m_show)
    {
        show();
    }
    else
    {
        hide();
    }

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
    m_brush_tool       = get<Brush_tool      >();
    m_icon_set         = get<Icon_set        >();
    m_physics_tool     = get<Physics_tool    >();
    m_operations       = get<Operations      >();
    m_selection_tool   = get<Selection_tool  >();
    m_trs_tool         = get<Trs_tool        >();
    m_viewport_windows = get<Viewport_windows>();

    set_action(Hotbar_action::Select);
}

auto Hotbar::description() -> const char*
{
   return c_title.data();
}

void Hotbar::on_message(Editor_message& message)
{
    Tool::on_message(message);

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view))
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
    if (!m_rendertarget_node)
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

auto Hotbar::try_call(erhe::application::Command_context& context) -> bool
{
    if (context.get_button_bits() == 0)
    {
        return false;
    }

    const auto position = context.get_vec2_absolute_value();

    if (position.x < -0.2f)
    {
        --m_action;
        if (m_action < Hotbar_action::First)
        {
            m_action = Hotbar_action::Last;
        }
        set_action(m_action);
    }

    if (position.x > 0.2f)
    {
        ++m_action;
        if (m_action < Hotbar_action::First)
        {
            m_action = Hotbar_action::Last;
        }
        set_action(m_action);
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

void Hotbar::button(const glm::vec2 icon, const Action action, const char* tooltip)
{
    const glm::vec4 background_color{0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec4 tint_color      {1.0f, 1.0f, 1.0f, 1.0f};
    const int       frame_padding      = 0;
    const auto&     icon_rasterization = m_icon_set->get_hotbar_rasterization();
    const bool      linear{false};

    if (m_action == action)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    }

    const bool is_pressed = icon_rasterization.icon_button(
        icon,
        frame_padding,
        background_color,
        tint_color,
        linear
    );

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", tooltip);
    }

    if (m_action == action)
    {
        ImGui::PopStyleColor();
    }

    if (is_pressed)
    {
        set_action(action);
    }

}

void Hotbar::set_action(const Action action)
{
    m_action = action;

    switch (action)
    {
        case Hotbar_action::Select:
        {
            m_operations->set_active_tool(m_selection_tool.get());
            m_selection_tool->set_enable_state(true);
            m_brush_tool    ->set_enable_state(false);
            m_trs_tool      ->set_enable_state(false);
            m_trs_tool      ->set_rotate   (false);
            m_trs_tool      ->set_translate(false);
            break;
        }
        case Hotbar_action::Move:
        {
            m_operations->set_active_tool(m_trs_tool.get());
            m_selection_tool->set_enable_state(true);
            m_brush_tool    ->set_enable_state(false);
            m_trs_tool      ->set_enable_state(true);
            m_trs_tool      ->set_rotate   (false);
            m_trs_tool      ->set_translate(true);
            break;
        }
        case Hotbar_action::Rotate:
        {
            m_operations->set_active_tool(m_trs_tool.get());
            m_selection_tool->set_enable_state(true);
            m_brush_tool    ->set_enable_state(false);
            m_trs_tool      ->set_enable_state(true);
            m_trs_tool      ->set_translate(false);
            m_trs_tool      ->set_rotate   (true);
            break;
        }
        case Hotbar_action::Drag:
        {
            m_operations->set_active_tool(m_physics_tool.get());
            m_selection_tool->clear_selection();
            m_selection_tool->set_enable_state(false);
            m_brush_tool    ->set_enable_state(false);
            m_trs_tool      ->set_enable_state(false);
            m_trs_tool      ->set_translate(false);
            m_trs_tool      ->set_rotate   (false);
            m_physics_tool->set_mode(Physics_tool_mode::Drag);
            break;
        }
        case Hotbar_action::Brush_tool:
        {
            m_operations->set_active_tool(m_brush_tool.get());
            //// m_selection_tool->clear_selection();
            //// m_selection_tool->set_enable_state(false);
            //// m_brush_tool    ->set_enable_state(true);
            //// m_trs_tool      ->set_enable_state(false);
            //// m_trs_tool      ->set_translate(false);
            //// m_trs_tool      ->set_rotate   (false);
            break;
        }
        default:
        {
            break;
        }
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

    button(m_icon_set->icons.select,     Hotbar_action::Select,     "Select");
    button(m_icon_set->icons.move,       Hotbar_action::Move,       "Move");
    button(m_icon_set->icons.rotate,     Hotbar_action::Rotate,     "Rotate");
    button(m_icon_set->icons.drag,       Hotbar_action::Drag,       "Drag");
    button(m_icon_set->icons.brush_tool, Hotbar_action::Brush_tool, "Brush Tool");

    ImGui::PopStyleColor(3);

#endif
}

} // namespace editor
