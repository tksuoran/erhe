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
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

using glm::vec3;

auto Toggle_hud_visibility_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    const bool is_visible = m_hud.toggle_visibility();
    if (is_visible)
    {
        Scene_view* scene_view = reinterpret_cast<Scene_view*>(context.get_input_context());
        const auto& camera     = scene_view->get_camera();
        if (camera)
        {
            m_hud.update_node_transform(camera->world_from_node());
        }
    }
    return true;
}

Hud::Hud()
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
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Scene_builder   >();
    require<Tools           >();
    require<Viewport_windows>();
}

void Hud::initialize_component()
{
    const auto& configuration = get<erhe::application::Configuration>();
    const auto& hud           = configuration->hud;
    if (!hud.enabled)
    {
        return;
    }

    const erhe::application::Scoped_gl_context gl_context{
        get<erhe::application::Gl_context_provider>()
    };

    get<Tools>()->register_background_tool(this);

    m_is_visible = hud.show;
    m_x          = hud.x;
    m_y          = hud.y;
    m_z          = hud.z;

    const auto& commands = get<erhe::application::Commands>();
    commands->register_command   (&m_toggle_visibility_command);
    commands->bind_command_to_key(&m_toggle_visibility_command, erhe::toolkit::Key_e, true);

    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& scene_builder           = get<Scene_builder>();
    const auto& primary_viewport_window = scene_builder->get_primary_viewport_window();
    const auto& scene_root              = scene_builder->get_scene_root();

    m_rendertarget_node = scene_root->create_rendertarget_node(
        *m_components,
        *primary_viewport_window.get(),
        hud.width,
        hud.height,
        hud.ppm
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
    );

    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    set_visibility(m_is_visible);
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

void Hud::tool_render(
    const Render_context& /*context*/
)
{
}

auto Hud::toggle_visibility() -> bool
{
    set_visibility(!m_is_visible);
    return m_is_visible;
}

void Hud::set_visibility(const bool value)
{
    m_is_visible = value;

    if (!m_rendertarget_node)
    {
        return;
    }

    if (m_is_visible)
    {
        m_rendertarget_imgui_viewport->set_enabled(true);
        m_rendertarget_node->node_data.visibility_mask |= erhe::scene::Node_visibility::visible;
    }
    else
    {
        m_rendertarget_imgui_viewport->set_enabled(false);
        m_rendertarget_node->node_data.visibility_mask &= ~erhe::scene::Node_visibility::visible;
    }
}

} // namespace editor
