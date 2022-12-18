#include "tools/hud.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"

#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"

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
    require<Editor_message_bus>();
    require<Scene_builder     >();
    require<Tools             >();
    require<Viewport_windows  >();
}

void Hud::initialize_component()
{
    ERHE_PROFILE_FUNCTION

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

    m_rendertarget_mesh = scene_root->create_rendertarget_mesh(
        *m_components,
        *primary_viewport_window.get(),
        hud.width,
        hud.height,
        hud.ppm
    );
    m_rendertarget_mesh->mesh_data.layer_id = scene_root->layers().rendertarget()->id;
    m_rendertarget_mesh->enable_flag_bits(
        erhe::scene::Item_flags::content |
        erhe::scene::Item_flags::visible |
        erhe::scene::Item_flags::show_in_ui
    );

    //m_rendertarget_mesh->disable_flag_bits(erhe::scene::Item_flags::visible);

    m_rendertarget_node = std::make_shared<erhe::scene::Node>("Hud RT node");
    m_rendertarget_node->set_parent(scene_root->scene().get_root_node());
    m_rendertarget_node->attach(m_rendertarget_mesh);
    m_rendertarget_node->enable_flag_bits(
        erhe::scene::Item_flags::content |
        erhe::scene::Item_flags::visible |
        erhe::scene::Item_flags::show_in_ui
    );
    auto node_raytrace = m_rendertarget_mesh->get_node_raytrace();
    if (node_raytrace)
    {
        m_rendertarget_node->attach(node_raytrace);
    }

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_mesh.get(),
        "Hud Viewport",
        *m_components
    );

    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    set_visibility(m_is_visible);

    get<Editor_message_bus>()->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );
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

void Hud::on_message(Editor_message& message)
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
            }
        }
    }
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

    if (!m_rendertarget_mesh)
    {
        return;
    }

    m_rendertarget_imgui_viewport->set_enabled(m_is_visible);
    m_rendertarget_mesh->set_visible(m_is_visible);
}

} // namespace editor
