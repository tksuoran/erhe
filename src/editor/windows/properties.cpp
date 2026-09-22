#include "windows/properties.hpp"

#include "animation/animation_window.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "content_library/brdf_slice.hpp"
#include "content_library/content_library.hpp"
#include "texture_graph/graph_texture.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include <algorithm>
#include "operations/compound_operation.hpp"
#include "operations/material_change_operation.hpp"
#include "operations/mesh_material_assign_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"

#include "app_scenes.hpp"
#include "preview/material_preview.hpp"
#include "rendertarget_mesh.hpp"
#include "scene/ik_properties.hpp"
#include "scene/joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
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
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
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

#include <algorithm>
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
    // Below the Rest Rotation row of a bone's "IK" group
    // (doc/plans/rigging/ik_settings.md section 5): the same undoable write the
    // generic rows and the MCP set_item_property tool record, one undo step
    // for the whole selection.
    m_dependency_rows.add_row_action(
        Property_row_action{
            .property    = Ik::rest_rotation_property.get_ptr(),
            .label       = "Set Rest",
            .button_text = "Set rest from current pose",
            .tooltip     = "Re-capture the reference orientation that defines the zero angle of the limits from the bone's current local rotation",
            .execute     = [this](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                const erhe::property::Dependency_property& property = Ik::rest_rotation_property.get();
                Compound_operation::Parameters parameters;
                for (const std::shared_ptr<erhe::Item_base>& item : items) {
                    const std::shared_ptr<erhe::scene::Node> bone = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                    if (!bone) {
                        continue;
                    }
                    const erhe::property::Property_value after{bone->parent_from_node_transform().get_rotation()};
                    parameters.operations.push_back(
                        std::make_shared<Property_set_operation>(bone, property, bone->read_local_state(property), to_local_state(after))
                    );
                }
                if (parameters.operations.size() == 1) {
                    m_context.operation_stack->queue(parameters.operations.front());
                } else if (!parameters.operations.empty()) {
                    m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
                }
            }
        }
    );

    // At the end of a node's "Rigid Body" group (doc/erhe/property_system.md
    // 4.26): the read-only state of the body the group implies, read back
    // from the scene's Node_physics_system. One item only - the readouts
    // describe one body, and the combined multi-selection form edits values.
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Rigid Body",
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                if (items.size() != 1) {
                    return;
                }
                const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(items.front());
                if (node) {
                    node_physics_properties(*node.get());
                }
            }
        }
    );

    // The hand-written groups of an item section, each drawn inside the
    // generic section as a shared property group (fold state, order and
    // drag reordering like every other group). They describe one item, so
    // each applies to a single-item selection of the right class only.
    const auto single = [](const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> erhe::Item_base* {
        return (items.size() == 1) ? items.front().get() : nullptr;
    };
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Scene Overrides",
            .applies  = [this, single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                erhe::scene::Scene* const scene = dynamic_cast<erhe::scene::Scene*>(single(items));
                return (scene != nullptr) && (scene->get_item_host() != nullptr) && (m_context.editor_settings != nullptr);
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                scene_override_properties(*dynamic_cast<erhe::scene::Scene*>(items.front().get()));
            }
        }
    );
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Variants",
            .applies  = [single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                erhe::scene::Scene* const scene = dynamic_cast<erhe::scene::Scene*>(single(items));
                if ((scene == nullptr) || (scene->get_item_host() == nullptr)) {
                    return false;
                }
                return !static_cast<Scene_root*>(scene->get_item_host())->get_variant_table().get_sets().empty();
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                variant_properties(*static_cast<Scene_root*>(dynamic_cast<erhe::scene::Scene*>(items.front().get())->get_item_host()));
            }
        }
    );
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Skin",
            .applies  = [single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                erhe::scene::Mesh* const mesh = dynamic_cast<erhe::scene::Mesh*>(single(items));
                return (mesh != nullptr) && static_cast<bool>(mesh->skin);
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                skin_properties(*dynamic_cast<erhe::scene::Mesh*>(items.front().get())->skin.get());
            }
        }
    );
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Primitives",
            .applies  = [single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                erhe::scene::Mesh* const mesh = dynamic_cast<erhe::scene::Mesh*>(single(items));
                return (mesh != nullptr) && (mesh->get_item_host() != nullptr) && !mesh->get_primitives().empty();
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                mesh_primitive_properties(*dynamic_cast<erhe::scene::Mesh*>(items.front().get()));
            }
        }
    );
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Mesh Raytrace",
            .applies  = [this, single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                erhe::scene::Mesh* const mesh = dynamic_cast<erhe::scene::Mesh*>(single(items));
                return m_context.developer_mode && (mesh != nullptr) && (mesh->get_item_host() != nullptr);
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                mesh_raytrace_properties(*dynamic_cast<erhe::scene::Mesh*>(items.front().get()));
            }
        }
    );
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Texture", // the size and format rows are the texture's computed properties
            .applies  = {},
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                if (items.size() == 1) {
                    texture_properties(std::dynamic_pointer_cast<erhe::graphics::Texture>(items.front()));
                }
            }
        }
    );
#if 0
    m_dependency_rows.add_group_rows(
        Property_group_rows{
            .group    = "Polygons",
            .applies  = [single](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                const erhe::scene::Node* const node = dynamic_cast<const erhe::scene::Node*>(single(items));
                if (node == nullptr) {
                    return false;
                }
                const std::optional<Brush_placement_data> placement = read_brush_placement(*node);
                return placement.has_value() && static_cast<bool>(placement.value().brush);
            },
            .add_rows = [this](Property_editor&, const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                brush_placement_properties(*dynamic_cast<const erhe::scene::Node*>(items.front().get()));
            }
        }
    );
#endif

    // Below each "Sizes X/Y/Z" row of a grid layout node: the toggle between
    // uniform tracks (the empty list) and per-track sizes seeded from the
    // node's layout volume (doc/erhe/property_system.md section 4.13). It
    // records the same Property_set_operation the generic row and MCP
    // set_item_property record.
    for (int axis = 0; axis < 3; ++axis) {
        static const char* const c_axis_labels[3] = {"Custom Sizes X", "Custom Sizes Y", "Custom Sizes Z"};
        m_dependency_rows.add_row_action(
            Property_row_action{
                .property    = erhe::scene::Layout::grid_track_extent_property(axis).get_ptr(),
                .label       = c_axis_labels[axis],
                .button_text = "Custom track sizes on / off",
                .tooltip     = "Seed one size per track from the layout volume along this axis, or clear the list back to uniform tracks",
                .execute     = [this, axis](const std::vector<std::shared_ptr<erhe::Item_base>>& items) {
                    const erhe::property::Dependency_property& property = erhe::scene::Layout::grid_track_extent_property(axis).get();
                    Compound_operation::Parameters parameters;
                    for (const std::shared_ptr<erhe::Item_base>& item : items) {
                        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                        if (!node) {
                            continue; // a style holding the list has no layout volume to seed from
                        }
                        const std::optional<erhe::scene::Layout_data> data = erhe::scene::read_layout(*node.get());
                        if (!data.has_value()) {
                            continue; // not a layout node
                        }
                        std::vector<float> after;
                        if (data.value().grid_track_extent[static_cast<std::size_t>(axis)].empty()) {
                            const int   track_count = data.value().grid_track_count[axis];
                            const int   count       = (track_count > 1) ? track_count : 1;
                            const float total       = data.value().volume.max[axis] - data.value().volume.min[axis];
                            const float per         = (total > 0.0f) ? (total / static_cast<float>(count)) : 0.0f;
                            after.assign(static_cast<std::size_t>(count), per);
                        }
                        parameters.operations.push_back(
                            std::make_shared<Property_set_operation>(node, property, node->read_local_state(property), to_local_state(erhe::property::Property_value{after}))
                        );
                    }
                    if (parameters.operations.size() == 1) {
                        m_context.operation_stack->queue(parameters.operations.front());
                    } else if (!parameters.operations.empty()) {
                        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
                    }
                }
            }
        );
    }

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
    // properties drawn by the generic rows (doc/erhe/property_system.md section
    // 4.16). Playback and curve editing live in the Animation window
    // (issue #243).
    add_entry("Edit", [this, animation]() {
        if (ImGui::Button("Open in Animation Window")) {
            m_context.animation_window->set_animation(animation);
            m_context.animation_window->show_window();
        }
    });
}

// The rows of the "Scene Overrides" group (a shared property group hook).
void Properties::scene_override_properties(erhe::scene::Scene& scene)
{
    ERHE_PROFILE_FUNCTION();

    Scene_root* scene_root = static_cast<Scene_root*>(scene.get_item_host());
    if (scene_root == nullptr) {
        return;
    }

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
}

void Properties::variant_properties(Scene_root& scene_root)
{
    ERHE_PROFILE_FUNCTION();

    // One combo per variant set the scene carries
    // (doc/erhe/usd_compatibility_design.md X4). Change-driven: the combo is drawn
    // from the table, and only a change queues the switch. A set a variant
    // block declares is listed below the set carrying that block, indented and
    // named by the block, and is editable only while that block is the
    // selected one - its variants reach the scene through nothing else
    // (doc/plans/usd_compatibility.md, "Variant opinions a variant set does
    // not carry").
    Variant_table&                  variant_table = scene_root.get_variant_table();
    const std::vector<Variant_set>& sets          = variant_table.get_sets();
    for (const Variant_set& set : sets) {
        const std::shared_ptr<erhe::Item_base> prim = set.prim.lock();
        if (!prim) {
            continue; // the carrying prim is gone; the table drops the set on the removal message
        }
        const Variant_set_key key  = set.get_key();
        const bool            live = variant_table.is_live(set);
        // How deep the chain of blocks this set is declared inside runs, which
        // is how far its row is indented. The table lists a set after the set
        // carrying the block it is declared in, so the rows read as the tree
        // they are.
        std::size_t depth = 0;
        for (const Variant_set* enclosing = variant_table.find_enclosing_set(set);
             (enclosing != nullptr) && (depth <= sets.size());
             enclosing = variant_table.find_enclosing_set(*enclosing))
        {
            ++depth;
        }
        // A set the scene's root prim carries has the empty path (a glTF
        // asset's one variant list, X4): label it by the prim's name.
        const std::string prim_label = key.prim_path.empty() ? prim->get_name() : key.prim_path;
        std::string label = key.enclosing_set_name.empty()
            ? (prim_label + " : " + key.set_name)
            : (std::string(4 * depth, ' ') + key.enclosing_set_name + "=" + key.enclosing_variant_name + " : " + key.set_name);
        std::string tooltip = key.enclosing_set_name.empty()
            ? std::string{"Which variant of this variant set the scene has selected."}
            : fmt::format(
                "Which variant of this variant set the scene has selected. The set is declared inside the '{}' "
                "block of '{}'{}",
                key.enclosing_variant_name,
                key.enclosing_set_name,
                live ? "." : ", which is not the selected block: select it to bring this set in."
            );
        add_entry(
            std::move(label),
            [this, &scene_root, &set, key, live]() {
                if (!live) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::BeginCombo("##", set.selected.c_str())) {
                    for (const Variant& variant : set.variants) {
                        const bool is_selected = (variant.name == set.selected);
                        if (ImGui::Selectable(variant.name.c_str(), is_selected) && !is_selected) {
                            const std::string error = scene_root.select_variant(
                                m_context, key, variant.name, Scene_root::Variant_switch_mode::undoable
                            );
                            if (!error.empty()) {
                                log_scene->warn("select variant: {}", error);
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!live) {
                    ImGui::EndDisabled();
                }
            },
            std::move(tooltip)
        );
    }
}

void Properties::light_properties(erhe::scene::Light& light)
{
    ERHE_PROFILE_FUNCTION();

    // The authored light state (type, cast shadow, spot angles, range,
    // intensity, color, temperature) and the derived rows (flux, the
    // blackbody swatch: computed properties, D26) are drawn by the generic
    // property rows (Dependency_property_rows); every write re-resolves the
    // scene light set through the Light property callback
    // (doc/erhe/property_system.md D19). Only the diagnostic remains here.
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

void Properties::skin_properties(erhe::scene::Skin& skin)
{
    ERHE_PROFILE_FUNCTION();

    // The skeleton name and the joint count are the skin's computed
    // properties; the joint names follow as rows of the same "Skin" group.
    auto& skin_data = skin.skin_data;
    for (auto& joint : skin_data.joints) {
        if (!joint) {
            add_entry("", [](){ ImGui::TextUnformatted("(missing joint)"); });
        } else {
            add_entry("", [&](){ ImGui::TextUnformatted(joint->get_name().c_str()); });
        }
    }
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

    // The size and format are the texture's computed properties, drawn as
    // the "Texture" group's property rows above this preview.
    add_entry("Preview", [this, texture](){
        // TODO Draw to available size respecting aspect ratio
        m_context.imgui_renderer->image(
            erhe::imgui::Draw_texture_parameters{
                .texture_reference = texture, //texture.get(),
                .width             = texture->get_width(),
                .height            = texture->get_height(),
                // Uploaded image content: identity UVs, not the render-target UVs
                .uv0               = glm::vec2{0.0f, 0.0f},
                .uv1               = glm::vec2{1.0f, 1.0f},
                .debug_label       = "Properties::texture_properties()"
            }
        );
    });
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

void Properties::shape_properties(const char* label, erhe::primitive::Primitive_shape* shape, const Shape_kind shape_kind)
{
    ERHE_PROFILE_FUNCTION();

    if (shape == nullptr) {
        return;
    }

    if (m_context.developer_mode) {
        push_group(label, ImGuiTreeNodeFlags_None, m_indent);
    }

    // The render shape's geometry counts are the primitive's computed
    // properties ("Geometry" group of the primitive's rows); the other
    // shapes list theirs here.
    if (shape_kind == Shape_kind::other) {
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry_const();
        if (geometry) {
            geometry_properties(*geometry.get());
        }
    }

    if (m_context.developer_mode) {
        primitive_raytrace_properties(&shape->get_raytrace());
        pop_group();
    }
}

// The rows of the "Primitives" group (a shared property group hook): one
// nested group per primitive with its registered sub-object rows, its shapes
// and buffer meshes.
void Properties::mesh_primitive_properties(erhe::scene::Mesh& mesh)
{
    ERHE_PROFILE_FUNCTION();

    auto* scene_root = static_cast<Scene_root*>(mesh.get_item_host());
    if (scene_root == nullptr) {
        // Mesh host not set
        return;
    }

    const std::shared_ptr<erhe::Item_base> mesh_shared = mesh.shared_from_this();

    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh.get_primitives();
    const std::size_t primitive_count = mesh_primitives.size();

    for (size_t primitive_index = 0; primitive_index < primitive_count; ++primitive_index) {
        const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives.at(primitive_index);
        while (m_primitive_labels.size() <= primitive_index) {
            m_primitive_labels.push_back(fmt::format("Primitive {}", m_primitive_labels.size()));
        }
        push_group(m_primitive_labels.at(primitive_index).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
        // The primitive's registered properties (its material, the geometry
        // counts): generic rows on the property sub-object
        // (doc/erhe/property_system.md D29), undo through
        // Property_set_operation on (mesh, primitive index).
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
            shape_properties("Render shape", primitive.render_shape.get(), Shape_kind::render);
            buffer_mesh_properties("Renderable Buffer Mesh", &primitive.render_shape->get_renderable_mesh());
        }
        // Inspecting a primitive should show what is actually resident, so list
        // the optimized build separately whenever one is live.
        if (primitive.optimized_render_shape) {
            shape_properties("Optimized render shape", primitive.optimized_render_shape.get(), Shape_kind::other);
            buffer_mesh_properties("Optimized Buffer Mesh", &primitive.optimized_render_shape->get_renderable_mesh());
        }
        if (m_context.developer_mode && primitive.collision_shape) {
            shape_properties("Collision shape", primitive.collision_shape.get(), Shape_kind::other);
        }
        pop_group();
    }
}

// The rows of the developer-mode "Mesh Raytrace" group (a shared property
// group hook).
void Properties::mesh_raytrace_properties(erhe::scene::Mesh& mesh)
{
    ERHE_PROFILE_FUNCTION();

    const auto* mesh_rt_scene = mesh.get_rt_scene();
    if (mesh_rt_scene != nullptr) {
        add_entry("RT Scene", [=](){ ImGui::TextUnformatted(mesh_rt_scene->debug_label().data()); });
    }
    const auto& rt_primitives = mesh.get_rt_primitives();
    const std::size_t rt_primitive_count = rt_primitives.size();
    if (!rt_primitives.empty()) {
        push_group("Raytrace Primitives", ImGuiTreeNodeFlags_None, m_indent);
        for (size_t rt_primitive_index = 0; rt_primitive_index < rt_primitive_count; ++rt_primitive_index) {
            while (m_rt_primitive_labels.size() <= rt_primitive_index) {
                m_rt_primitive_labels.push_back(fmt::format("Raytrace Primitive {}", m_rt_primitive_labels.size()));
            }
            const auto& rt_primitive = rt_primitives.at(rt_primitive_index);

            const auto* rt_instance = rt_primitive->rt_instance.get();
            const auto* rt_scene    = rt_primitive->rt_scene.get();
            push_group(m_rt_primitive_labels.at(rt_primitive_index).c_str(), ImGuiTreeNodeFlags_DefaultOpen, m_indent);
            add_entry("Mesh",            [&](){ ImGui::TextUnformatted((rt_primitive->mesh != nullptr) ? rt_primitive->mesh->get_name().c_str() : "(nullptr)"); });
            add_entry("Primitive Index", [&](){ ImGui::Text("%zu", rt_primitive->primitive_index); });
            add_entry("RT Instance",     [=](){ ImGui::TextUnformatted((rt_instance != nullptr) ? rt_instance->debug_label().data() : "(nullptr)"); });
            add_entry("RT Scene",        [=](){ ImGui::TextUnformatted((rt_scene != nullptr) ? rt_scene->debug_label().data() : "(nullptr)"); });
            pop_group();
        }
        pop_group();
    }
}

// The developer-mode diagnostic rows of a mesh outside any group.
void Properties::mesh_properties(erhe::scene::Mesh& mesh)
{
    ERHE_PROFILE_FUNCTION();

    if (m_context.developer_mode) {
        add_entry("Layer ID", [&](){ ImGui::Text("%u %s", static_cast<unsigned int>(mesh.layer_id), layer_name(mesh.layer_id)); });
    }
}

void Properties::brush_placement_properties(const erhe::scene::Node& node)
{
    ERHE_PROFILE_FUNCTION();

    // The brush, facet and corner are generic rows of the node itself
    // (doc/erhe/property_system.md 4.11); the polygon counts of the brush
    // are the rows of the "Polygons" group (a shared property group hook).
    const std::optional<Brush_placement_data> placement = read_brush_placement(node);
    if (!placement.has_value()) {
        return;
    }
    const std::shared_ptr<Brush>& brush = placement.value().brush;
    if (!brush) {
        return;
    }
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
}

void Properties::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
}

void Properties::on_end()
{
    ImGui::PopStyleVar();
}

void Properties::node_physics_properties(const erhe::scene::Node& node)
{
    ERHE_PROFILE_FUNCTION();

    erhe::physics::IRigid_body* rigid_body = get_node_rigid_body(node);
    if (rigid_body == nullptr) {
        return;
    }

    const glm::mat4 transform = rigid_body->get_world_transform();
    const glm::vec3 pos       = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    add_entry("Rigid Body", [=](){ ImGui::TextUnformatted(rigid_body->get_debug_label()); });
    add_entry("Position",   [=](){ ImGui::Text("%.2f, %.2f, %.2f", pos.x, pos.y, pos.z); });

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
    // The authored rigid body state (motion mode, trigger, mass, gravity
    // factor, initial velocities, center of mass, physics material,
    // collision filter, collision mesh) is the generic rows of the same
    // "Rigid Body" group (doc/erhe/property_system.md 4.26), drawn above
    // these.
}


void Properties::joint_properties(Joint& joint)
{
    ERHE_PROFILE_FUNCTION();

    // The two frame nodes, the joint settings and the collision flag are
    // generic property rows (doc/erhe/property_system.md section 4.17); the
    // actions and the diagnostic remain here.
    add_entry(
        "Connect",
        [this, &joint]() {
            if (ImGui::Button("Connect to Selected Node", ImVec2{-FLT_MIN, 0.0f})) {
                const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();
                const std::shared_ptr<erhe::scene::Node> frame_0 = joint.get_body_0();
                for (const std::shared_ptr<erhe::Item_base>& selected_item : selected_items) {
                    const std::shared_ptr<erhe::scene::Node> selected_node = std::dynamic_pointer_cast<erhe::scene::Node>(selected_item);
                    if (selected_node && (selected_node != frame_0)) {
                        joint.set_body_1(selected_node);
                        break;
                    }
                }
            }
        },
        "Names the first selected node other than the joint's first frame node as the second frame node"
    );

    add_entry("Constraint", [&joint]() {
        ImGui::TextUnformatted((joint.get_constraint() != nullptr) ? "Created" : "Pending");
    });

    add_entry(
        "Rebuild",
        [&joint]() {
            if (ImGui::Button("Rebuild Joint", ImVec2{-FLT_MIN, 0.0f})) {
                joint.rebuild();
            }
        },
        "Recreates the constraint, re-capturing the joint frames; use after moving the nodes"
    );
}

// Developer diagnostics (R3 of doc/editor/properties_window.md): the
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
    return !erhe::is<Rendertarget_mesh>(item);
}

// The per-item part of the window (R3 / R5 of
// doc/editor/properties_window.md): the read-only diagnostics of the
// item's class, the actions, and the list editors that have no property
// form (the scene's settings block, a collision filter's system lists,
// joint limits and drives), all drawn per item and
// disabled while the item is sealed.
void Properties::item_diagnostics(const std::shared_ptr<erhe::Item_base>& item)
{
    // The ungrouped diagnostic rows of an item; the grouped ones are the
    // shared property group hooks registered in the constructor.
    const auto& joint            = std::dynamic_pointer_cast<Joint                  >(item);
    const auto& light            = std::dynamic_pointer_cast<erhe::scene::Light     >(item);
    const auto& mesh             = std::dynamic_pointer_cast<erhe::scene::Mesh      >(item);

    const bool edit_disabled = item->is_lock_edit();
    if (edit_disabled) {
        ImGui::BeginDisabled();
    }
    if (joint)            { joint_properties(*joint); }
    if (light)            { light_properties(*light); }
    if (mesh)             { mesh_properties(*mesh); }
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
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed;
    if (!erhe::is<Rendertarget_mesh>(item.get())) {
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

    // The registered rows of the item, the values of its attached groups
    // among them (doc/erhe/property_system.md section 4.23).
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

    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = m_context.selection->get_selected_items();

    // Individual mode shows one section per item: the active item
    // (doc/editor/active_item.md D5) comes first, the rest keep selection order.
    // Combined mode groups by owner type, where the order does not show.
    if (m_selection_mode != Selection_mode::individual) {
        return selected_items;
    }
    const std::shared_ptr<erhe::Item_base> active_item = m_context.selection->get_active_item();
    if (!active_item) {
        return selected_items;
    }
    const std::vector<std::shared_ptr<erhe::Item_base>>::const_iterator i = std::find(selected_items.begin(), selected_items.end(), active_item);
    if (i == selected_items.end()) {
        return selected_items;
    }
    m_ordered_items.clear();
    m_ordered_items.reserve(selected_items.size());
    m_ordered_items.push_back(active_item);
    for (const std::shared_ptr<erhe::Item_base>& item : selected_items) {
        if (item != active_item) {
            m_ordered_items.push_back(item);
        }
    }
    return m_ordered_items;
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
            add_to_group(selected);
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

    // The active-item-first scratch must not pin the selection between frames
    // (scene-close leak class); the capacity is kept.
    m_ordered_items.clear();
}

}
