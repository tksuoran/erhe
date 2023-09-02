
#include "tools/tool.hpp"

#include "editor_context.hpp"
#include "editor_settings.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/brushes/brush.hpp"
#include "tools/brushes/create/create_preview_settings.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_profile/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

using Transform = erhe::scene::Transform;
using Trs_transform = erhe::scene::Trs_transform;

Create::Create(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Tools&                       tools
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Create", "create"}
    , Tool{editor_context}
{
    set_base_priority  (c_priority);
    set_description    ("Create");
    set_flags          (Tool_flags::background);
    tools.register_tool(this);
}


auto Create::get_button_size() -> ImVec2
{
    const float ui_scale = m_context.editor_settings->get_ui_scale();
    return ImVec2{110.0f * ui_scale, 0.0f};
}

void Create::brush_create_button(const char* label, Create_shape* create_shape)
{
    if (ImGui::Button(label, get_button_size())) {
        if (m_create_shape == create_shape) {
            m_create_shape = nullptr;
        } else {
            m_create_shape = create_shape;
            m_brush_name = label;
        }
    }
}

auto Create::find_parent() -> std::shared_ptr<erhe::scene::Node>
{
    const auto selected_node   = m_context.selection->get<erhe::scene::Node>();
    const auto viewport_window = m_context.viewport_windows->last_window();

    Scene_view* scene_view = get_hover_scene_view();
    erhe::Item_host* item_host = selected_node
        ? selected_node->get_item_host()
        : viewport_window
            ? viewport_window->get_scene_root().get()
            : (scene_view != nullptr)
                ? scene_view->get_scene_root().get()
                : nullptr;
    if (item_host == nullptr) {
        return {};
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);

    const auto parent = selected_node
        ? selected_node
        : scene_root->get_hosted_scene()->get_root_node();

    return parent;
}

void Create::imgui()
{
    ERHE_PROFILE_FUNCTION();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto parent = find_parent();
    if (!parent) {
        return;
    }

    Scene_root* scene_root = static_cast<Scene_root*>(parent->get_item_host());
    auto content_library = scene_root->content_library();

    const glm::mat4 world_from_node = parent->world_from_node();

    ImGui::Text("Nodes");
    const auto button_size = get_button_size();
    if (ImGui::Button("Empty Node", button_size)) {
        m_context.scene_commands->create_new_empty_node(parent.get());
    }
    if (ImGui::Button("Camera", button_size)) {
        m_context.scene_commands->create_new_camera(parent.get());
    }
    if (ImGui::Button("Light", button_size)) {
        m_context.scene_commands->create_new_light(parent.get());
    }
    if (ImGui::Button("Rendertarget", button_size)) {
        m_context.scene_commands->create_new_rendertarget(parent.get());
    }

    ImGui::Separator();
    ImGui::Text("Meshes");
    ImGui::Checkbox("Preview Ideal Shape", &m_preview_ideal_shape);
    ImGui::Checkbox("Preview Shape",       &m_preview_shape);
    brush_create_button("UV Sphere", &m_create_uv_sphere);
    brush_create_button("Cone",      &m_create_cone);
    brush_create_button("Torus",     &m_create_torus);
    brush_create_button("Box",       &m_create_box);

    auto material = m_context.selection->get<erhe::primitive::Material>();

    if ((m_create_shape != nullptr) && material) {
        m_create_shape->imgui();
        erhe::imgui::make_combo(
            "Normal Style",
            m_normal_style,
            erhe::primitive::c_normal_style_strings,
            IM_ARRAYSIZE(erhe::primitive::c_normal_style_strings)
        );
        const bool create_instance = ImGui::Button("Create Instance", button_size);
        ImGui::InputText("Brush Name", &m_brush_name);
        const bool create_brush    = ImGui::Button("Create Brush", button_size);
        if (create_instance || create_brush) {
            Brush_data brush_create_info{
                .context         = m_context,
                .editor_settings = *m_context.editor_settings,
                .name            = m_brush_name,
                .build_info      = erhe::primitive::Build_info{
                    .primitive_types = { .fill_triangles = true, .edge_lines = true, .corner_points = true, .centroid_points = true },
                    .buffer_info     = m_context.mesh_memory->buffer_info
                },
                .normal_style    = m_normal_style,
                .density         = m_density,
            };

            m_brush = m_create_shape->create(brush_create_info);
            if (m_brush && create_instance) {
                using Item_flags = erhe::Item_flags;
                const uint64_t node_flags =
                    Item_flags::visible     |
                    Item_flags::content     |
                    Item_flags::show_in_ui;
                const uint64_t mesh_flags =
                    Item_flags::visible     |
                    Item_flags::content     |
                    Item_flags::opaque      |
                    Item_flags::shadow_cast |
                    Item_flags::id          |
                    Item_flags::show_in_ui;

                const Instance_create_info brush_instance_create_info{
                    .node_flags      = node_flags,
                    .mesh_flags      = mesh_flags,
                    .scene_root      = scene_root,
                    .world_from_node = world_from_node,
                    .material        = material,
                    .scale           = 1.0
                };
                const auto instance_node = m_brush->make_instance(brush_instance_create_info);

                auto op = std::make_shared<Item_insert_remove_operation>(
                    Item_insert_remove_operation::Parameters{
                        .context = m_context,
                        .item    = instance_node,
                        .parent  = parent,
                        .mode    = Scene_item_operation::Mode::insert
                    }
                );
                m_context.operation_stack->queue(op);
            }
            m_create_shape = nullptr;
        }
        if (m_brush && create_brush) {
            content_library->brushes->add(m_brush);
            m_brush.reset();
        }
    }

    {
        const auto& selection = m_context.selection->get_selection();
        if (!selection.empty()) {
            std::shared_ptr<erhe::geometry::Geometry> source_geometry;
            for (const auto& node : selection) {
                const auto& mesh = as<erhe::scene::Mesh>(node);
                if (mesh) {
                    for (const auto& primitive : mesh->mesh_data.primitives) {
                        const auto& geometry_primitive = primitive.geometry_primitive;
                        if (geometry_primitive) {
                            if (geometry_primitive->source_geometry) {
                                source_geometry = geometry_primitive->source_geometry;
                                break;
                            }
                        }
                    }
                    if (source_geometry) {
                        break;
                    }
                }
            }
            if (source_geometry) {
                if (m_create_shape == nullptr) {
                    erhe::imgui::make_combo(
                        "Normal Style",
                        m_normal_style,
                        erhe::primitive::c_normal_style_strings,
                        IM_ARRAYSIZE(erhe::primitive::c_normal_style_strings)
                    );
                    ImGui::InputText("Brush Name", &m_brush_name);
                }

                ImGui::Text("Selected Primitive: %s", source_geometry->name.c_str());
                if (ImGui::Button("Selected Mesh to Brush")) {
                    Brush_data brush_create_info{
                        .context         = m_context,
                        .editor_settings = *m_context.editor_settings,
                        .name            = m_brush_name,
                        .build_info      = erhe::primitive::Build_info{
                            .primitive_types = { .fill_triangles = true, .edge_lines = true, .corner_points = true, .centroid_points = true },
                            .buffer_info     = m_context.mesh_memory->buffer_info
                        },
                        .normal_style    = m_normal_style,
                        .geometry        = source_geometry,
                        .density         = m_density
                    };
                    //// source_geometry->build_edges();
                    //// source_geometry->compute_polygon_normals();
                    //// source_geometry->compute_tangents();
                    //// source_geometry->compute_polygon_centroids();
                    //// source_geometry->compute_point_normals(erhe::geometry::c_point_normals_smooth);
                    content_library->brushes->make<Brush>(brush_create_info);
                }
            }
        }
    }

#endif
}

void Create::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    const auto parent = find_parent();
    if (!parent) {
        return;
    }

    Scene_root* scene_root = static_cast<Scene_root*>(parent->get_item_host());
    if (context.get_scene() != scene_root->get_hosted_scene()) {
        return;
    }

    const Trs_transform transform = parent->world_from_node_transform();
    if (m_create_shape != nullptr) {
        if (m_preview_ideal_shape) {
            const Create_preview_settings preview_settings{
                .render_context = context,
                .transform      = transform,
                .major_color    = glm::vec4{1.0f, 0.5f, 0.0f, 1.0f},
                .minor_color    = glm::vec4{1.0f, 0.5f, 0.0f, 0.5f},
                .ideal_shape    = true
            };
            m_create_shape->render_preview(preview_settings);
        }
        if (m_preview_shape) {
            const Create_preview_settings preview_settings{
                .render_context = context,
                .transform      = transform,
                .major_color    = glm::vec4{0.5f, 1.0f, 0.0f, 1.0f},
                .minor_color    = glm::vec4{0.5f, 1.0f, 0.0f, 0.5f},
                .ideal_shape    = false
            };
            m_create_shape->render_preview(preview_settings);
        }
    }
}

} // namespace editor
