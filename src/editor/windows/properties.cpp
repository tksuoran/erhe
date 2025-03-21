#include "windows/properties.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "rendertarget_mesh.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/selection_tool.hpp"
#include "scene/brush_placement.hpp"
#include "scene/content_library.hpp"
#include "scene/frame_controller.hpp"
#include "scene/material_preview.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "windows/animation_curve.hpp"
#include "windows/brdf_slice.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor {

Properties::Properties(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, Editor_context& editor_context)
    : Imgui_window{imgui_renderer, imgui_windows, "Properties", "properties"}
    , m_context   {editor_context}
{
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)

void Properties::animation_properties(erhe::scene::Animation& animation)
{
    ERHE_PROFILE_FUNCTION();

    static float time       = 0.0f;
    static float start_time = 0.0f;
    static float end_time   = 5.0f;
    bool time_changed = false;

    add_entry("Time", [&](){ time_changed = ImGui::SliderFloat("Time##animation-time", &time, start_time, end_time); });
    add_entry("Samplers", [&animation](){ ImGui::Text("%d", static_cast<int>(animation.samplers.size())); });
    add_entry("Channels", [&animation](){ ImGui::Text("%d", static_cast<int>(animation.channels.size())); });
    //ImGui::BulletText("Samplers");
    //{
    //    for (auto& sampler : animation.samplers) {
    //        std::string text = fmt::format("Sampler {}", erhe::scene::c_str(sampler.interpolation_mode));
    //        ImGui::BulletText("%s", text.c_str());
    //    }
    //}
    //
    //ImGui::BulletText("Channels");
    //ImGui::Indent(10.0f);
    //{
    //    for (auto& channel : animation->channels) {
    //        std::string text = fmt::format(
    //            "{} {}",
    //            //channel.target->get_name(),
    //            channel.target->describe(),
    //            erhe::scene::c_str(channel.path)
    //        );
    //        ImGui::BulletText("%s", text.c_str());
    //    }
    //}
    //ImGui::Unindent(10.0f);

    add_entry("Curve", [&animation](){ animation_curve(animation); });

    if (!time_changed) {
        return;
    }

    animation.apply(time);
    m_context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_animation_update
        }
    );

    // TODO assert all animation channels targets point to same scene?
    std::shared_ptr<erhe::scene::Node> target = animation.channels.front().target;
    if (!target) {
        return;
    }
    erhe::scene::Scene* scene = animation.channels.front().target->get_scene();
    if (scene == nullptr) {
        return;
    }
    scene->update_node_transforms();
}

void Properties::camera_properties(erhe::scene::Camera& camera)
{
    ERHE_PROFILE_FUNCTION();

    auto* const projection = camera.projection();
    if (projection != nullptr) {
        push_group("Projection", ImGuiTreeNodeFlags_None, m_indent);
        //ImGui::TreeNodeEx("Projection", ImGuiTreeNodeFlags_DefaultOpen)

        //ImGui::Indent(indent);
        add_entry("Type", [=]() {
            erhe::imgui::make_combo(
                "##",
                projection->projection_type,
                erhe::scene::Projection::c_type_strings,
                IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings)
            );
        });
        switch (projection->projection_type) {
            //using enum erhe::scene::Projection::Type;
            case erhe::scene::Projection::Type::perspective: {
                add_entry("Fov X",  [=](){ImGui::SliderFloat("##", &projection->fov_x,  0.0f, glm::pi<float>());});
                add_entry("Fov Y",  [=](){ImGui::SliderFloat("##", &projection->fov_y,  0.0f, glm::pi<float>());});
                add_entry("Z Near", [=](){ImGui::SliderFloat("##", &projection->z_near, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);});
                add_entry("Z Far",  [=](){ImGui::SliderFloat("##", &projection->z_far,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);});
                break;
            }

            case erhe::scene::Projection::Type::perspective_xr: {
                add_entry("Fov Left",  [=](){ ImGui::SliderFloat("##", &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Right", [=](){ ImGui::SliderFloat("##", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Up",    [=](){ ImGui::SliderFloat("##", &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Down",  [=](){ ImGui::SliderFloat("##", &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Z Near",    [=](){ ImGui::SliderFloat("##", &projection->z_near,    0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);});
                add_entry("Z Far",     [=](){ ImGui::SliderFloat("##", &projection->z_far,     0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);});
                break;
            }

            case erhe::scene::Projection::Type::perspective_horizontal: {
                add_entry("Fov X",  [=](){ ImGui::SliderFloat("##", &projection->fov_x,  0.0f, glm::pi<float>()); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::perspective_vertical: {
                add_entry("Fov Y",  [=](){ ImGui::SliderFloat("##", &projection->fov_y,  0.0f, glm::pi<float>()); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_horizontal: {
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near,      0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,       0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_vertical: {
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near,       0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,        0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal: {
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near,       0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,        0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_rectangle: {
                add_entry("Left",   [=](){ ImGui::SliderFloat("##", &projection->ortho_left,   0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Bottom", [=](){ ImGui::SliderFloat("##", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near,       0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,        0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::generic_frustum: {
                add_entry("Left",   [=](){ ImGui::SliderFloat("##", &projection->frustum_left,   0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Right",  [=](){ ImGui::SliderFloat("##", &projection->frustum_right,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Bottom", [=](){ ImGui::SliderFloat("##", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Top",    [=](){ ImGui::SliderFloat("##", &projection->frustum_top,    0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Near", [=](){ ImGui::SliderFloat("##", &projection->z_near,         0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Z Far",  [=](){ ImGui::SliderFloat("##", &projection->z_far,          0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::other: {
                // TODO(tksuoran@gmail.com): Implement
                break;
            }
        }

        pop_group();
    }

    add_entry("Exposure", [&camera]() {
        float exposure = camera.get_exposure();
        if (ImGui::SliderFloat("##", &exposure, 0.0f, 800000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
            camera.set_exposure(exposure);
        }
    });

    add_entry("Shadow Range", [&camera]() {
        float shadow_range = camera.get_shadow_range();
        if (ImGui::SliderFloat("##", &shadow_range, 1.00f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
            camera.set_shadow_range(shadow_range);
        }
    });
}

void Properties::light_properties(erhe::scene::Light& light)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Light Type", [&light]() {
        erhe::imgui::make_combo(
            "##",
            light.type,
            erhe::scene::Light::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Light::c_type_strings)
        );
    });

    if (light.type == erhe::scene::Light::Type::spot) {
        add_entry("Inner Spot", [&](){ ImGui::SliderFloat("##", &light.inner_spot_angle, 0.0f, glm::pi<float>()); });
        add_entry("Outer Spot", [&](){ ImGui::SliderFloat("##", &light.outer_spot_angle, 0.0f, glm::pi<float>()); });
    }
    add_entry("Range",     [&](){ ImGui::SliderFloat("##", &light.range,     1.00f, 20000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
    add_entry("Intensity", [&](){ ImGui::SliderFloat("##", &light.intensity, 0.01f, 20000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
    add_entry("Color",     [&](){ ImGui::ColorEdit3 ("##", &light.color.x,   ImGuiColorEditFlags_Float); });

    // TODO Move to scene
    //// const auto* node = light.get_node();
    //// if (node != nullptr) {
    ////     auto* scene_root = static_cast<Scene_root*>(node->get_item_host());
    ////     if (scene_root != nullptr) {
    ////         const auto& layers = scene_root->layers();
    ////         ImGui::ColorEdit3(
    ////             "Ambient",
    ////             &layers.light()->ambient_light.x,
    ////             ImGuiColorEditFlags_Float
    ////         );
    ////     }
    //// }
}

void Properties::skin_properties(erhe::scene::Skin& skin)
{
    ERHE_PROFILE_FUNCTION();

    auto& skin_data = skin.skin_data;
    add_entry("Skeleton",    [&](){ ImGui::TextUnformatted(skin_data.skeleton ? skin_data.skeleton->get_name().c_str() : "(no skeleton)"); });
    add_entry("Joint Count", [&](){ ImGui::Text("%d", static_cast<int>(skin_data.joints.size())); });

    push_group("Skin", ImGuiTreeNodeFlags_None, m_indent);
    for (auto& joint : skin_data.joints) {
        if (!joint) {
            ImGui::TextUnformatted("(missing joint)");
        } else {
            add_entry("", [&](){ ImGui::TextUnformatted(joint->get_name().c_str()); });
        }
    }
    pop_group();
}

auto layer_name(const erhe::scene::Layer_id layer_id) -> const char*
{
    switch (layer_id) {
        case Mesh_layer_id::brush       : return "brush";
        case Mesh_layer_id::content     : return "content";
        case Mesh_layer_id::sky         : return "sky";
        case Mesh_layer_id::controller  : return "controller";
        case Mesh_layer_id::tool        : return "tool";
        case Mesh_layer_id::rendertarget: return "rendertarget";
        default:                          return "?";
    }
}

void Properties::texture_properties(const std::shared_ptr<erhe::graphics::Texture>& texture)
{
    ERHE_PROFILE_FUNCTION();

    if (!texture) {
        return;
    }

    push_group("Texture", ImGuiTreeNodeFlags_DefaultOpen, m_indent);

    add_entry("Name",   [&](){ ImGui::TextUnformatted(texture->get_name().c_str()); });
    add_entry("Width",  [&](){ ImGui::Text("%d", texture->width()); });
    add_entry("Height", [&](){ ImGui::Text("%d", texture->height()); });
    add_entry("Format", [&](){ ImGui::TextUnformatted(gl::c_str(texture->internal_format())); });

    add_entry("Preview", [&](){
        // TODO Draw to available size respecting aspect ratio
        m_context.imgui_renderer->image(texture, texture->width(), texture->height());
    });

    pop_group();
}

void Properties::geometry_properties(erhe::geometry::Geometry& geometry)
{
    ERHE_PROFILE_FUNCTION();

    push_group("Geometry", ImGuiTreeNodeFlags_None, m_indent);

    const GEO::Mesh& geo_mesh = geometry.get_mesh();
    add_entry("Vertices", [&geo_mesh](){
        int vertex_count = static_cast<int>(geo_mesh.vertices.nb());
        ImGui::InputInt("##", &vertex_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
    });
    add_entry("Facets", [&geo_mesh](){
        int facet_count = static_cast<int>(geo_mesh.facets.nb());
        ImGui::InputInt("##", &facet_count,  0, 0, ImGuiInputTextFlags_ReadOnly);
    });
    add_entry("Edges", [&geo_mesh](){
        int edge_count = static_cast<int>(geo_mesh.edges.nb());
        ImGui::InputInt("##", &edge_count,   0, 0, ImGuiInputTextFlags_ReadOnly);
    });
    add_entry("Corners", [&geo_mesh](){
        int corner_count = static_cast<int>(geo_mesh.facet_corners.nb());
        ImGui::InputInt("##", &corner_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
    });

    pop_group();
}

void Properties::buffer_mesh_properties(const char* label, const erhe::primitive::Buffer_mesh* buffer_mesh)
{
    ERHE_PROFILE_FUNCTION();

    if (buffer_mesh == nullptr) {
        return;
    }

    push_group(label, ImGuiTreeNodeFlags_None, m_indent);

    add_entry("Fill Triangles",  [=](){ ImGui::Text("%zu", buffer_mesh->triangle_fill_indices.get_triangle_count()); });
    add_entry("Edge Lines",      [=](){ ImGui::Text("%zu", buffer_mesh->edge_line_indices.get_line_count()); });
    add_entry("Corner Points",   [=](){ ImGui::Text("%zu", buffer_mesh->corner_point_indices.get_point_count()); });
    add_entry("Centroid Points", [=](){ ImGui::Text("%zu", buffer_mesh->polygon_centroid_indices.get_point_count()); });
    add_entry("Indices",         [=](){ ImGui::Text("%zu", buffer_mesh->index_buffer_range.count); });

    for (size_t i = 0, end = buffer_mesh->vertex_buffer_ranges.size(); i < end; ++i) {
        std::string stream_label = fmt::format("Vertex stream {}", i);
        push_group(label, ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        add_entry("Vertices",     [=](){ ImGui::Text("%zu", buffer_mesh->vertex_buffer_ranges.at(i).count); });
        add_entry("Vertex Bytes", [=](){ ImGui::Text("%zu", buffer_mesh->vertex_buffer_ranges.at(i).get_byte_size()); });
        add_entry("Index Bytes",  [=](){ ImGui::Text("%zu", buffer_mesh->vertex_buffer_ranges.at(i).get_byte_size()); });
        pop_group();
    }

    if (buffer_mesh->bounding_box.is_valid()) {
        const glm::vec3 size = buffer_mesh->bounding_box.max - buffer_mesh->bounding_box.min;
        float volume = buffer_mesh->bounding_box.volume();
        add_entry("Bounding box size",   [=](){ ImGui::Text("%f, %f, %f", size.x, size.y, size.z); });
        add_entry("Bounding box volume", [=](){ ImGui::Text("%f", volume); });
    }
    if (buffer_mesh->bounding_sphere.radius > 0.0f) {
        add_entry("Bounding sphere radius", [=](){ ImGui::Text("%f", buffer_mesh->bounding_sphere.radius); });
        add_entry("Bounding box volume",    [=](){ ImGui::Text("%f", buffer_mesh->bounding_sphere.volume()); });
    }

    pop_group();
}

void Properties::primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace)
{
    if (primitive_raytrace == nullptr) {
        return;
    }
    const erhe::primitive::Buffer_mesh& buffer_mesh = primitive_raytrace->get_raytrace_mesh();
    buffer_mesh_properties("Raytrace Buffer Mesh", &buffer_mesh);
}

void Properties::shape_properties(const char* label, erhe::primitive::Primitive_shape* shape)
{
    ERHE_PROFILE_FUNCTION();

    if (shape == nullptr) {
        return;
    }

    push_group(label, ImGuiTreeNodeFlags_None, m_indent);

    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry_const();
    if (geometry) {
        geometry_properties(*geometry.get());
    }

    primitive_raytrace_properties(&shape->get_raytrace());

    pop_group();
}

void Properties::mesh_properties(erhe::scene::Mesh& mesh)
{
    ERHE_PROFILE_FUNCTION();

    const auto* node = mesh.get_node();
    auto* scene_root = static_cast<Scene_root*>(node->get_item_host());
    if (scene_root == nullptr) {
        // Mesh host not set
        return;
    }

    add_entry("Layer ID", [&](){ ImGui::Text("%u %s", static_cast<unsigned int>(mesh.layer_id), layer_name(mesh.layer_id)); });

    if (mesh.skin) {
        skin_properties(*mesh.skin.get());
    }

    auto& material_library = scene_root->content_library()->materials;

    push_group("Primitives", ImGuiTreeNodeFlags_None, m_indent);
    int primitive_index = 0;
    for (auto& primitive : mesh.get_mutable_primitives()) { // may edit material
        std::string label = fmt::format("Primitive {}", primitive_index++);
        push_group(label, ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        add_entry("Material", [&](){ material_library->combo(m_context, "##", primitive.material, false); });
        if (primitive.material) {
            add_entry("Material Buffer Index", [&](){ ImGui::Text("%u", primitive.material->material_buffer_index); });
        }
        if (primitive.render_shape) {
            shape_properties("Render shape", primitive.render_shape.get());
            const erhe::primitive::Buffer_mesh& renderable_mesh = primitive.render_shape->get_renderable_mesh();
            buffer_mesh_properties("Renderable Buffer Mesh", &renderable_mesh);
        }
        if (primitive.collision_shape) {
            shape_properties("Collision shape", primitive.collision_shape.get());
        }
        pop_group();
    }
    pop_group();

    push_group("Mesh Raytrace", ImGuiTreeNodeFlags_None, m_indent);
    const auto* mesh_rt_scene = mesh.get_rt_scene();
    if (mesh_rt_scene != nullptr) {
        add_entry("RT Scene", [=](){ ImGui::TextUnformatted(mesh_rt_scene->debug_label().data()); });
    }
    const auto& rt_primitives = mesh.get_rt_primitives();
    if (!rt_primitives.empty()) {
        push_group("Raytrace Primitives", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        for (const auto& rt_primitive : rt_primitives) {
            std::string label = fmt::format("Raytrace Primitive {}", primitive_index++);
            const auto* rt_instance = rt_primitive->rt_instance.get();
            const auto* rt_scene    = rt_primitive->rt_scene.get();
            push_group(label, ImGuiTreeNodeFlags_DefaultOpen, m_indent);
            add_entry("Mesh",            [&](){ ImGui::TextUnformatted((rt_primitive->mesh != nullptr) ? rt_primitive->mesh->get_name().c_str() : "(nullptr)"); });
            add_entry("Primitive Index", [&](){ ImGui::Text("%zu", rt_primitive->primitive_index); });
            add_entry("RT Instance",     [=](){ ImGui::TextUnformatted((rt_instance != nullptr) ? rt_instance->debug_label().data() : "(nullptr)"); });
            add_entry("RT Scene",        [=](){ ImGui::TextUnformatted((rt_scene != nullptr) ? rt_scene->debug_label().data() : "(nullptr)"); });
            pop_group();
        }
        pop_group();
    }
    pop_group();
}

void Properties::rendertarget_properties(Rendertarget_mesh& rendertarget)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Width",            [&](){ ImGui::Text("%f", rendertarget.width()); });
    add_entry("Height",           [&](){ ImGui::Text("%f", rendertarget.height()); });
    add_entry("Pixels per Meter", [&](){ ImGui::Text("%f", static_cast<float>(rendertarget.pixels_per_meter())); });
}

void Properties::brush_placement_properties(Brush_placement& brush_placement)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Brush",  [&](){ ImGui::TextUnformatted(brush_placement.get_brush()->get_name().c_str()); });
    add_entry("Facet",  [&](){ ImGui::Text("%u", brush_placement.get_facet()); });
    add_entry("Corner", [&](){ ImGui::Text("%u", brush_placement.get_corner()); });
}

#endif

void Properties::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
#endif
}

void Properties::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar();
#endif
}

void Properties::node_physics_properties(Node_physics& node_physics)
{
    ERHE_PROFILE_FUNCTION();

    erhe::physics::IRigid_body* rigid_body = node_physics.get_rigid_body();
    if (rigid_body == nullptr) {
        return;
    }

    const glm::mat4 transform = rigid_body->get_world_transform();
    const glm::vec3 pos       = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    add_entry("Rigid Body", [=](){ ImGui::TextUnformatted(rigid_body->get_debug_label()); });
    add_entry("Position",   [=](){ ImGui::Text("%.2f, %.2f, %.2f", pos.x, pos.y, pos.z); });
    add_entry("Is Active",  [=](){ ImGui::TextUnformatted(rigid_body->is_active() ? "Yes" : "No"); });

    //bool allow_sleeping = rigid_body->get_allow_sleeping();
    //ImGui::Text("Allow Sleeping: %s", allow_sleeping ? "Yes" : "No");
    //if (allow_sleeping && ImGui::Button("Disallow Sleeping")) {
    //    rigid_body->set_allow_sleeping(false);
    //}
    //if (!allow_sleeping && ImGui::Button("Allow Sleeping")) {
    //    rigid_body->set_allow_sleeping(true);
    //}

    const std::shared_ptr<erhe::physics::ICollision_shape> collision_shape = rigid_body->get_collision_shape();
    if (collision_shape) {
        const glm::vec3 com = collision_shape->get_center_of_mass();
        add_entry("Collision Shape",      [=](){ ImGui::TextUnformatted(collision_shape->describe().c_str()); });
        add_entry("Local Center of Mass", [=](){ ImGui::Text("%.2f, %.2f, %.2f", com.x, com.y, com.z); });
    }

    add_entry("Mass", [rigid_body]() {
        float           mass    = rigid_body->get_mass();
        const glm::mat4 inertia = rigid_body->get_local_inertia();
        if (ImGui::SliderFloat("##", &mass, 0.01f, 1000.0f)) {
            rigid_body->set_mass_properties(mass, inertia);
        }
    });
    add_entry("Friction", [rigid_body]() {
        float friction = rigid_body->get_friction();
        if (ImGui::SliderFloat("##", &friction, 0.0f, 1.0f)) {
            rigid_body->set_friction(friction);
        }
    });
    add_entry("Restitution", [rigid_body]() {
        float restitution = rigid_body->get_restitution();
        if (ImGui::SliderFloat("##", &restitution, 0.0f, 1.0f)) {
            rigid_body->set_restitution(restitution);
        }
    });

    // Static bodies don't have access to these properties, at least in Jolt
    if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_static) {
        add_entry("Angular Dampening", [rigid_body](){
            float angular_damping = rigid_body->get_angular_damping();
            float linear_damping  = rigid_body->get_linear_damping();
            if (ImGui::SliderFloat("##", &angular_damping, 0.0f, 1.0f)) {
                rigid_body->set_damping(linear_damping, angular_damping);
            }
        });
        add_entry("Linear Dampening", [rigid_body](){
            float angular_damping = rigid_body->get_angular_damping();
            float linear_damping  = rigid_body->get_linear_damping();
            if (ImGui::SliderFloat("##", &linear_damping, 0.0f, 1.0f)) {
                rigid_body->set_damping(linear_damping, angular_damping);
            }
        });

        add_entry("Local Inertia", [rigid_body](){
            const glm::mat4 local_inertia = rigid_body->get_local_inertia();
            float floats[4] = { local_inertia[0][0], local_inertia[1][1], local_inertia[2][2] };
            ImGui::InputFloat3("##", floats);
            // TODO floats back to rigid body?
        });
    }

    add_entry("Motion Mode", [rigid_body](){
        const auto motion_mode = rigid_body->get_motion_mode();
        int i_motion_mode = static_cast<int>(motion_mode);
        if (ImGui::Combo("##", &i_motion_mode, erhe::physics::c_motion_mode_strings, IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings))) {
            rigid_body->set_motion_mode(static_cast<erhe::physics::Motion_mode>(i_motion_mode));
        }
    });
}

void Properties::item_flags(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_FUNCTION();

    push_group("Flags", ImGuiTreeNodeFlags_None, m_indent);

    using namespace erhe::bit;
    using Item_flags = erhe::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        add_entry(Item_flags::c_bit_labels[bit_position], [item, bit_position, flags, this]() {
            const uint64_t bit_mask = uint64_t{1} << bit_position;
            bool           value    = test_all_rhs_bits_set(flags, bit_mask);
            if (ImGui::Checkbox("##", &value)) {
                if (bit_mask == Item_flags::selected) {
                    if (value) {
                        m_context.selection->add_to_selection(item);
                    } else {
                        m_context.selection->remove_from_selection(item);
                    }
                } else {
                    item->set_flag_bits(bit_mask, value);
                }
            }
        });
    }

    pop_group();
}

[[nodiscard]] auto show_item_details(const erhe::Item_base* const item)
{
    return
        !is_physics         (item) &&
        !is_frame_controller(item) &&
        !is_rendertarget    (item);
}

void Properties::item_properties(const std::shared_ptr<erhe::Item_base>& item_in)
{
    ERHE_PROFILE_FUNCTION();

    const auto& content_library_node = std::dynamic_pointer_cast<Content_library_node   >(item_in);
    const auto& item                 = (content_library_node && content_library_node->item) ? content_library_node->item : item_in;

    const auto& node_physics         = std::dynamic_pointer_cast<Node_physics           >(item);
    const auto& rendertarget         = std::dynamic_pointer_cast<Rendertarget_mesh      >(item);
    const auto& camera               = std::dynamic_pointer_cast<erhe::scene::Camera    >(item);
    const auto& light                = std::dynamic_pointer_cast<erhe::scene::Light     >(item);
    const auto& mesh                 = std::dynamic_pointer_cast<erhe::scene::Mesh      >(item);
    const auto& node                 = std::dynamic_pointer_cast<erhe::scene::Node      >(item);
    const auto& brush_placement      = std::dynamic_pointer_cast<Brush_placement        >(item);
    const auto& texture              = std::dynamic_pointer_cast<erhe::graphics::Texture>(item);

    ////const bool default_open = !node_physics && !content_library_node && !node;

    if (!item) {
        return;
    }

    std::string group_label = fmt::format("{} {}", item->get_type_name().data(), item->get_name());
    push_group(group_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, m_indent);
    if (show_item_details(item.get())) {
        std::string label_name = fmt::format("{} Name", item->get_type_name());
        add_entry(label_name, [item]() {
            std::string name = item->get_name();
            const bool enter_pressed = ImGui::InputText("##", &name, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter_pressed || ImGui::IsItemDeactivatedAfterEdit()) { // TODO
                if (name != item->get_name()) {
                    item->set_name(name);
                }
            }
        });

        add_entry("Id", [item]() { ImGui::Text("%u", static_cast<unsigned int>(item->get_id())); });

        item_flags(item);

        //// if (!erhe::scene::is_light(item)) { // light uses light color, so hide item color
        ////     glm::vec4 color = item->get_wireframe_color();
        ////     if (
        ////         ImGui::ColorEdit4(
        ////             "Item_base Color",
        ////             &color.x,
        ////             ImGuiColorEditFlags_Float |
        ////             ImGuiColorEditFlags_NoInputs
        ////         )
        ////     ) {
        ////         item->set_wireframe_color(color);
        ////     }
        //// }
    }

    if (node_physics) {
        node_physics_properties(*node_physics);
    }

    if (camera) {
        camera_properties(*camera);
    }

    if (light) {
        light_properties(*light);
    }

    if (mesh) {
        mesh_properties(*mesh);
    }

    if (rendertarget) {
        rendertarget_properties(*rendertarget);
    }

    if (brush_placement) {
        brush_placement_properties(*brush_placement);
    }

    if (texture) {
        texture_properties(texture);
    }

    if (node) {
        push_group("Attachments", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        for (auto& attachment : node->get_attachments()) {
            item_properties(attachment);
        }
        pop_group();
    }

    pop_group();
}

void Properties::material_properties()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const std::shared_ptr<erhe::primitive::Material> selected_material = m_context.selection->get<erhe::primitive::Material>();
    if (!selected_material) {
        return;
    }
    push_group("Material", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, m_indent);

    add_entry("Name", [=]() {
        std::string name = selected_material->get_name();
        if (ImGui::InputText("##", &name)) {
            selected_material->set_name(name);
        }
    });

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float area_size_0    = std::min(available_size.x, available_size.y);
    const int   area_size      = std::max(1, static_cast<int>(area_size_0));
    m_context.material_preview->set_area_size(area_size);
    m_context.material_preview->update_rendertarget(*m_context.graphics_instance);
    m_context.material_preview->render_preview(selected_material);
    m_context.material_preview->show_preview();
    auto* node = m_context.brdf_slice->get_node();
    if (node != nullptr) {
        push_group("BRDF Slice", ImGuiTreeNodeFlags_None);
        node->set_material(selected_material);
        add_entry("BRDF", [this, area_size]() {
            m_context.brdf_slice->show_brdf_slice(area_size);
        });
        pop_group();
    }
    add_entry("Metallic",    [=](){ ImGui::SliderFloat("##", &selected_material->metallic,     0.0f,  1.0f); });
    add_entry("Reflectance", [=](){ ImGui::SliderFloat("##", &selected_material->reflectance,  0.35f, 1.0f); });
    add_entry("Roughness X", [=](){ ImGui::SliderFloat("##", &selected_material->roughness.x,  0.1f,  0.8f); });
    add_entry("Roughness Y", [=](){ ImGui::SliderFloat("##", &selected_material->roughness.y,  0.1f,  0.8f); });
    add_entry("Base Color",  [=](){ ImGui::ColorEdit4 ("##", &selected_material->base_color.x, ImGuiColorEditFlags_Float); });
    add_entry("Emissive",    [=](){ ImGui::ColorEdit4 ("##", &selected_material->emissive.x,   ImGuiColorEditFlags_Float); });
    add_entry("Opacity",     [=](){ ImGui::SliderFloat("##", &selected_material->opacity,      0.0f,  1.0f); });

    Scene_root* scene_root = m_context.scene_commands->get_scene_root(selected_material.get());
    if (scene_root != nullptr) {
        const std::shared_ptr<Content_library>& content_library = scene_root->content_library();
        if (content_library) {
            const std::shared_ptr<Content_library_node>& textures = content_library->textures;
            if (textures) {
                add_entry("Base Color Texture",         [=](){ textures->combo(m_context, "##", selected_material->textures.base_color,         true); });
                add_entry("Metallic Roughness Texture", [=](){ textures->combo(m_context, "##", selected_material->textures.metallic_roughness, true); });
            }
        }
    }

    pop_group();
#endif
}

void Properties::imgui()
{
    ERHE_PROFILE_FUNCTION();

    reset();

    const auto& selection = m_context.selection->get_selection();
    int id = 0;
    for (const auto& item : selection) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        ERHE_VERIFY(item);
        item_properties(item);
    }

    const auto selected_animation = m_context.selection->get<erhe::scene::Animation>();
    if (selected_animation) {
        animation_properties(*selected_animation.get());
    }

    const auto selected_skin = m_context.selection->get<erhe::scene::Skin>();
    if (selected_skin) {
        skin_properties(*selected_skin.get());
    }

    material_properties();

    show_entries();
}

} // namespace editor
