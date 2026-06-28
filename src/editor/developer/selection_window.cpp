#include "developer/selection_window.hpp"

#include "app_context.hpp"
#include "graphics/icon_set.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_item/item.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/mesh.hpp"

#include <string>

namespace editor {

Selection_window::Selection_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Selection", "selection", true}
    , m_context                {app_context}
{
}

void Selection_window::imgui()
{
    const float scale = get_scale_value();

    const auto& items = m_context.selection->get_selected_items();
    for (const auto& item : items) {
        m_context.icon_set->item_icon(item, scale);
        ImGui::TextUnformatted(item->get_name().c_str());
    }

    // Mesh-component (vertex / edge / face) sub-selection, with the concrete
    // selected component IDs. Surfaced here to visually debug region (box / paint)
    // selection - the IDs shown are exactly the indices resolved from the GPU ID
    // buffer and stored in Mesh_component_selection.
    Mesh_component_selection* component_selection = m_context.mesh_component_selection;
    if (component_selection != nullptr) {
        const std::vector<Mesh_component_entry>& entries = component_selection->get_entries();
        if (!entries.empty()) {
            static const char* const c_mode_names[] = {"Object", "Vertex", "Edge", "Face"};
            const int mode_index = static_cast<int>(component_selection->get_mode());
            ImGui::SeparatorText("Mesh components");
            ImGui::Text("Mode: %s", c_mode_names[(mode_index >= 0 && mode_index < 4) ? mode_index : 0]);

            int entry_index = 0;
            for (const Mesh_component_entry& entry : entries) {
                ImGui::PushID(entry_index++);
                const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
                const std::string mesh_name = mesh ? mesh->get_name() : std::string{"(freed)"};
                const bool        live      = component_selection->is_live(entry);
                if (
                    ImGui::TreeNodeEx(
                        "entry",
                        ImGuiTreeNodeFlags_DefaultOpen,
                        "%s [prim %zu]%s  v:%zu e:%zu f:%zu",
                        mesh_name.c_str(),
                        entry.primitive_index,
                        live ? "" : " (not live)",
                        entry.vertices.size(),
                        entry.edges.size(),
                        entry.facets.size()
                    )
                ) {
                    if (!entry.facets.empty()) {
                        std::string ids;
                        for (const GEO::index_t f : entry.facets) {
                            ids += std::to_string(f);
                            ids += ' ';
                        }
                        ImGui::TextWrapped("Faces: %s", ids.c_str());
                    }
                    if (!entry.vertices.empty()) {
                        std::string ids;
                        for (const GEO::index_t v : entry.vertices) {
                            ids += std::to_string(v);
                            ids += ' ';
                        }
                        ImGui::TextWrapped("Vertices: %s", ids.c_str());
                    }
                    if (!entry.edges.empty()) {
                        std::string ids;
                        for (const Mesh_edge_key& e : entry.edges) {
                            ids += std::to_string(e.first);
                            ids += '-';
                            ids += std::to_string(e.second);
                            ids += ' ';
                        }
                        ImGui::TextWrapped("Edges: %s", ids.c_str());
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }
}

}