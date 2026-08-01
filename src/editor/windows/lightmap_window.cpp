#include "windows/lightmap_window.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>

namespace editor {

namespace {

// Lightmapped, non-skinned content mesh nodes of the active scene, as
// operation items (skinned meshes are excluded from baking - they have no
// static BLAS either).
[[nodiscard]] auto collect_lightmapped_mesh_nodes(App_context& context) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    const std::shared_ptr<Scene_root> scene_root = context.selection->get_active_scene_root();
    if (!scene_root) {
        return items;
    }
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root->layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            continue;
        }
        erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        std::shared_ptr<erhe::Item_base> item = std::dynamic_pointer_cast<erhe::Item_base>(node->shared_from_this());
        if (item) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

} // anonymous namespace

Lightmap_window::Lightmap_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Lightmap", "lightmap", true}
    , m_context   {app_context}
{
}

void Lightmap_window::generate_lightmap_uvs()
{
    const std::vector<std::shared_ptr<erhe::Item_base>> items = collect_lightmapped_mesh_nodes(m_context);
    if (items.empty()) {
        return;
    }
    const float hard_angles_deg = m_context.editor_settings->lightmap.hard_angles_deg;
    async_for_nodes_with_mesh(
        m_context,
        items,
        [this, hard_angles_deg](Mesh_operation_parameters&& params) {
            // Runs on a tf::Executor worker: queue() is main-thread-only.
            m_context.operation_stack->queue_from_thread(
                std::make_shared<Make_atlas_operation>(
                    std::move(params),
                    2, // lightmap UV channel (texcoord usage_index 2)
                    hard_angles_deg,
                    erhe::geometry::operation::Atlas_parameterizer::abf,
                    erhe::geometry::operation::Atlas_packer::xatlas
                )
            );
        }
    );
}

void Lightmap_window::imgui()
{
    Lightmap_config& config = m_context.editor_settings->lightmap;

    const std::vector<std::shared_ptr<erhe::Item_base>> lightmapped = collect_lightmapped_mesh_nodes(m_context);
    ImGui::Text("Lightmapped meshes in active scene: %zu", lightmapped.size());
    if (lightmapped.empty()) {
        ImGui::TextUnformatted("Enable the \"Lightmapped\" flag on content meshes (Properties window) to include them.");
    }

    if (ImGui::DragFloat("Texels per meter", &config.texels_per_meter, 0.5f, 1.0f, 256.0f, "%.1f")) {
        m_context.app_settings->settings_store().touch();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Lightmap texel density; sets each instance's atlas region size. The one quality knob.");
    }

    ImGui::BeginDisabled(lightmapped.empty());
    if (ImGui::Button("Generate Lightmap UVs")) {
        generate_lightmap_uvs();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatic UV unwrap (ABF + xatlas) into texcoord channel 2 for every lightmapped mesh.\n"
            "Undoable. Inspect with Scene View Config > Shader Debug > TexCoord 2 (Lightmap)."
        );
    }

    // Plan phase 3 turns this into the interactive bake toggle.
    ImGui::BeginDisabled(true);
    bool baking = false;
    ImGui::Checkbox("Baking", &baking);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Interactive lightmap baking is not implemented yet (doc/lightmap_baking_plan.md phase 3).");
    }
}

} // namespace editor
