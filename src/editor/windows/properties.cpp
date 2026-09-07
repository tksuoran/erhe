#include "windows/properties.hpp"

#include "animation/animation_window.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "content_library/brdf_slice.hpp"
#include "content_library/content_library.hpp"
#include "texture_graph/graph_texture.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"

#include "app_scenes.hpp"
#include "preview/material_preview.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/frame_controller.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/physics_edits.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "windows/config_ui.hpp"
#include "windows/item_reference.hpp"

// Per-scene overrides (issues #239 / #240): the editor-global defaults type plus
// the reflection helpers (get_struct_info / get_fields) for each overridable
// config group, needed to instantiate add_config_section in scene_properties.
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/developer_config.hpp"
#include "config/generated/camera_controls_config_serialization.hpp"
#include "config/generated/grid_config_serialization.hpp"
#include "config/generated/physics_config_serialization.hpp"
#include "config/generated/shadow_frustum_fit_config_serialization.hpp"
#include "config/generated/sky_config_serialization.hpp"

#include "erhe_defer/defer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
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

Properties::Properties(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus,
    std::string_view             title,
    std::string_view             ini_label
)
    : Imgui_window{imgui_renderer, imgui_windows, title, ini_label}
    , m_context   {app_context}
    , m_dependency_rows{app_context}
{
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

auto Properties::get_target() const -> std::shared_ptr<erhe::Item_base>
{
    return m_target.lock();
}

auto Properties::get_target_items() const -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_target_items;
}

auto Properties::get_inspected_material() const -> const std::shared_ptr<erhe::primitive::Material>&
{
    return m_inspected_material;
}

void Properties::on_items_removed(const Removed_items& removed)
{
    const std::shared_ptr<erhe::Item_base> target = m_target.lock();
    if (target && removed.lookup.contains(target.get())) {
        m_target.reset();
        m_target_items.clear();
    } else {
        // The target itself survives, but individual inspected items may not.
        std::erase_if(
            m_target_items,
            [&removed](const std::shared_ptr<erhe::Item_base>& item) {
                return item && removed.lookup.contains(item.get());
            }
        );
    }
    if (m_inspected_material && removed.lookup.contains(m_inspected_material.get())) {
        m_inspected_material.reset();
    }
}

void Properties::on_close_scene(erhe::Item_host* const closing_host)
{
    // The target can be scene content (host comparison) or a managed asset
    // the manager's defining-container lookup covers both through is_hosted_or_defined_by).
    Asset_manager* const asset_manager = m_context.asset_manager;
    const std::shared_ptr<erhe::Item_base> target = m_target.lock();
    if (target && (asset_manager != nullptr) && asset_manager->is_hosted_or_defined_by(*target, closing_host)) {
        m_target.reset();
        m_target_items.clear();
    }
    if (m_inspected_material && (asset_manager != nullptr) && asset_manager->is_hosted_or_defined_by(*m_inspected_material, closing_host)) {
        m_inspected_material.reset();
    }
}

void Properties::animation_properties(const std::shared_ptr<erhe::scene::Animation>& animation)
{
    ERHE_PROFILE_FUNCTION();

    // The time range and the sampler / channel counts are computed
    // properties drawn by the generic rows (doc/property-system.md section
    // 4.16). Playback and curve editing live in the Animation window
    // (issue #243).
    add_entry("Edit", [this, animation]() {
        if (ImGui::Button("Open in Animation Window")) {
            m_context.animation_window->set_animation(animation);
            m_context.animation_window->show_window();
        }
    });
}

void Properties::scene_properties(erhe::scene::Scene& scene)
{
    ERHE_PROFILE_FUNCTION();

    Scene_root* scene_root = static_cast<Scene_root*>(scene.get_item_host());
    if (scene_root == nullptr) {
        return;
    }

    // Ambient light color is a scene property now (issues #237 / #240). Direct
    // edit, matching the other color properties in this window.
    add_entry("Ambient Light", [&scene]() {
        ImGui::ColorEdit3("##", &scene.ambient_light.x, ImGuiColorEditFlags_Float);
    }, "Scene-wide ambient light color.");

    // Per-scene setting overrides (issues #239 / #240). Each group can override
    // the matching editor-global setting; an unchecked override falls back to
    // the editor default, a checked one edits the scene's own copy (saved with
    // the scene). Moved here from the Settings window so it is keyed on the
    // selected scene.
    if (m_context.editor_settings == nullptr) {
        return;
    }
    Editor_settings_config& settings       = *m_context.editor_settings;
    Scene_settings&         scene_settings = scene_root->get_scene_settings();
    const bool              show_developer  = (m_context.developer_config != nullptr) && m_context.developer_config->enable;

    variant_properties(*scene_root);

    push_group("Scene Overrides", ImGuiTreeNodeFlags_Framed);

    // Whole-config-group override: an "Override" checkbox that engages the
    // scene's optional (seeded from the current editor value) or clears it,
    // followed by the group's editable fields when engaged.
    auto override_struct = [this, show_developer](auto& optional_field, const auto& editor_value, const char* name) {
        add_entry(std::string{name}, [&optional_field, &editor_value]() {
            bool overridden = optional_field.has_value();
            if (ImGui::Checkbox("##", &overridden)) {
                if (overridden) {
                    optional_field = editor_value;
                } else {
                    optional_field.reset();
                }
            }
        }, "Override this setting for this scene. Unchecked uses the editor-global default.");
        if (optional_field.has_value()) {
            const std::string section_label = std::string{name} + " (scene override)";
            add_config_section(*this, show_developer, optional_field.value(), section_label.c_str());
        }
    };

    override_struct(scene_settings.sky,                settings.sky,                "Sky");
    override_struct(scene_settings.grid,               settings.grid,               "Grid");
    override_struct(scene_settings.physics,            settings.physics,            "Physics");
    override_struct(scene_settings.shadow_frustum_fit, settings.shadow_frustum_fit, "Shadow Frustum Fit");
    override_struct(scene_settings.camera_controls,    settings.camera_controls,    "Camera Controls");

    // Clear Color and Post Processing per-scene overrides are intentionally not
    // surfaced here for now (the Scene_settings fields remain and still
    // serialize). Re-add add_entry rows for scene_settings.clear_color /
    // .post_processing when their per-scene effect is wired up.

    pop_group();
}

void Properties::variant_properties(Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();

    // One combo per variant set the scene carries
    // (doc/usd-compatibility-plan.md X4). Change-driven: the combo is drawn
    // from the table, and only a change queues the switch.
    const std::vector<Variant_set>& sets = scene_root.get_variant_table().get_sets();
    if (sets.empty()) {
        return;
    }
    push_group("Variants", ImGuiTreeNodeFlags_Framed);
    for (const Variant_set& set : sets) {
        const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
        if (!prim) {
            continue; // the carrying prim is gone; the table drops the set on the removal message
        }
        const std::string prim_path = set.get_prim_path();
        const std::string set_name  = set.set_name;
        // A set the scene's root prim carries has the empty path (a glTF
        // asset's one variant list, X4): label it by the prim's name.
        std::string label = (prim_path.empty() ? prim->get_name() : prim_path) + " : " + set_name;
        add_entry(
            std::move(label),
            [this, &scene_root, &set, prim_path, set_name]() {
                if (!ImGui::BeginCombo("##", set.selected.c_str())) {
                    return;
                }
                for (const Variant& variant : set.variants) {
                    const bool is_selected = (variant.name == set.selected);
                    if (ImGui::Selectable(variant.name.c_str(), is_selected) && !is_selected) {
                        const std::string error = scene_root.select_variant(
                            m_context, prim_path, set_name, variant.name, Scene_root::Variant_switch_mode::undoable
                        );
                        if (!error.empty()) {
                            log_scene->warn("select variant: {}", error);
                        }
                    }
                }
                ImGui::EndCombo();
            },
            "Which variant of this variant set the scene has selected."
        );
    }
    pop_group();
}

void Properties::light_properties(erhe::scene::Light& light)
{
    ERHE_PROFILE_FUNCTION();

    // The authored light state (type, cast shadow, spot angles, range,
    // intensity, color, temperature) and the derived rows (flux, the
    // blackbody swatch: computed properties, D26) are drawn by the generic
    // property rows (Dependency_property_rows); every write re-resolves the
    // scene light set through the Light property callback
    // (doc/property-system.md D19). Only the diagnostic remains here.
    const erhe::scene::Light::Type type = light.get_light_type();
    if ((type == erhe::scene::Light::Type::point) && (light.get_range() <= 0.0f)) {
        add_entry("Warning", [](){
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 160, 32, 255));
            ImGui::TextWrapped("Point light range is 0: it reaches nowhere, so it emits no light and casts no shadow. Set a positive range.");
            ImGui::PopStyleColor();
        });
    }

    // Ambient light color is a scene property now (issues #237 / #240); it is
    // shown in Scene properties (Properties::scene_properties), not per light.
}

void Properties::layout_properties(erhe::scene::Layout& layout)
{
    ERHE_PROFILE_FUNCTION();

    // The authored layout parameters (type, volume, axes, gap, grid track
    // count) are drawn by the generic property rows (Dependency_property_rows,
    // doc/property-system.md section 4.13). Only the per-track extent lists
    // remain here.
    if (layout.get_layout_type() == erhe::scene::Layout_type::grid) {
        static const char* const c_grid_size_labels[] = { "Sizes X", "Sizes Y", "Sizes Z" };
        for (int axis = 0; axis < 3; ++axis) {
            add_entry(c_grid_size_labels[axis], [&layout, axis]() {
                std::vector<float>& extents = layout.get_grid_track_extent(axis);
                const int track_count = layout.get_grid_track_count()[axis];
                const int count = (track_count > 1) ? track_count : 1;
                bool custom = !extents.empty();
                if (ImGui::Checkbox("Custom", &custom)) {
                    if (custom) {
                        const erhe::math::Aabb& volume = layout.get_volume();
                        const float total = volume.max[axis] - volume.min[axis];
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

    auto* scene_root = static_cast<Scene_root*>(mesh.get_item_host());
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

    if (m_context.developer_mode) {
        push_group("Primitives", ImGuiTreeNodeFlags_DefaultOpen, m_indent);
    }
    const std::shared_ptr<erhe::Item_base> mesh_shared = mesh.shared_from_this();
    int primitive_index = 0;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        while (m_primitive_labels.size() <= primitive_index) {
            m_primitive_labels.push_back(fmt::format("Primitive {}", m_primitive_labels.size()));
        }
        push_group(m_primitive_labels.at(primitive_index).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        // The primitive's registered properties (its material): generic rows
        // on the property sub-object (doc/property-system.md D29), undo
        // through Property_set_operation on (mesh, primitive index).
        m_dependency_rows.add_sub_object_rows(*this, mesh_shared, static_cast<std::size_t>(primitive_index));
        if (m_context.developer_mode) {
            if (mesh_primitive.material) {
                // The slot in this mesh's scene root's FORWARD set - the one
                // the bucket path binds. A mesh in a root that also has a draw
                // list holds a different slot in that root's draw-list set;
                // get_draw_lists over MCP reports that one.
                const std::optional<uint32_t> slot =
                    scene_root->get_material_set().get_slot(mesh_primitive.material.get());
                add_entry(
                    "Material Slot (forward set)",
                    [slot](){
                        if (slot.has_value()) {
                            ImGui::Text("%u", slot.value());
                        } else {
                            ImGui::TextUnformatted("-");
                        }
                    }
                );
            }
        }

        erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
        if (primitive.render_shape) {
            shape_properties("Render shape", primitive.render_shape.get());
            buffer_mesh_properties("Renderable Buffer Mesh", &primitive.render_shape->get_renderable_mesh());
        }
        // Inspecting a primitive should show what is actually resident, so list
        // the optimized build separately whenever one is live.
        if (primitive.optimized_render_shape) {
            shape_properties("Optimized render shape", primitive.optimized_render_shape.get());
            buffer_mesh_properties("Optimized Buffer Mesh", &primitive.optimized_render_shape->get_renderable_mesh());
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

void Properties::brush_placement_properties(Brush_placement& brush_placement)
{
    ERHE_PROFILE_FUNCTION();

    // The brush, facet and corner are generic rows (doc/property-system.md
    // 4.11); the polygon counts of the brush follow as diagnostics.
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

    // Static bodies have no motion properties (Jolt).
    if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_static) {
        add_entry("Local Inertia", [rigid_body](){
            const glm::mat4 local_inertia = rigid_body->get_local_inertia();
            ImGui::Text("%.3f, %.3f, %.3f", local_inertia[0][0], local_inertia[1][1], local_inertia[2][2]);
        });
    }
    // The authored rigid body state (motion mode, trigger, mass, friction,
    // restitution, damping, gravity factor, wind receptivity, initial
    // velocities, center of mass, physics material, collision filter) is
    // generic rows (doc/property-system.md 4.10), drawn by
    // dependency_properties() after the item rows.
}


void Properties::node_joint_properties(Node_joint& node_joint)
{
    ERHE_PROFILE_FUNCTION();

    // The connected node, the joint settings and the collision flag are
    // generic property rows (doc/property-system.md section 4.17); the
    // actions and the diagnostic remain here.
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

constexpr const char* c_drive_type_names  [] = { "Linear", "Angular" };
constexpr const char* c_drive_mode_names  [] = { "Force", "Acceleration" };

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

// Developer diagnostics (R3 of doc/properties-window-single-path.md): the
// whole flag word as text. The authored bits are property rows
// (Item_base::lock_edit_property and the other flag bridges); the rest are
// transient presentation state and not editable here.
void Properties::item_flags(const std::shared_ptr<erhe::Item_base>& item)
{
    ERHE_PROFILE_FUNCTION();

    add_entry("Flags", [item]() {
        ImGui::TextWrapped("%s", erhe::Item_flags::to_string(item->get_flag_bits()).c_str());
    });
}

[[nodiscard]] auto show_item_details(const erhe::Item_base* const item)
{
    return
        !erhe::is<Node_physics>     (item) &&
        !erhe::is<Frame_controller> (item) &&
        !erhe::is<Rendertarget_mesh>(item);
}

// The per-item part of the window (R3 / R5 of
// doc/properties-window-single-path.md): the read-only diagnostics of the
// item's class, the actions, and the list editors that have no property
// form (the scene's settings block, a layout's track extents, a collision
// filter's system lists, joint limits and drives), all drawn per item and
// disabled while the item is sealed.
void Properties::item_diagnostics(const std::shared_ptr<erhe::Item_base>& item)
{
    const auto& node_physics     = std::dynamic_pointer_cast<Node_physics           >(item);
    const auto& node_joint       = std::dynamic_pointer_cast<Node_joint             >(item);
    const auto& scene            = std::dynamic_pointer_cast<erhe::scene::Scene     >(item);
    const auto& layout           = std::dynamic_pointer_cast<erhe::scene::Layout    >(item);
    const auto& light            = std::dynamic_pointer_cast<erhe::scene::Light     >(item);
    const auto& mesh             = std::dynamic_pointer_cast<erhe::scene::Mesh      >(item);
    const auto& brush_placement  = std::dynamic_pointer_cast<Brush_placement        >(item);
    const auto& texture          = std::dynamic_pointer_cast<erhe::graphics::Texture>(item);
    const auto& collision_filter = std::dynamic_pointer_cast<erhe::physics::Collision_filter      >(item);
    const auto& physics_joint    = std::dynamic_pointer_cast<erhe::physics::Physics_joint_settings>(item);

    const bool edit_disabled = item->is_lock_edit();
    if (edit_disabled) {
        ImGui::BeginDisabled();
    }
    if (node_physics)     { node_physics_properties(*node_physics); }
    if (node_joint)       { node_joint_properties(*node_joint); }
    if (collision_filter) { collision_filter_properties(collision_filter); }
    if (physics_joint)    { physics_joint_settings_properties(physics_joint); }
    if (scene)            { scene_properties(*scene); }
    if (light)            { light_properties(*light); }
    if (layout)           { layout_properties(*layout); }
    if (mesh)             { mesh_properties(*mesh); }
    if (brush_placement)  { brush_placement_properties(*brush_placement); }
    if (texture)          { texture_properties(texture); }
    if (edit_disabled) {
        ImGui::EndDisabled();
    }
}

void Properties::item_properties(const std::shared_ptr<erhe::Item_base>& item_in)
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<erhe::Item_base>& item = item_in;
    if (!item) {
        return;
    }
    const auto& node = std::dynamic_pointer_cast<erhe::scene::Node>(item);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed;
    if (!erhe::is<Node_physics>(item.get()) && !erhe::is<Rendertarget_mesh>(item.get())) {
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
    if (show_item_details(item.get()) && m_context.developer_mode) {
        add_entry("Id", [item]() { ImGui::Text("%u", static_cast<unsigned int>(item->get_id())); });
        item_flags(item);
    }

    item_diagnostics(item);

    if (node) {
        for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
            item_properties(attachment);
            // Undoable remove (pure detach) for this attachment. Queuing to the
            // operation stack runs on the next frame, so node->get_attachments()
            // is not mutated during this iteration.
            add_entry("Remove", [this, attachment]() {
                std::string button_label = fmt::format("X##remove_attachment_{}", attachment->get_id());
                if (ImGui::Button(button_label.c_str())) {
                    m_context.scene_commands->remove_attachment(attachment);
                }
            }, "Remove this attachment (undoable)");
        }

    }

    // The registered rows of the item (and, for a node, of each attachment
    // above, drawn by the recursive call).
    dependency_properties(item);

    pop_group();
}

void Properties::dependency_properties(const std::shared_ptr<erhe::Item_base>& item)
{
    m_dependency_rows.add_rows(*this, std::vector<std::shared_ptr<erhe::Item_base>>{item});
}

// The material preview and the BRDF slice for a selected material; the
// material rows themselves are the registered rows of its section.
void Properties::material_properties(const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<erhe::primitive::Material> selected_material = get<erhe::primitive::Material>(items);
    m_inspected_material = selected_material;
    if (!selected_material) {
        return;
    }

    const auto  available_size = ImGui::GetContentRegionAvail();
    const float area_size_0    = std::min(available_size.x, available_size.y);
    const int   area_size      = std::max(1, static_cast<int>(area_size_0));
    m_context.material_preview->resize(area_size, area_size);
    m_context.material_preview->update_rendertarget(*m_context.graphics_device);
    m_context.material_preview->render_preview(selected_material);
    m_context.material_preview->show_preview();

    auto* node = m_context.brdf_slice->get_node();
    if (node != nullptr) {
        node->set_material(selected_material);
        if (ImGui::TreeNodeEx("BRDF Slice", ImGuiTreeNodeFlags_None)) {
            m_context.brdf_slice->show_brdf_slice(area_size);
            ImGui::TreePop();
        }
    }
}

auto Properties::effective_items() -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    const std::shared_ptr<erhe::Item_base> target = m_target.lock();
    if (target) {
        m_target_items.clear();
        m_target_items.push_back(target);
        return m_target_items;
    }
    return m_context.selection->get_selected_items();
}

void Properties::set_target(const std::shared_ptr<erhe::Item_base>& item)
{
    m_target = item;
}

void Properties::target_selector_imgui()
{
    static const char* const c_selection_mode_names[] = {"Individual", "Combined"};
    int selection_mode = static_cast<int>(m_selection_mode);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Individual").x + ImGui::GetFrameHeight() + 3.0f * ImGui::GetStyle().FramePadding.x);
    if (ImGui::Combo("##selection_mode", &selection_mode, c_selection_mode_names, IM_ARRAYSIZE(c_selection_mode_names))) {
        m_selection_mode = static_cast<Selection_mode>(selection_mode);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Multi-selection: Individual shows every item on its own; Combined edits the items of each type together");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Pin");
    ImGui::SameLine();
    const bool was_pinned = !m_target.expired();
    std::shared_ptr<erhe::Item_base> value = m_target.lock();
    Item_reference_options options;
    options.none_text                   = "(selection)";
    options.show_select_button          = false;
    // Any item type: the widget iterates the type bits, so an all-ones mask
    // accepts every leaf payload (plus content-library assets, unwrapped).
    if (item_reference_imgui(m_context, "properties_target", value, ~uint64_t{0}, options)) {
        m_target = value;
    }
    if (was_pinned) {
        ImGui::SameLine();
        ImGui::TextDisabled("(pinned)");
    }
    ImGui::Separator();
}

void Properties::imgui()
{
    ERHE_PROFILE_FUNCTION();

    reset();

    target_selector_imgui();

    const std::vector<std::shared_ptr<erhe::Item_base>>& items = effective_items();

    const bool combined = (items.size() > 1) && (m_selection_mode == Selection_mode::combined);
    if (!combined) {
        int id = 0;
        for (const auto& item : items) {
            ImGui::PushID(id++);
            ERHE_DEFER( ImGui::PopID(); );
            ERHE_VERIFY(item);
            item_properties(item);
        }
    } else {
        // Multi-selection: one row set per selected property owner type
        // (the items of one type edit together, with mixed-value display
        // and one operation per edit), in the order the types first appear,
        // so an item of a different type never hides another's rows.
        for (std::vector<std::shared_ptr<erhe::Item_base>>& group : m_type_groups) {
            group.clear();
        }
        std::size_t group_count = 0;
        const auto add_to_group = [this, &group_count](const std::shared_ptr<erhe::Item_base>& item) {
            const erhe::property::Owner_type owner_type = item->get_property_owner_type();
            std::size_t group_index = 0;
            while ((group_index < group_count) && (m_type_groups[group_index].front()->get_property_owner_type() != owner_type)) {
                ++group_index;
            }
            if (group_index == group_count) {
                if (m_type_groups.size() == group_count) {
                    m_type_groups.emplace_back();
                }
                ++group_count;
            }
            m_type_groups[group_index].push_back(item);
        };
        for (const std::shared_ptr<erhe::Item_base>& selected : items) {
            const std::shared_ptr<erhe::Item_base>& item = selected;
            add_to_group(item);
            // A node's attachments get their sections too (the single-item
            // path draws them under the node).
            const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                    add_to_group(attachment);
                }
            }
        }
        for (std::size_t group_index = 0; group_index < group_count; ++group_index) {
            const std::vector<std::shared_ptr<erhe::Item_base>>& group = m_type_groups[group_index];
            // The rows carry their own ImGui ids (the first item's id).
            push_group(
                (group_count == 1)
                    ? fmt::format("Properties ({} items)", group.size())
                    : fmt::format("{} Properties ({} item{})", group.front()->get_type_name(), group.size(), (group.size() == 1) ? "" : "s"),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed,
                m_indent
            );
            m_dependency_rows.add_rows(*this, group);
            pop_group();
        }
        // The rows hold their own snapshot; the scratch must not pin the
        // items between frames (scene-close leak class).
        for (std::vector<std::shared_ptr<erhe::Item_base>>& group : m_type_groups) {
            group.clear();
        }
    }

    const auto selected_animation = get<erhe::scene::Animation>(items);
    if (selected_animation) {
        animation_properties(selected_animation);
    }

    const auto selected_skin = get<erhe::scene::Skin>(items);
    if (selected_skin) {
        skin_properties(*selected_skin.get());
    }

    show_entries();

    material_properties(items);

}

}
