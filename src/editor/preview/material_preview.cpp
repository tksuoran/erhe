#include "preview/material_preview.hpp"

#include "app_context.hpp"
#include "brushes/brush.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/programs.hpp"
#include "renderers/render_context.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/viewport_config.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

namespace editor {

Material_preview::Material_preview(
    erhe::graphics::Device&         graphics_device,
    erhe::scene::Scene_message_bus& scene_message_bus,
    App_context&                    app_context,
    Mesh_memory&                    mesh_memory,
    Programs&                       programs
)
    : Scene_preview{graphics_device, scene_message_bus, app_context, mesh_memory, programs}
{
    make_preview_scene(mesh_memory);

    resize(256, 256);
    update_rendertarget(graphics_device);
}

Material_preview::~Material_preview() noexcept
{
}

void Material_preview::make_preview_scene(Mesh_memory& mesh_memory)
{
    ERHE_PROFILE_FUNCTION();

    m_node = std::make_shared<erhe::scene::Node>("Material Preview Node");
    m_mesh = std::make_shared<erhe::scene::Mesh>("Material Preview Mesh");
    erhe::primitive::Element_mappings dummy; // TODO make Element_mappings optional
    GEO::Mesh sphere_mesh{3, true};
    erhe::geometry::shapes::make_sphere(
        sphere_mesh,
        m_radius,
        std::max(1, m_slice_count),
        std::max(1, m_stack_count)
    );
    erhe::primitive::Buffer_mesh buffer_mesh{};
    const bool buffer_mesh_ok = erhe::primitive::build_buffer_mesh(
        buffer_mesh,
        sphere_mesh,
        erhe::primitive::Build_info{
            .primitive_types = {.fill_triangles = true },
            .buffer_info = mesh_memory.buffer_info
        },
        dummy,
        erhe::primitive::Normal_style::corner_normals
    );

    if (buffer_mesh_ok) {
        std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(buffer_mesh);
        m_mesh->add_primitive(new_primitive);
    } else {
        // TODO handle error
        log_render->error("Unable to create material preview mesh - out of memory?");
    }

    using Item_flags = erhe::Item_flags;
    m_mesh->layer_id = m_scene_root_shared->layers().content()->id;
    m_mesh->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::opaque);
    m_node->attach(m_mesh);
    m_node->enable_flag_bits(Item_flags::content | Item_flags::visible);

    const auto paremt = m_scene_root_shared->get_hosted_scene()->get_root_node();
    m_node->set_parent(paremt);

    m_key_light_node = std::make_shared<erhe::scene::Node>("Key Light Node");
    m_key_light      = std::make_shared<erhe::scene::Light>("Key Light");
    m_key_light_node->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->enable_flag_bits(erhe::Item_flags::content);
    m_key_light->layer_id = m_scene_root_shared->layers().light()->id;
    m_key_light_node->attach(m_key_light);
    m_key_light_node->set_parent(paremt);
    m_key_light_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{-8.0f, 8.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    //// m_fill_light_node = std::make_shared<erhe::scene::Node>("Fill Light Node");
    //// m_fill_light      = std::make_shared<erhe::scene::Light>("Fill Light");
    //// m_fill_light_node->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->enable_flag_bits(erhe::Item_flags::content);
    //// m_fill_light     ->layer_id = m_scene_root->layers().light()->id;

    m_camera_node = std::make_shared<erhe::scene::Node>("Camera node");
    m_camera = std::make_shared<erhe::scene::Camera>("Camera");
    m_camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_camera->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    m_camera->projection()->fov_y = 0.3f;
    m_camera->projection()->z_near = 4.0f;
    m_camera->projection()->z_far = 12.0f;
    m_camera_node->attach(m_camera);
    m_camera_node->set_parent(paremt);
    m_camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 0.0f, 8.0f},  // eye
            glm::vec3{0.0f, 0.0f, 0.0f},  // center
            glm::vec3{0.0f, 1.0f, 0.0f}   // up
        )
    );

    auto composition_pass = std::make_shared<Composition_pass>("Material Preview Composition_pass");
    composition_pass->mesh_layers = {Mesh_layer_id::content};
    composition_pass->primitive_mode = erhe::primitive::Primitive_mode::polygon_fill;
    composition_pass->filter = erhe::Item_filter{};
    composition_pass->passes = {&m_pipeline_pass};
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_composer.mutex};
        m_composer.composition_passes.push_back(composition_pass);
    }
}

void Material_preview::render_preview(const std::shared_ptr<erhe::primitive::Material>& material)
{
    ERHE_PROFILE_FUNCTION();

    erhe::graphics::Scoped_debug_group outer_debug_scope{"Scene_preview::render_preview()"};

    m_content_library->materials->remove_all_children_recursively();
    m_content_library->materials->add(material);
    m_last_material = material;

    m_mesh->get_mutable_primitives().front().material = material;

    const erhe::math::Viewport viewport{0, 0, m_width, m_height};

    const auto& layers = m_scene_root_shared->layers();

    m_light_projections = erhe::scene_renderer::Light_projections{
        layers.light()->lights,
        m_camera.get(),
        viewport,
        erhe::math::Viewport{},
        m_shadow_texture
    };

    erhe::graphics::Render_command_encoder render_encoder = m_graphics_device.make_render_command_encoder(*m_render_pass.get());
    const Render_context context{
        .encoder             = &render_encoder,
        .app_context         = m_context,
        .scene_view          = *this,
        .viewport_config     = m_viewport_config,
        .camera              = m_camera.get(),
        .viewport_scene_view = nullptr,
        .viewport = erhe::math::Viewport{
            .x      = 0,
            .y      = 0,
            .width  = m_width,
            .height = m_height
        },
        .override_shader_stages = nullptr
    };
    m_composer.render(context);
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

void Material_preview::show_preview()
{
    m_context.imgui_renderer->image(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = m_color_texture, //std::static_pointer_cast<erhe::graphics::Texture_reference>(m_color_texture),
            .width             = m_width,
            .height            = m_height,
            .debug_label       = "Material_preview::show_preview()"
        }
    );
}


}  // namespace editor
