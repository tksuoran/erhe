#include "scene/material_preview.hpp"
#include "editor_scenes.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/content_library.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/shapes/sphere.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/gl/enum_bit_mask_operators.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/renderbuffer.hpp"
#include "erhe/graphics/texture.hpp"

#include "erhe/primitive/primitive_builder.hpp"

#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/bit_helpers.hpp"

#include <fmt/format.h>

namespace editor {

Material_preview::Material_preview()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Material_preview::~Material_preview() noexcept
{
}

void Material_preview::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<Editor_scenes>();
    m_mesh_memory = require<Mesh_memory>();
}

void Material_preview::initialize_component()
{
    const auto editor_scenes = get<Editor_scenes>();

    m_last_content_library = std::make_shared<Content_library>();
    m_last_content_library->is_shown_in_ui = true; //// TODO

    m_last_material = std::make_shared<erhe::primitive::Material>("Dummy Material");
    m_last_content_library->materials.add(m_last_material);

    m_scene_root = std::make_shared<Scene_root>(
        *m_components,
        m_last_content_library,
        "Material preview scene"
    );

    editor_scenes->register_scene_root(m_scene_root);

    m_scene_root->get_shared_scene()->disable_flag_bits(erhe::scene::Item_flags::show_in_ui);

    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    make_preview_scene();
}

void Material_preview::make_rendertarget()
{
    m_width        = 256;
    m_height       = 256;
    m_color_format = gl::Internal_format::rgba16f;
    m_depth_format = gl::Internal_format::depth_component32f;

    using Framebuffer  = erhe::graphics::Framebuffer;
    using Renderbuffer = erhe::graphics::Renderbuffer;
    using Texture      = erhe::graphics::Texture;
    m_color_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = m_color_format,
            .width           = m_width,
            .height          = m_height
        }
    );
    m_color_texture->set_debug_label("Material Preview Color Texture");
    const float clear_value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    gl::clear_tex_image(
        m_color_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    m_depth_renderbuffer = std::make_unique<erhe::graphics::Renderbuffer>(
        m_depth_format,
        m_width,
        m_height
    );

    m_depth_renderbuffer->set_debug_label("Material Preview Depth Renderbuffer");

    Framebuffer::Create_info create_info;
    create_info.attach(
        gl::Framebuffer_attachment::color_attachment0,
        m_color_texture.get()
    );

    create_info.attach(
        gl::Framebuffer_attachment::depth_attachment,
        m_depth_renderbuffer.get()
    );
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("Material Preview Framebuffer");

    gl::Color_buffer draw_buffers[] = { gl::Color_buffer::color_attachment0 };
    gl::named_framebuffer_draw_buffers(m_framebuffer->gl_name(), 1, &draw_buffers[0]);
    gl::named_framebuffer_read_buffer(m_framebuffer->gl_name(), gl::Color_buffer::color_attachment0);

    if (!m_framebuffer->check_status())
    {
        m_framebuffer.reset();
    }
}

void Material_preview::make_preview_scene()
{
    erhe::geometry::Geometry sphere_geometry = erhe::geometry::shapes::make_sphere(
        m_radius,
        std::max(1, m_slice_count),
        std::max(1, m_stack_count)
    );
    erhe::primitive::Primitive_geometry gl_primitive_geometry = erhe::primitive::make_primitive(
        sphere_geometry,
        m_mesh_memory->build_info,
        erhe::primitive::Normal_style::corner_normals
    );

    m_node = std::make_shared<erhe::scene::Node>("Material Preview Node");
    m_mesh = std::make_shared<erhe::scene::Mesh>("Material Preview Mesh");
    m_mesh->mesh_data.primitives.push_back(
        erhe::primitive::Primitive{
            .material              = m_last_material,
            .gl_primitive_geometry = gl_primitive_geometry,
            .normal_style          = erhe::primitive::Normal_style::corner_normals
        }
    );

    using Item_flags = erhe::scene::Item_flags;
    m_mesh->mesh_data.layer_id = m_scene_root->layers().content()->id;
    m_mesh->enable_flag_bits(Item_flags::content | Item_flags::visible);
    m_node->attach          (m_mesh);
    m_node->enable_flag_bits(Item_flags::content | Item_flags::visible);

    const auto paremt = m_scene_root->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_key_light_node  = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light       = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light_node ->enable_flag_bits(erhe::scene::Item_flags::content);
    m_key_light      ->enable_flag_bits(erhe::scene::Item_flags::content);
    m_key_light      ->layer_id = m_scene_root->layers().light()->id;
    m_key_light_node ->attach(m_key_light);
    m_key_light_node ->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::toolkit::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{ 0.0f, 0.0f, 0.0f},  // center
            glm::vec3{ 0.0f, 1.0f, 0.0f}   // up
        )
    );

    //// m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    //// m_fill_light      = std::make_shared<erhe::scene::Light>("Fill Light");
    //// m_fill_light_node->enable_flag_bits(erhe::scene::Item_flags::content);
    //// m_fill_light     ->enable_flag_bits(erhe::scene::Item_flags::content);
    //// m_fill_light     ->layer_id = m_scene_root->layers().light()->id;

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera      = std::make_shared<erhe::scene::Camera>("Camera");
    m_camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->enable_flag_bits(erhe::scene::Item_flags::content | Item_flags::show_in_ui);
    m_camera     ->projection()->fov_y  =  0.3f;
    m_camera     ->projection()->z_near =  4.0f;
    m_camera     ->projection()->z_far  = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);
    m_camera_node->set_parent_from_node(
        erhe::toolkit::create_look_at(
            glm::vec3{0.0f, 0.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

}

void Material_preview::render_preview(
    const std::shared_ptr<Content_library>&           content_library,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const erhe::scene::Viewport&                      viewport
)
{
    m_last_content_library = content_library;
    m_last_material        = material;

    m_mesh->mesh_data.primitives.front().material = material;

    gl::enable(gl::Enable_cap::scissor_test);
    gl::scissor(viewport.x, viewport.y, viewport.width, viewport.height);
    gl::clear_color(
        m_clear_color[0],
        m_clear_color[1],
        m_clear_color[2],
        m_clear_color[3]
    );
    gl::clear_stencil(0);
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear(
        gl::Clear_buffer_mask::color_buffer_bit |
        gl::Clear_buffer_mask::depth_buffer_bit |
        gl::Clear_buffer_mask::stencil_buffer_bit
    );

    //// const Render_context context
    //// {
    ////     .scene_view             = nullptr,
    ////     .viewport_window        = nullptr,
    ////     .viewport_config        = m_viewport_config.get(),
    ////     .camera                 = m_camera.lock().get(),
    ////     .viewport               = output_viewport,
    ////     .override_shader_stages = get_override_shader_stages()
    //// };
    ////
    //// gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    //// m_editor_rendering->render_content(context, true);
}

////void Material_preview::generate_torus_geometry()
////{
////    m_torus_geometry = std::make_shared<erhe::geometry::Geometry>(
////        erhe::geometry::shapes::make_torus(
////            m_major_radius,
////            m_minor_radius,
////            m_major_steps,
////            m_minor_steps
////        )
////    );
////}

void Material_preview::post_initialize()
{
    m_configuration = get<erhe::application::Configuration>();
    m_imgui_windows = get<erhe::application::Imgui_windows>();
}

[[nodiscard]] auto Material_preview::get_scene_root() -> std::shared_ptr<Scene_root>
{
    return m_scene_root;
}

void Material_preview::imgui()
{
}

}  // namespace erhe::application
