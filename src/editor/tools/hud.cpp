#include "tools/hud.hpp"
#include "editor_log.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "rendertarget_node.hpp"
#include "rendertarget_imgui_viewport.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/scene/scene.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

auto Toggle_hud_visibility_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);
    m_hud.toggle_visibility();
    return true;
}

Hud::Hud()
    //: erhe::application::Imgui_window{c_title, c_type_name}
    : erhe::components::Component{c_type_name}
    , m_toggle_visibility_command{*this}
{
}

Hud::~Hud() noexcept
{
}

void Hud::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Scene_builder   >();
    require<Tools           >();
    require<Viewport_windows>();
}

void Hud::initialize_component()
{
    const erhe::application::Scoped_gl_context gl_context{
        get<erhe::application::Gl_context_provider>()
    };

    get<Tools>()->register_background_tool(this);
    //get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto& commands = get<erhe::application::Commands>();
    commands->register_command(&m_toggle_visibility_command);
    commands->bind_command_to_key(&m_toggle_visibility_command, erhe::toolkit::Key_e, true );

    //const auto& rendergraph             = get<erhe::application::Rendergraph  >();
    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& scene_builder           = get<Scene_builder   >();
    //const auto& viewport_windows        = get<Viewport_windows>();
    const auto& primary_viewport_window = scene_builder->get_primary_viewport_window();
    const auto& scene_root              = scene_builder->get_scene_root();

    m_rendertarget_node = scene_root->create_rendertarget_node(
        *m_components,
        *primary_viewport_window.get(),
        1024,
        1024,
        5000.0
    );

    scene_root->scene().add_to_mesh_layer(
        *scene_root->layers().rendertarget(),
        m_rendertarget_node
    );

    m_rendertarget_node->node_data.visibility_mask &= ~erhe::scene::Node_visibility::visible;

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_node.get(),
        "Hud Viewport",
        *m_components
        //false
    );

    // Also registers rendertarget node
    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    //set_viewport(m_rendertarget_imgui_viewport.get());
    //show();

    m_is_visible = true;
    m_rendertarget_node->node_data.visibility_mask |= erhe::scene::Node_visibility::visible;
}

[[nodiscard]] auto Hud::get_rendertarget_imgui_viewport() -> std::shared_ptr<Rendertarget_imgui_viewport>
{
    return m_rendertarget_imgui_viewport;
}

void Hud::post_initialize()
{
    m_viewport_windows = get<Viewport_windows>();
}

auto Hud::description() -> const char*
{
   return c_title.data();
}

void Hud::update_node_transform(const glm::mat4& world_from_camera)
{
    const glm::vec3 target_position{world_from_camera * glm::vec4{0.0, 0.0, 0.0, 1.0}};
    const glm::vec3 eye_position{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}};
    const glm::vec3 up_direction{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}};

    const glm::mat4 m = erhe::toolkit::create_look_at(
        eye_position,
        target_position,
        up_direction
    );

    m_rendertarget_node->set_world_from_node(m);
    m_rendertarget_node->update_transform();
}

void Hud::tool_render(
    const Render_context& /*context*/
)
{
}

void Hud::toggle_visibility()
{
    m_is_visible = !m_is_visible;

    if (m_is_visible)
    {
        log_hud->trace("Hud visible");
        m_rendertarget_node->node_data.visibility_mask |= erhe::scene::Node_visibility::visible;
    }
    else
    {
        log_hud->trace("Hud hidden");
        m_rendertarget_node->node_data.visibility_mask &= ~erhe::scene::Node_visibility::visible;
    }
}

#if defined(ERHE_GUI_LIBRARY_IMGUI) && 0
auto Hud::flags() -> ImGuiWindowFlags
{
    return
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoNavInputs       |
        ImGuiWindowFlags_NoNavFocus;
}

void Hud::on_begin()
{
    m_min_size[0] = static_cast<float>(m_rendertarget_node->width());
    m_min_size[1] = static_cast<float>(m_rendertarget_node->height());
    m_max_size[0] = m_min_size[0];
    m_max_size[1] = m_min_size[1];
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
}

void Hud::imgui()
{
    //const ImVec2 button_size{64.0f, 64.0f};
    //for (int y = 0; y < m_height_items; ++y)
    //{
    //    for (int x = 0; x < m_width_items; ++x)
    //    {
    //        ImGui::Button("_", button_size);
    //        if (x != (m_width_items - 1))
    //        {
    //            ImGui::SameLine();
    //        }
    //    }
    //}
}
#endif

} // namespace editor
