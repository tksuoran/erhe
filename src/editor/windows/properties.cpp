#include "windows/properties.hpp"

#include "animation/animation_curve.hpp"
#include "animation/timeline_window.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "content_library/brdf_slice.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"

#include "app_scenes.hpp"
#include "preview/material_preview.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/frame_controller.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_physics/physics_material.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_item.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <cmath>
#include <limits>

#define ICON_MDI_AXIS_ARROW                               "\xf3\xb0\xb5\x89" // U+F0D49
#define ICON_MDI_AXIS_ARROW_LOCK                          "\xf3\xb0\xb5\x8a" // U+F0D4A
#define ICON_MDI_LOCK_OPEN                                "\xf3\xb0\x8c\xbf" // U+F033F
#define ICON_MDI_LOCK                                     "\xf3\xb0\x8c\xbe" // U+F033E
#define ICON_MDI_LOCK_OPEN_VARIANT_OUTLINE                "\xf3\xb0\xbf\x87" // U+F0FC7
#define ICON_MDI_LOCK_OUTLINE                             "\xf3\xb0\x8d\x81" // U+F0341

namespace editor {

Properties::Properties(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : Imgui_window{imgui_renderer, imgui_windows, "Properties", "properties"}
    , m_context   {app_context}
{
}

void Properties::animation_properties(erhe::scene::Animation& animation)
{
    ERHE_PROFILE_FUNCTION();

    float start_time = animation.get_first_time();
    float end_time   = animation.get_last_time();

    add_entry("Start Time", [=](){ ImGui::Text("%.4f", start_time); });
    add_entry("End Time",   [=](){ ImGui::Text("%.4f", end_time); });
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

    //if (start_time > end_time) {
    //    std::swap(start_time, end_time);
    //}
    const float timeline_length = end_time - start_time;
    m_context.timeline_window->set_timeline_length(timeline_length);
    const float time_in_timeline = m_context.timeline_window->get_play_position();
    animation.apply(start_time + time_in_timeline);

    m_context.app_message_bus->animation_update.send_message(
        Animation_update_message{}
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
        push_group("Projection", ImGuiTreeNodeFlags_None | ImGuiTreeNodeFlags_DefaultOpen, m_indent);
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
                break;
            }

            case erhe::scene::Projection::Type::perspective_xr: {
                add_entry("Fov Left",  [=](){ ImGui::SliderFloat("##", &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Right", [=](){ ImGui::SliderFloat("##", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Up",    [=](){ ImGui::SliderFloat("##", &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                add_entry("Fov Down",  [=](){ ImGui::SliderFloat("##", &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);});
                break;
            }

            case erhe::scene::Projection::Type::perspective_horizontal: {
                add_entry("Fov X",  [=](){ ImGui::SliderFloat("##", &projection->fov_x,  0.0f, glm::pi<float>()); });
                break;
            }

            case erhe::scene::Projection::Type::perspective_vertical: {
                add_entry("Fov Y",  [=](){ ImGui::SliderFloat("##", &projection->fov_y,  0.0f, glm::pi<float>()); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_horizontal: {
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_vertical: {
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal: {
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_rectangle: {
                add_entry("Left",   [=](){ ImGui::SliderFloat("##", &projection->ortho_left,   0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Width",  [=](){ ImGui::SliderFloat("##", &projection->ortho_width,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Bottom", [=](){ ImGui::SliderFloat("##", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Height", [=](){ ImGui::SliderFloat("##", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::generic_frustum: {
                add_entry("Left",   [=](){ ImGui::SliderFloat("##", &projection->frustum_left,   0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Right",  [=](){ ImGui::SliderFloat("##", &projection->frustum_right,  0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Bottom", [=](){ ImGui::SliderFloat("##", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                add_entry("Top",    [=](){ ImGui::SliderFloat("##", &projection->frustum_top,    0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic); });
                break;
            }

            case erhe::scene::Projection::Type::other: {
                // TODO(tksuoran@gmail.com): Implement
                break;
            }
        }
        add_entry("Z Near", [=](){ImGui::SliderFloat("##", &projection->z_near, 0.0f, 1000.0f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);});
        add_entry("Z Far",  [=](){ImGui::SliderFloat("##", &projection->z_far,  0.0f, 1000.0f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);});

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
    const auto* node = light.get_node();
    if (node != nullptr) {
        auto* scene_root = static_cast<Scene_root*>(node->get_item_host());
        if (scene_root != nullptr) {
            const auto& layers = scene_root->layers();
            ImGui::ColorEdit3(
                "Ambient",
                &layers.light()->ambient_light.x,
                ImGuiColorEditFlags_Float
            );
        }
    }
}

void Properties::layout_properties(erhe::scene::Layout& layout)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Type", [&layout]() {
        erhe::imgui::make_combo(
            "##",
            layout.type,
            erhe::scene::Layout::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Layout::c_type_strings)
        );
    });
    add_entry("Volume Min", [&layout]() { ImGui::DragFloat3("##", &layout.volume.min.x, 0.01f); });
    add_entry("Volume Max", [&layout]() { ImGui::DragFloat3("##", &layout.volume.max.x, 0.01f); });
    add_entry("Primary", [&layout]() {
        erhe::imgui::make_combo("##", layout.primary, erhe::scene::Layout::c_axis_direction_strings, IM_ARRAYSIZE(erhe::scene::Layout::c_axis_direction_strings));
    });
    add_entry("Secondary", [&layout]() {
        erhe::imgui::make_combo("##", layout.secondary, erhe::scene::Layout::c_axis_direction_strings, IM_ARRAYSIZE(erhe::scene::Layout::c_axis_direction_strings));
    });
    add_entry("Tertiary", [&layout]() {
        erhe::imgui::make_combo("##", layout.tertiary, erhe::scene::Layout::c_axis_direction_strings, IM_ARRAYSIZE(erhe::scene::Layout::c_axis_direction_strings));
    });
    add_entry("Gap", [&layout]() { ImGui::DragFloat3("##", &layout.gap.x, 0.01f, 0.0f, 10000.0f); });

    if (layout.type == erhe::scene::Layout_type::grid) {
        add_entry("Grid Tracks", [&layout]() { ImGui::DragInt3("##", &layout.grid_track_count.x, 0.1f, 1, 1000); });

        static const char* const c_grid_size_labels[] = { "Sizes X", "Sizes Y", "Sizes Z" };
        for (int axis = 0; axis < 3; ++axis) {
            add_entry(c_grid_size_labels[axis], [&layout, axis]() {
                std::vector<float>& extents = layout.grid_track_extent[static_cast<std::size_t>(axis)];
                const int count = (layout.grid_track_count[axis] > 1) ? layout.grid_track_count[axis] : 1;
                bool custom = !extents.empty();
                if (ImGui::Checkbox("Custom", &custom)) {
                    if (custom) {
                        const float total = layout.volume.max[axis] - layout.volume.min[axis];
                        const float per   = (total > 0.0f) ? (total / static_cast<float>(count)) : 0.0f;
                        extents.assign(static_cast<std::size_t>(count), per);
                    } else {
                        extents.clear();
                    }
                }
                if (!extents.empty()) {
                    extents.resize(static_cast<std::size_t>(count), 0.0f); // keep in sync with track count
                    for (int k = 0; k < count; ++k) {
                        ImGui::PushID(k);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(48.0f);
                        ImGui::DragFloat("##e", &extents[static_cast<std::size_t>(k)], 0.01f, 0.0f, 10000.0f);
                        ImGui::PopID();
                    }
                }
            });
        }
    }
}

void Properties::layout_item_properties(erhe::scene::Layout_item& layout_item)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Align X", [&layout_item]() {
        erhe::imgui::make_combo("##", layout_item.alignment[0], erhe::scene::Layout_item::c_alignment_strings, IM_ARRAYSIZE(erhe::scene::Layout_item::c_alignment_strings));
    });
    add_entry("Align Y", [&layout_item]() {
        erhe::imgui::make_combo("##", layout_item.alignment[1], erhe::scene::Layout_item::c_alignment_strings, IM_ARRAYSIZE(erhe::scene::Layout_item::c_alignment_strings));
    });
    add_entry("Align Z", [&layout_item]() {
        erhe::imgui::make_combo("##", layout_item.alignment[2], erhe::scene::Layout_item::c_alignment_strings, IM_ARRAYSIZE(erhe::scene::Layout_item::c_alignment_strings));
    });
    add_entry("Margin Min", [&layout_item]() { ImGui::DragFloat3("##", &layout_item.margin_min.x, 0.01f); });
    add_entry("Margin Max", [&layout_item]() { ImGui::DragFloat3("##", &layout_item.margin_max.x, 0.01f); });
    add_entry("Auto Cell",  [&layout_item]() { ImGui::Checkbox  ("##", &layout_item.grid_cell_auto); });
    if (!layout_item.grid_cell_auto) {
        add_entry("Grid Cell", [&layout_item]() { ImGui::DragInt3("##", &layout_item.grid_cell.x, 0.1f, 0, 1000); });
    }
    add_entry("Grid Span",  [&layout_item]() { ImGui::DragInt3  ("##", &layout_item.grid_span.x, 0.1f, 1, 1000); });
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

    add_entry("Name",   [texture](){ ImGui::TextUnformatted(texture->get_name().c_str()); });
    add_entry("Width",  [texture](){ ImGui::Text("%d", texture->get_width()); });
    add_entry("Height", [texture](){ ImGui::Text("%d", texture->get_height()); });
    add_entry("Format", [texture](){ ImGui::TextUnformatted(erhe::dataformat::c_str(texture->get_pixelformat())); });

    add_entry("Preview", [this, texture](){
        // TODO Draw to available size respecting aspect ratio
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = texture, //texture.get(),
                .width             = texture->get_width(),
                .height            = texture->get_height(),
                .debug_label       = "Properties::texture_properties()"
            }
        );
    });

    pop_group();
}

void Properties::geometry_properties(erhe::geometry::Geometry& geometry)
{
    ERHE_PROFILE_FUNCTION();

    push_group("Geometry", ImGuiTreeNodeFlags_DefaultOpen, m_indent);

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

    push_group(label, ImGuiTreeNodeFlags_DefaultOpen, m_indent);

    add_entry("Fill Triangles", [=](){ ImGui::Text("%zu", buffer_mesh->triangle_fill_indices.get_triangle_count()); });
    add_entry("Edge Lines",     [=](){ ImGui::Text("%zu", buffer_mesh->edge_line_indices.get_line_count()); });
    add_entry("Corner Points",  [=](){ ImGui::Text("%zu", buffer_mesh->corner_point_indices.get_point_count()); });
    if (m_context.developer_mode) {
        add_entry("Centroid Points", [=](){ ImGui::Text("%zu", buffer_mesh->polygon_centroid_indices.get_point_count()); });
    }
    add_entry("Indices",     [=](){ ImGui::Text("%zu", buffer_mesh->index_buffer_range.count); });
    add_entry("Index Bytes", [=](){ ImGui::Text("%zu", buffer_mesh->index_buffer_range.get_byte_size()); });
    for (size_t i = 0, end = buffer_mesh->vertex_buffer_ranges.size(); i < end; ++i) {
        const erhe::primitive::Buffer_range& vertex_buffer_range = buffer_mesh->vertex_buffer_ranges.at(i);
        if (vertex_buffer_range.count > 0) {
            while (m_vertex_stream_labels.size() <= i) {
                m_vertex_stream_labels.push_back(fmt::format("Vertex stream {}", m_vertex_stream_labels.size()));
            }
            push_group(m_vertex_stream_labels.at(i).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
            add_entry("Vertices",     [=](){ ImGui::Text("%zu", vertex_buffer_range.count); });
            add_entry("Vertex Bytes", [=](){ ImGui::Text("%zu", vertex_buffer_range.get_byte_size()); });
            pop_group();
        }
    }

    if (buffer_mesh->bounding_box.is_valid()) {
        const glm::vec3 size = buffer_mesh->bounding_box.max - buffer_mesh->bounding_box.min;
        const float volume = buffer_mesh->bounding_box.volume();
        add_entry("Bounding box size",   [=](){ ImGui::Text("%f, %f, %f", size.x, size.y, size.z); });
        add_entry("Bounding box volume", [=](){ ImGui::Text("%f", volume); });
    }
    if (buffer_mesh->bounding_sphere.radius > 0.0f) {
        add_entry("Bounding sphere radius", [=](){ ImGui::Text("%f", buffer_mesh->bounding_sphere.radius); });
        add_entry("Bounding sphere volume", [=](){ ImGui::Text("%f", buffer_mesh->bounding_sphere.volume()); });
    }

    pop_group();
}

void Properties::primitive_raytrace_properties(erhe::primitive::Primitive_raytrace* primitive_raytrace)
{
    if (!m_context.developer_mode) {
        return;
    }
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

    if (m_context.developer_mode) {
        push_group(label, ImGuiTreeNodeFlags_None, m_indent);
    }

    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry_const();
    if (geometry) {
        geometry_properties(*geometry.get());
    }

    if (m_context.developer_mode) {
        primitive_raytrace_properties(&shape->get_raytrace());
        pop_group();
    }
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

    if (m_context.developer_mode) {
        add_entry("Layer ID", [&](){ ImGui::Text("%u %s", static_cast<unsigned int>(mesh.layer_id), layer_name(mesh.layer_id)); });
    }

    if (mesh.skin) {
        skin_properties(*mesh.skin.get());
    }

    auto& material_library = scene_root->get_content_library()->materials;

    if (m_context.developer_mode) {
        push_group("Primitives", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
    }
    int primitive_index = 0;
    for (erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_mutable_primitives()) { // may edit material
        while (m_primitive_labels.size() <= primitive_index) {
            m_primitive_labels.push_back(fmt::format("Primitive {}", m_primitive_labels.size()));
        }
        push_group(m_primitive_labels.at(primitive_index).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        add_entry("Material", [&](){ material_library->combo(m_context, "##", mesh_primitive.material, false); });
        if (m_context.developer_mode) {
            if (mesh_primitive.material) {
                add_entry("Material Buffer Index", [&](){ ImGui::Text("%u", mesh_primitive.material->material_buffer_index); });
            }
        }

        erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
        if (primitive.render_shape) {
            shape_properties("Render shape", primitive.render_shape.get());
            const erhe::primitive::Buffer_mesh& renderable_mesh = primitive.render_shape->get_renderable_mesh();
            buffer_mesh_properties("Renderable Buffer Mesh", &renderable_mesh);
        }
        if (m_context.developer_mode && primitive.collision_shape) {
            shape_properties("Collision shape", primitive.collision_shape.get());
        }
        pop_group();
    }
    if (m_context.developer_mode) {
        pop_group();
    }

    if (m_context.developer_mode) {
        push_group("Mesh Raytrace", ImGuiTreeNodeFlags_None, m_indent);
        const auto* mesh_rt_scene = mesh.get_rt_scene();
        if (mesh_rt_scene != nullptr) {
            add_entry("RT Scene", [=](){ ImGui::TextUnformatted(mesh_rt_scene->debug_label().data()); });
        }
        const auto& rt_primitives = mesh.get_rt_primitives();
        if (!rt_primitives.empty()) {
            push_group("Raytrace Primitives", ImGuiTreeNodeFlags_None, m_indent);
            for (const auto& rt_primitive : rt_primitives) {
                while (m_rt_primitive_labels.size() <= primitive_index) {
                    m_rt_primitive_labels.push_back(fmt::format("Raytrace Primitive {}", m_rt_primitive_labels.size()));
                }

                const auto* rt_instance = rt_primitive->rt_instance.get();
                const auto* rt_scene    = rt_primitive->rt_scene.get();
                push_group(m_rt_primitive_labels.at(primitive_index).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
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
}

void Properties::rendertarget_properties(Rendertarget_mesh& rendertarget)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Width",            [&](){ ImGui::Text("%f", rendertarget.get_width()); });
    add_entry("Height",           [&](){ ImGui::Text("%f", rendertarget.get_height()); });
    add_entry("Pixels per Meter", [&](){ ImGui::Text("%f", static_cast<float>(rendertarget.get_pixels_per_meter())); });
}

void Properties::brush_properties(const std::shared_ptr<Brush>& brush)
{
    ERHE_PROFILE_FUNCTION();

    push_group("Brush", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, m_indent);

    add_entry("Material", [brush, this]() {
        const std::shared_ptr<erhe::primitive::Material>& brush_material = brush->get_material();
        const char* material_name = brush_material ? brush_material->get_name().c_str() : "(none)";
        ImGui::Button(material_name, ImVec2{-FLT_MIN, 0.0f});

        // Accept material drag-and-drop from content library
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* node_payload = ImGui::AcceptDragDropPayload(
                Content_library_node::static_type_name.data()
            );
            if (node_payload != nullptr) {
                erhe::Item_base* item_base = *static_cast<erhe::Item_base**>(node_payload->Data);
                Content_library_node* node = dynamic_cast<Content_library_node*>(item_base);
                if (node != nullptr) {
                    std::shared_ptr<erhe::primitive::Material> material =
                        std::dynamic_pointer_cast<erhe::primitive::Material>(node->item);
                    if (material) {
                        brush->set_material(material);
                    }
                }
            }
            // Also accept direct Material payload
            const ImGuiPayload* mat_payload = ImGui::AcceptDragDropPayload("Material");
            if (mat_payload != nullptr) {
                erhe::Item_base* item_base = *static_cast<erhe::Item_base**>(mat_payload->Data);
                erhe::primitive::Material* raw_material = dynamic_cast<erhe::primitive::Material*>(item_base);
                if (raw_material != nullptr) {
                    brush->set_material(
                        std::dynamic_pointer_cast<erhe::primitive::Material>(raw_material->shared_from_this())
                    );
                }
            }
            ImGui::EndDragDropTarget();
        }
    });

    pop_group();
}

void Properties::brush_placement_properties(Brush_placement& brush_placement)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Brush", [&](){ ImGui::TextUnformatted(brush_placement.get_brush()->get_name().c_str()); });
    if (m_context.developer_mode) {
        add_entry(
            "Facet",
            [&]() {
                if (brush_placement.get_facet() == GEO::NO_FACET) {
                    ImGui::TextUnformatted("--");
                } else {
                    ImGui::Text("%u", brush_placement.get_facet());
                }
            }
        );
        add_entry(
            "Corner",
            [&]() {
                if (brush_placement.get_corner() == GEO::NO_FACET) {
                    ImGui::TextUnformatted("--");
                } else {
                    ImGui::Text("%u", brush_placement.get_corner());
                }
            }
        );
    }

    std::shared_ptr<Brush> brush = brush_placement.get_brush();
    if (!brush) {
        return;
    }
    push_group("Polygons", ImGuiTreeNodeFlags_DefaultOpen);
    const std::map<GEO::index_t, std::vector<GEO::index_t>>& facets = brush->get_corner_count_to_facets();
    for (const auto& i : facets) {
        const GEO::index_t corner_count  = i.first;
        const std::size_t  polygon_count = i.second.size();

        while (m_ngon_labels.size() <= corner_count) {
            m_ngon_labels.push_back(fmt::format("{}-gons", m_ngon_labels.size()));
        }

        add_entry(
            m_ngon_labels.at(corner_count).c_str(),
            [polygon_count]() {
                ImGui::Text("%zu", polygon_count);
            }
        );
    }
    pop_group();
}

void Properties::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
}

void Properties::on_end()
{
    ImGui::PopStyleVar();
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

    add_entry("Motion Mode", [&node_physics](){
        // Display / set the user-facing motion mode through Node_physics so
        // that the intended mode and the rigid body stay consistent (static
        // trigger bodies are internally kinematic non-physical).
        int i_motion_mode = static_cast<int>(node_physics.get_motion_mode());
        if (ImGui::Combo("##", &i_motion_mode, erhe::physics::c_motion_mode_strings, IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings))) {
            if (i_motion_mode != static_cast<int>(erhe::physics::Motion_mode::e_invalid)) {
                node_physics.set_motion_mode(static_cast<erhe::physics::Motion_mode>(i_motion_mode));
            }
        }
    });

    add_entry("Is Trigger", [&node_physics](){
        bool is_trigger = node_physics.is_trigger();
        if (ImGui::Checkbox("##", &is_trigger)) {
            node_physics.set_trigger(is_trigger);
        }
    });

    add_entry("Gravity Factor", [&node_physics](){
        float gravity_factor = node_physics.get_gravity_factor();
        if (ImGui::SliderFloat("##", &gravity_factor, 0.0f, 2.0f)) {
            node_physics.set_gravity_factor(gravity_factor);
        }
    });

    add_entry(
        "Initial Linear Velocity",
        [&node_physics](){
            glm::vec3 velocity = node_physics.get_initial_linear_velocity();
            if (ImGui::DragFloat3("##", &velocity.x, 0.01f)) {
                node_physics.set_initial_linear_velocity(velocity);
            }
        },
        "Applied when the rigid body is (re)created"
    );

    add_entry(
        "Initial Angular Velocity",
        [&node_physics](){
            glm::vec3 velocity = node_physics.get_initial_angular_velocity();
            if (ImGui::DragFloat3("##", &velocity.x, 0.01f)) {
                node_physics.set_initial_angular_velocity(velocity);
            }
        },
        "Applied when the rigid body is (re)created"
    );

    add_entry(
        "Center of Mass",
        [&node_physics](){
            glm::vec3 offset = node_physics.get_center_of_mass_offset();
            ImGui::DragFloat3("##", &offset.x, 0.01f);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                node_physics.set_center_of_mass_offset(offset);
            }
        },
        "Center of mass offset; applied when the edit is completed (recreates the rigid body)"
    );

    erhe::scene::Node* node = node_physics.get_node();
    Scene_root* scene_root = (node != nullptr) ? static_cast<Scene_root*>(node->get_item_host()) : nullptr;
    const std::shared_ptr<Content_library> content_library = (scene_root != nullptr) ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
    if (content_library) {
        add_entry("Physics Material", [this, &node_physics, content_library]() {
            std::shared_ptr<erhe::physics::Physics_material> material = node_physics.get_physics_material();
            if (content_library->physics_materials->combo(m_context, "##", material, true)) {
                node_physics.set_physics_material(material);
            }
        });
        add_entry("Collision Filter", [this, &node_physics, content_library]() {
            std::shared_ptr<erhe::physics::Collision_filter> filter = node_physics.get_collision_filter();
            if (content_library->collision_filters->combo(m_context, "##", filter, true)) {
                node_physics.set_collision_filter(filter);
            }
        });
    }
}

void Properties::node_joint_properties(Node_joint& node_joint)
{
    ERHE_PROFILE_FUNCTION();

    add_entry(
        "Connected Node",
        [&node_joint]() {
            const std::shared_ptr<erhe::scene::Node> connected_node = node_joint.get_connected_node();
            ImGui::Button(connected_node ? connected_node->get_name().c_str() : "(world)", ImVec2{-FLT_MIN, 0.0f});
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(erhe::scene::Node::static_type_name.data());
                if (payload != nullptr) {
                    erhe::Item_base* item_base = *(static_cast<erhe::Item_base**>(payload->Data));
                    erhe::scene::Node* dropped_node = dynamic_cast<erhe::scene::Node*>(item_base);
                    if ((dropped_node != nullptr) && (dropped_node != node_joint.get_node())) {
                        node_joint.set_connected_node(std::static_pointer_cast<erhe::scene::Node>(dropped_node->shared_from_this()));
                    }
                }
                ImGui::EndDragDropTarget();
            }
        },
        "Drop a node here to connect the joint to it; no connected node constrains to the world"
    );

    add_entry(
        "Connect",
        [this, &node_joint]() {
            if (ImGui::Button("Connect to Selected Node", ImVec2{-FLT_MIN, 0.0f})) {
                const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
                for (const std::shared_ptr<erhe::Item_base>& selected_item : selected_items) {
                    const std::shared_ptr<erhe::scene::Node> selected_node = std::dynamic_pointer_cast<erhe::scene::Node>(selected_item);
                    if (selected_node && (selected_node.get() != node_joint.get_node())) {
                        node_joint.set_connected_node(selected_node);
                        break;
                    }
                }
            }
        },
        "Connects the joint to the first selected node other than the joint's own node"
    );

    erhe::scene::Node* node = node_joint.get_node();
    Scene_root* scene_root = (node != nullptr) ? static_cast<Scene_root*>(node->get_item_host()) : nullptr;
    const std::shared_ptr<Content_library> content_library = (scene_root != nullptr) ? scene_root->get_content_library() : std::shared_ptr<Content_library>{};
    if (content_library) {
        add_entry("Joint Settings", [this, &node_joint, content_library]() {
            std::shared_ptr<erhe::physics::Physics_joint_settings> settings = node_joint.get_settings();
            if (content_library->physics_joints->combo(m_context, "##", settings, true)) {
                node_joint.set_settings(settings);
            }
        });
    }

    add_entry("Enable Collision", [&node_joint]() {
        bool enable_collision = node_joint.get_enable_collision();
        if (ImGui::Checkbox("##", &enable_collision)) {
            node_joint.set_enable_collision(enable_collision);
        }
    });

    add_entry("Constraint", [&node_joint]() {
        ImGui::TextUnformatted((node_joint.get_constraint() != nullptr) ? "Created" : "Pending");
    });

    add_entry(
        "Rebuild",
        [&node_joint]() {
            if (ImGui::Button("Rebuild Joint", ImVec2{-FLT_MIN, 0.0f})) {
                node_joint.rebuild();
            }
        },
        "Recreates the constraint, re-capturing the joint frames; use after editing the shared joint settings or moving the nodes"
    );
}

namespace {

constexpr const char* c_combine_mode_names[] = { "Average", "Minimum", "Maximum", "Multiply" };
constexpr const char* c_drive_type_names  [] = { "Linear", "Angular" };
constexpr const char* c_drive_mode_names  [] = { "Force", "Acceleration" };

// Re-assigns the edited material to every scene body using it so the physics
// backend re-snapshots the values (the per-body snapshot does not track the
// shared item).
void reapply_physics_material(App_context& context, const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
            if (node_physics && (node_physics->get_physics_material() == physics_material)) {
                node_physics->set_physics_material(physics_material);
            }
        }
    }
}

// Re-assigns the edited filter to every scene body using it so the physics
// backend recompiles the filter snapshot.
void reapply_collision_filter(App_context& context, const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
            if (node_physics && (node_physics->get_collision_filter() == collision_filter)) {
                node_physics->set_collision_filter(collision_filter);
            }
        }
    }
}

// Checkbox toggling presence + drag editing the value of an optional float.
void optional_float_editor(std::optional<float>& value, const float default_value)
{
    bool has_value = value.has_value();
    if (ImGui::Checkbox("##has", &has_value)) {
        if (has_value) {
            value = default_value;
        } else {
            value.reset();
        }
    }
    if (value.has_value()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        float editable_value = value.value();
        if (ImGui::DragFloat("##value", &editable_value, 0.01f)) {
            value = editable_value;
        }
    }
}

} // anonymous namespace

void Properties::physics_material_properties(const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Static Friction", [this, physics_material]() {
        if (ImGui::SliderFloat("##", &physics_material->static_friction, 0.0f, 1.0f)) {
            reapply_physics_material(m_context, physics_material);
        }
    });
    add_entry("Dynamic Friction", [this, physics_material]() {
        if (ImGui::SliderFloat("##", &physics_material->dynamic_friction, 0.0f, 1.0f)) {
            reapply_physics_material(m_context, physics_material);
        }
    });
    add_entry("Restitution", [this, physics_material]() {
        if (ImGui::SliderFloat("##", &physics_material->restitution, 0.0f, 1.0f)) {
            reapply_physics_material(m_context, physics_material);
        }
    });
    add_entry("Friction Combine", [this, physics_material]() {
        int current = static_cast<int>(physics_material->friction_combine);
        if (ImGui::Combo("##", &current, c_combine_mode_names, IM_ARRAYSIZE(c_combine_mode_names))) {
            physics_material->friction_combine = static_cast<erhe::physics::Combine_mode>(current);
            reapply_physics_material(m_context, physics_material);
        }
    });
    add_entry("Restitution Combine", [this, physics_material]() {
        int current = static_cast<int>(physics_material->restitution_combine);
        if (ImGui::Combo("##", &current, c_combine_mode_names, IM_ARRAYSIZE(c_combine_mode_names))) {
            physics_material->restitution_combine = static_cast<erhe::physics::Combine_mode>(current);
            reapply_physics_material(m_context, physics_material);
        }
    });
}

void Properties::collision_filter_properties(const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    ERHE_PROFILE_FUNCTION();

    class List_description
    {
    public:
        const char*               group_label;
        const char*               tooltip;
        std::vector<std::string>* strings;
    };
    const List_description lists[] = {
        { "Collision Systems",        "Systems this filter's body belongs to",                          &collision_filter->collision_systems        },
        { "Collide With",             "Non-empty = collide only with these systems (allowlist)",       &collision_filter->collide_with_systems     },
        { "Not Collide With",         "Used when Collide With is empty: never collide with these",     &collision_filter->not_collide_with_systems },
    };
    for (const List_description& list : lists) {
        push_group(list.group_label, ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        std::vector<std::string>* strings = list.strings;
        for (std::size_t i = 0, end = strings->size(); i < end; ++i) {
            add_entry(
                fmt::format("System {}", i),
                [this, collision_filter, strings, i]() {
                    if (i >= strings->size()) {
                        return;
                    }
                    if (ImGui::Button("-")) {
                        strings->erase(strings->begin() + static_cast<std::ptrdiff_t>(i));
                        reapply_collision_filter(m_context, collision_filter);
                        return;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##", &(*strings)[i]);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        reapply_collision_filter(m_context, collision_filter);
                    }
                },
                list.tooltip
            );
        }
        add_entry("Add", [strings]() {
            if (ImGui::Button("Add System", ImVec2{-FLT_MIN, 0.0f})) {
                strings->emplace_back();
            }
        });
        pop_group();
    }
}

void Properties::physics_joint_settings_properties(const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    ERHE_PROFILE_FUNCTION();

    // Changes take effect on a joint when its constraint is recreated; press
    // "Rebuild Joint" on the Node_joint(s) using these settings.
    push_group("Limits", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
    for (std::size_t i = 0, end = settings->limits.size(); i < end; ++i) {
        push_group(fmt::format("Limit {}", i), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        add_entry(
            "Linear Axes",
            [settings, i]() {
                if (i >= settings->limits.size()) {
                    return;
                }
                erhe::physics::Joint_limit& limit = settings->limits[i];
                ImGui::Checkbox("X##l", &limit.linear_axes[0]); ImGui::SameLine();
                ImGui::Checkbox("Y##l", &limit.linear_axes[1]); ImGui::SameLine();
                ImGui::Checkbox("Z##l", &limit.linear_axes[2]);
            },
            "Translation axes this limit applies to"
        );
        add_entry(
            "Angular Axes",
            [settings, i]() {
                if (i >= settings->limits.size()) {
                    return;
                }
                erhe::physics::Joint_limit& limit = settings->limits[i];
                ImGui::Checkbox("X##a", &limit.angular_axes[0]); ImGui::SameLine();
                ImGui::Checkbox("Y##a", &limit.angular_axes[1]); ImGui::SameLine();
                ImGui::Checkbox("Z##a", &limit.angular_axes[2]);
            },
            "Rotation axes this limit applies to"
        );
        add_entry("Min", [settings, i]() {
            if (i >= settings->limits.size()) {
                return;
            }
            optional_float_editor(settings->limits[i].min, 0.0f);
        }, "Absent = unbounded below");
        add_entry("Max", [settings, i]() {
            if (i >= settings->limits.size()) {
                return;
            }
            optional_float_editor(settings->limits[i].max, 0.0f);
        }, "Absent = unbounded above");
        add_entry("Stiffness", [settings, i]() {
            if (i >= settings->limits.size()) {
                return;
            }
            optional_float_editor(settings->limits[i].stiffness, 0.0f);
        }, "Soft limit spring stiffness; absent = hard limit");
        add_entry("Damping", [settings, i]() {
            if (i >= settings->limits.size()) {
                return;
            }
            ImGui::DragFloat("##", &settings->limits[i].damping, 0.01f, 0.0f, FLT_MAX);
        });
        add_entry("Remove", [settings, i]() {
            if (i >= settings->limits.size()) {
                return;
            }
            if (ImGui::Button("Remove Limit", ImVec2{-FLT_MIN, 0.0f})) {
                settings->limits.erase(settings->limits.begin() + static_cast<std::ptrdiff_t>(i));
            }
        });
        pop_group();
    }
    add_entry("Add", [settings]() {
        if (ImGui::Button("Add Limit", ImVec2{-FLT_MIN, 0.0f})) {
            settings->limits.emplace_back();
        }
    });
    pop_group();

    push_group("Drives", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
    for (std::size_t i = 0, end = settings->drives.size(); i < end; ++i) {
        push_group(fmt::format("Drive {}", i), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        add_entry("Type", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            int current = static_cast<int>(settings->drives[i].type);
            if (ImGui::Combo("##", &current, c_drive_type_names, IM_ARRAYSIZE(c_drive_type_names))) {
                settings->drives[i].type = static_cast<erhe::physics::Drive_type>(current);
            }
        });
        add_entry("Mode", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            int current = static_cast<int>(settings->drives[i].mode);
            if (ImGui::Combo("##", &current, c_drive_mode_names, IM_ARRAYSIZE(c_drive_mode_names))) {
                settings->drives[i].mode = static_cast<erhe::physics::Drive_mode>(current);
            }
        }, "Acceleration mode is approximated as force mode");
        add_entry("Axis", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            ImGui::SliderInt("##", &settings->drives[i].axis, 0, 2);
        });
        add_entry(
            "Max Force",
            [settings, i]() {
                if (i >= settings->drives.size()) {
                    return;
                }
                erhe::physics::Joint_drive& drive = settings->drives[i];
                bool limited = std::isfinite(drive.max_force);
                if (ImGui::Checkbox("##has", &limited)) {
                    drive.max_force = limited ? 0.0f : std::numeric_limits<float>::infinity();
                }
                if (std::isfinite(drive.max_force)) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragFloat("##value", &drive.max_force, 0.1f, 0.0f, FLT_MAX);
                }
            },
            "Unchecked = unlimited force"
        );
        add_entry("Position Target", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            ImGui::DragFloat("##", &settings->drives[i].position_target, 0.01f);
        });
        add_entry("Velocity Target", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            ImGui::DragFloat("##", &settings->drives[i].velocity_target, 0.01f);
        });
        add_entry("Stiffness", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            ImGui::DragFloat("##", &settings->drives[i].stiffness, 0.01f, 0.0f, FLT_MAX);
        }, "> 0 selects a position motor, 0 a velocity motor");
        add_entry("Damping", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            ImGui::DragFloat("##", &settings->drives[i].damping, 0.01f, 0.0f, FLT_MAX);
        });
        add_entry("Remove", [settings, i]() {
            if (i >= settings->drives.size()) {
                return;
            }
            if (ImGui::Button("Remove Drive", ImVec2{-FLT_MIN, 0.0f})) {
                settings->drives.erase(settings->drives.begin() + static_cast<std::ptrdiff_t>(i));
            }
        });
        pop_group();
    }
    add_entry("Add", [settings]() {
        if (ImGui::Button("Add Drive", ImVec2{-FLT_MIN, 0.0f})) {
            settings->drives.emplace_back();
        }
    });
    pop_group();
}

void Properties::item_flags(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_FUNCTION();

    push_group("Flags", ImGuiTreeNodeFlags_None, m_indent);

    using namespace erhe::utility;
    using Item_flags = erhe::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        add_entry(Item_flags::c_bit_labels[bit_position], [item, bit_position, flags, this]() {
            const uint64_t bit_mask = uint64_t{1} << bit_position;
            bool           value    = test_bit_set(flags, bit_mask);
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
        !erhe::is<Node_physics>     (item) &&
        !erhe::is<Frame_controller> (item) &&
        !erhe::is<Rendertarget_mesh>(item);
}

void Properties::item_properties(const std::shared_ptr<erhe::Item_base>& item_in)
{
    ERHE_PROFILE_FUNCTION();

    const auto& content_library_node = std::dynamic_pointer_cast<Content_library_node   >(item_in);
    const auto& item            = (content_library_node && content_library_node->item) ? content_library_node->item : item_in;

    const auto& node_physics    = std::dynamic_pointer_cast<Node_physics           >(item);
    const auto& node_joint      = std::dynamic_pointer_cast<Node_joint             >(item);
    const auto& rendertarget    = std::dynamic_pointer_cast<Rendertarget_mesh      >(item);
    const auto& camera          = std::dynamic_pointer_cast<erhe::scene::Camera    >(item);
    const auto& layout          = std::dynamic_pointer_cast<erhe::scene::Layout    >(item);
    const auto& layout_item     = std::dynamic_pointer_cast<erhe::scene::Layout_item>(item);
    const auto& light           = std::dynamic_pointer_cast<erhe::scene::Light     >(item);
    const auto& mesh            = std::dynamic_pointer_cast<erhe::scene::Mesh      >(item);
    const auto& node            = std::dynamic_pointer_cast<erhe::scene::Node      >(item);
    const auto& brush           = std::dynamic_pointer_cast<Brush                  >(item);
    const auto& brush_placement = std::dynamic_pointer_cast<Brush_placement        >(item);
    const auto& texture         = std::dynamic_pointer_cast<erhe::graphics::Texture>(item);
    const auto& physics_material  = std::dynamic_pointer_cast<erhe::physics::Physics_material      >(item);
    const auto& collision_filter  = std::dynamic_pointer_cast<erhe::physics::Collision_filter      >(item);
    const auto& physics_joint     = std::dynamic_pointer_cast<erhe::physics::Physics_joint_settings>(item);

    if (!item) {
        return;
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed;
    if (!node_physics && !rendertarget) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // TODO: Avoid formatting this label every frame. Store it somewhere; storage needs to
    //       be able to handle multiple items. Avoid storing the label in the item, as that
    //       would require adding a string member to all items, and most items don't need it.
    std::string group_label = fmt::format(
        "{} {}",
        item->get_type_name().data(),
        item->get_name()
    );
    push_group(group_label.c_str(), flags, m_indent);
    if (show_item_details(item.get())) {
        // TODO Same as group_label above - avoid heap allocation every frame
        std::string item_name_label = fmt::format("{} Name", item->get_type_name());
        add_entry(item_name_label.c_str(), [item]() {
            if (item->is_lock_edit()) {
                ImGui::TextUnformatted(item->get_name().c_str());
            } else {
                std::string name = item->get_name();
                const bool enter_pressed = ImGui::InputText("##", &name, ImGuiInputTextFlags_EnterReturnsTrue);
                if (enter_pressed || ImGui::IsItemDeactivatedAfterEdit()) { // TODO
                    if (name != item->get_name()) {
                        item->set_name(name);
                    }
                }
            }
        });                 

        add_entry("Locks", [this, item]() {
            ImFont* icon_font = m_imgui_renderer.material_design_font();
            if (icon_font == nullptr) {
                return;
            }
            const float font_size =
                m_imgui_renderer.get_imgui_settings().scale_factor *
                m_imgui_renderer.get_imgui_settings().material_design_font_size;

            bool lock_xfm = item->is_lock_viewport_transform();
            bool lock_edt = item->is_lock_edit();
            bool lock_sel = item->is_lock_viewport_selection();
            const char* xfm_icon = lock_xfm ? ICON_MDI_AXIS_ARROW_LOCK         : ICON_MDI_AXIS_ARROW;
            const char* edt_icon = lock_edt ? ICON_MDI_LOCK                     : ICON_MDI_LOCK_OPEN;
            const char* sel_icon = lock_sel ? ICON_MDI_LOCK_OUTLINE             : ICON_MDI_LOCK_OPEN_VARIANT_OUTLINE;
            const ImVec4 dim_color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);

            if (!lock_xfm) { ImGui::PushStyleColor(ImGuiCol_Text, dim_color); }
            ImGui::PushFont(icon_font, font_size);
            const bool xfm_clicked = ImGui::Button(xfm_icon);
            ImGui::PopFont();
            if (!lock_xfm) { ImGui::PopStyleColor(); }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Transform Lock: %s", lock_xfm ? "Locked" : "Unlocked");
            }
            if (xfm_clicked) {
                item->set_flag_bits(erhe::Item_flags::lock_viewport_transform, !lock_xfm);
            }

            ImGui::SameLine();
            if (!lock_edt) { ImGui::PushStyleColor(ImGuiCol_Text, dim_color); }
            ImGui::PushFont(icon_font, font_size);
            const bool edt_clicked = ImGui::Button(edt_icon);
            ImGui::PopFont();
            if (!lock_edt) { ImGui::PopStyleColor(); }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edit Lock: %s", lock_edt ? "Locked" : "Unlocked");
            }
            if (edt_clicked) {
                item->set_lock_edit(!lock_edt);
            }

            ImGui::SameLine();
            if (!lock_sel) { ImGui::PushStyleColor(ImGuiCol_Text, dim_color); }
            ImGui::PushFont(icon_font, font_size);
            const bool sel_clicked = ImGui::Button(sel_icon);
            ImGui::PopFont();
            if (!lock_sel) { ImGui::PopStyleColor(); }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Selection Lock: %s", lock_sel ? "Locked" : "Unlocked");
            }
            if (sel_clicked) {
                item->set_flag_bits(erhe::Item_flags::lock_viewport_selection, !lock_sel);
            }
        });

        if (m_context.developer_mode) {
            add_entry("Id", [item]() { ImGui::Text("%u", static_cast<unsigned int>(item->get_id())); });
            item_flags(item);
        }

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

    const bool edit_disabled = item->is_lock_edit();
    if (edit_disabled) {
        ImGui::BeginDisabled();
    }

    if (node_physics) {
        node_physics_properties(*node_physics);
    }

    if (node_joint) {
        node_joint_properties(*node_joint);
    }

    if (physics_material) {
        physics_material_properties(physics_material);
    }

    if (collision_filter) {
        collision_filter_properties(collision_filter);
    }

    if (physics_joint) {
        physics_joint_settings_properties(physics_joint);
    }

    if (camera) {
        camera_properties(*camera);
    }

    if (light) {
        light_properties(*light);
    }

    if (layout) {
        layout_properties(*layout);
    }

    if (layout_item) {
        layout_item_properties(*layout_item);
    }

    if (mesh) {
        mesh_properties(*mesh);
    }

    if (rendertarget) {
        rendertarget_properties(*rendertarget);
    }

    if (brush) {
        brush_properties(brush);
    }

    if (brush_placement) {
        brush_placement_properties(*brush_placement);
    }

    if (texture) {
        texture_properties(texture);
    }

    if (edit_disabled) {
        ImGui::EndDisabled();
    }

    if (node) {
        //push_group("Attachments", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        for (auto& attachment : node->get_attachments()) {
            item_properties(attachment);
        }
        //pop_group();

        // A child of a layout node without per-child layout parameters:
        // offer creating the Layout_item attachment (undoable).
        const std::shared_ptr<erhe::scene::Node> parent_node = node->get_parent_node();
        const bool parent_is_layout =
            parent_node &&
            static_cast<bool>(erhe::scene::get_attachment<erhe::scene::Layout>(parent_node.get()));
        const bool has_layout_item =
            static_cast<bool>(erhe::scene::get_attachment<erhe::scene::Layout_item>(node.get()));
        if (parent_is_layout && !has_layout_item) {
            add_entry("Layout Item", [this, node]() {
                if (ImGui::Button("Add Layout Item")) {
                    std::shared_ptr<erhe::scene::Layout_item> new_layout_item = std::make_shared<erhe::scene::Layout_item>("layout item");
                    new_layout_item->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
                    m_context.operation_stack->queue(
                        std::make_shared<Node_attach_operation>(new_layout_item, node)
                    );
                }
            });
        }
    }

    pop_group();
}

void Properties::end_material_inspect()
{
    if (m_inspected_material && (m_inspected_material->data != m_inspected_material_initial_state)) {
        log_operations->info("end_material_inspect - material changed");
        m_context.operation_stack->queue(
            std::make_shared<Material_change_operation>(
                m_inspected_material, m_inspected_material_initial_state, m_inspected_material->data
            )
        );
        m_inspected_material_initial_state = m_inspected_material->data;
    } else {
        log_operations->info("end_material_inspect - material not changed");
    }
    m_material_state = Editor_state::clean;
}

void Properties::material_properties()
{
    ERHE_PROFILE_FUNCTION();

    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    const std::shared_ptr<erhe::primitive::Material> selected_material_shared = get<erhe::primitive::Material>(selected_items);
    if (m_inspected_material != selected_material_shared) {
        log_operations->info("m_inspected_material != selected_material_shared");
        if (m_inspected_material) {
            end_material_inspect();
        }
        m_inspected_material = selected_material_shared;
        if (m_inspected_material) {
            log_operations->info("have m_inspected_material - record initial state");
            m_inspected_material_initial_state = m_inspected_material->data;
            m_material_state = Editor_state::clean;
        }
    }

    if (!selected_material_shared) {
        return;
    }

    use_state(&m_material_state);

    erhe::primitive::Material* selected_material = selected_material_shared.get();

    const std::string                           name_before = selected_material->get_name();
    const erhe::primitive::Material_data        before      = selected_material->data;
    erhe::primitive::Material_data&             data        = selected_material->data;
    erhe::primitive::Material_texture_samplers& samplers    = data.texture_samplers;
    push_group("Material", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, m_indent);

    add_entry("state", [this]() {ImGui::TextUnformatted(
        (m_material_state == Editor_state::clean          ) ? "clean" :
        (m_material_state == Editor_state::dirty_editing  ) ? "dirty_editing" :
        (m_material_state == Editor_state::dirty_completed) ? "dirty_completed" : "?"); });

    add_entry("Name", [=]() {
        std::string name = selected_material->get_name();
        if (ImGui::InputText("##", &name)) {
            selected_material->set_name(name);
        }
    });

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float area_size_0    = std::min(available_size.x, available_size.y);
    const int   area_size      = std::max(1, static_cast<int>(area_size_0));
    m_context.material_preview->resize(area_size, area_size);
    m_context.material_preview->update_rendertarget(*m_context.graphics_device);
    m_context.material_preview->render_preview(selected_material_shared);
    m_context.material_preview->show_preview();

    auto* node = m_context.brdf_slice->get_node();
    if (node != nullptr) {
        node->set_material(selected_material_shared);
        if (ImGui::TreeNodeEx("BRDF Slice", ImGuiTreeNodeFlags_None)) {
            m_context.brdf_slice->show_brdf_slice(area_size);
            ImGui::TreePop();
        }

        //push_group("BRDF Slice", ImGuiTreeNodeFlags_None);
        //add_entry("BRDF", [this, area_size]() {
        //    m_context.brdf_slice->show_brdf_slice(area_size);
        //});
        //pop_group();
    }

    add_entry("BxDF Model", [&]() {
        static const char* const c_bxdf_model_names[] = {
            "Unlit",
            "Isotropic BRDF",
            "Anisotropic BRDF",
            "Anisotropic Slope",
            "Anisotropic Engine-Ready"
        };
        int current = static_cast<int>(data.bxdf_model);
        if (ImGui::Combo("##", &current, c_bxdf_model_names, IM_ARRAYSIZE(c_bxdf_model_names))) {
            data.bxdf_model = static_cast<erhe::primitive::Bxdf_model>(current);
        }
    });
    add_entry("Blending Mode", [&]() {
        // Order MUST match erhe::primitive::Material_blending_mode.
        int current = static_cast<int>(data.blending_mode);
        if (ImGui::Combo("##", &current, erhe::primitive::c_material_blending_mode_names, IM_ARRAYSIZE(erhe::primitive::c_material_blending_mode_names))) {
            data.blending_mode = static_cast<erhe::primitive::Material_blending_mode>(current);
        }
    });
    if (data.blending_mode == erhe::primitive::Material_blending_mode::alpha_test) {
        add_entry("Alpha Cutoff", [&](){
            ImGui::SliderFloat("##", &data.alpha_cutoff, 0.0f, 1.0f);
        });
    }
    add_entry("Circular Brushed Metal", [&](){
        ImGui::Checkbox("##", &data.use_circular_brushed_metal);
    });
    if (data.use_circular_brushed_metal) {
        add_entry("Brushed Metal TexCoord", [&](){
            int tex_coord = static_cast<int>(data.circular_brushed_metal_tex_coord);
            if (ImGui::SliderInt("##", &tex_coord, 0, 1)) {
                data.circular_brushed_metal_tex_coord = static_cast<uint32_t>(tex_coord);
            }
        });
    }
    add_entry("Aniso Control", [&](){
        ImGui::Checkbox("##", &data.use_aniso_control);
    });
    add_entry("Metallic",    [&](){ ImGui::SliderFloat("##", &data.metallic,     0.0f,  1.0f); });
    add_entry("Reflectance", [&](){ ImGui::SliderFloat("##", &data.reflectance,  0.35f, 1.0f); });
    add_entry("Roughness X", [&](){ ImGui::SliderFloat("##", &data.roughness.x,  0.1f,  0.8f); });
    add_entry("Roughness Y", [&](){ ImGui::SliderFloat("##", &data.roughness.y,  0.1f,  0.8f); });
    add_entry("Base Color",  [&](){ ImGui::ColorEdit4 ("##", &data.base_color.x, ImGuiColorEditFlags_Float); });
    add_entry("Emissive",    [&](){ ImGui::ColorEdit4 ("##", &data.emissive.x,   ImGuiColorEditFlags_Float); });
    add_entry("Opacity",     [&](){ ImGui::SliderFloat("##", &data.opacity,      0.0f,  1.0f); });

    Scene_root* scene_root = m_context.scene_commands->get_scene_root(selected_material);
    if (scene_root != nullptr) {
        const std::shared_ptr<Content_library>& content_library = scene_root->get_content_library();
        if (content_library) {
            const std::shared_ptr<Content_library_node>& textures_ = content_library->textures;
            if (textures_) {
                Content_library_node& textures = *textures_.get();
                auto add_tex_coord_entry = [this](const char* label, erhe::primitive::Material_texture_sampler* sampler) {
                    add_entry(label, [sampler]() {
                        int tex_coord = static_cast<int>(sampler->tex_coord);
                        if (ImGui::SliderInt("##", &tex_coord, 0, 1)) {
                            sampler->tex_coord = static_cast<uint32_t>(tex_coord);
                        }
                    });
                };
                add_entry("Base Color Texture", [&, this]() { textures.combo(m_context, "##", samplers.base_color.texture, true);});
                if (samplers.base_color.texture) {
                    add_entry("Base Color Offset",   [&](){ ImGui::SliderFloat2("##", &samplers.base_color.offset.x, -10.0f, 10.0f); });
                    add_entry("Base Color Scale",    [&](){ ImGui::SliderFloat2("##", &samplers.base_color.scale.x,  -10.0f, 10.0f); });
                    add_entry("Base Color Rotation", [&](){ ImGui::SliderFloat ("##", &samplers.base_color.rotation, -10.0f, 10.0f); });
                    add_tex_coord_entry("Base Color TexCoord", &samplers.base_color);
                }
                add_entry("Metallic Roughness Texture",[&]() { textures.combo(m_context, "##", samplers.metallic_roughness.texture, true); });
                if (samplers.metallic_roughness.texture) {
                    add_tex_coord_entry("Metallic Roughness TexCoord", &samplers.metallic_roughness);
                }
                add_entry("Normal Texture",            [&]() { textures.combo(m_context, "##", samplers.normal.texture, true); } );
                if (samplers.normal.texture) {
                    add_entry("Normal Map Scale", [&](){ ImGui::SliderFloat("##", &data.normal_texture_scale, 0.0f, 1.0f); });
                    add_tex_coord_entry("Normal TexCoord", &samplers.normal);
                }
                add_entry("Occlusion Texture", [&, this]() { textures.combo(m_context, "##", samplers.occlusion.texture, true); });
                if (samplers.occlusion.texture) {
                    add_tex_coord_entry("Occlusion TexCoord", &samplers.occlusion);
                }
                add_entry("Emissive Texture",  [&, this]() { textures.combo(m_context, "##", samplers.emissive.texture, true); });
                if (samplers.emissive.texture) {
                    add_tex_coord_entry("Emissive TexCoord", &samplers.emissive);
                }
            }
        }
    }

    pop_group();

    show_entries();

    if (m_material_state == Editor_state::dirty_completed) {
        log_operations->info("m_material_state == Editor_state::dirty_completed");
        end_material_inspect();
    }
}

void Properties::imgui()
{
    ERHE_PROFILE_FUNCTION();

    reset();

    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    int id = 0;
    for (const auto& item : selected_items) {
        ImGui::PushID(id++);
        ERHE_DEFER( ImGui::PopID(); );
        ERHE_VERIFY(item);
        item_properties(item);
    }

    const auto selected_animation = get<erhe::scene::Animation>(selected_items);
    if (selected_animation) {
        animation_properties(*selected_animation.get());
    }

    const auto selected_skin = get<erhe::scene::Skin>(selected_items);
    if (selected_skin) {
        skin_properties(*selected_skin.get());
    }

    show_entries();

    material_properties();

}

}
