#pragma once

#include "scene/content_library.hpp"

#include "erhe/imgui/imgui_window.hpp"

#include <memory>
#include <thread>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor
{

class Editor_scenes;

class Content_library_window
    : public erhe::imgui::Imgui_window
{
public:
    Content_library_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_scenes&               editor_scenes
    );

    // Implements Imgui_window
    void imgui() override;

    // Public API
    [[nodiscard]] auto selected_brush   () const -> std::shared_ptr<Brush>;
    [[nodiscard]] auto selected_material() const -> std::shared_ptr<erhe::primitive::Material>;

private:
    Editor_scenes& m_editor_scenes;

    template <typename T>
    class Item_list
    {
    public:
        void imgui(const Library<T>& library)
        {
            constexpr ImGuiTreeNodeFlags parent_flags{
                //ImGuiTreeNodeFlags_DefaultOpen       |
                ImGuiTreeNodeFlags_OpenOnArrow       |
                ImGuiTreeNodeFlags_OpenOnDoubleClick |
                ImGuiTreeNodeFlags_SpanFullWidth
            };

            if (!ImGui::TreeNodeEx(T::static_type_name(), parent_flags)) {
                return;
            }

            for (const auto& entry : library.entries()) {
                if (!entry->is_shown_in_ui()) {
                    continue;
                }
                bool selected = m_selected_entry == entry;

                ImGui::TreeNodeEx(
                    entry->get_label().c_str(),
                    ImGuiTreeNodeFlags_SpanAvailWidth   |
                    ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_Bullet           |
                    (selected
                        ? ImGuiTreeNodeFlags_Selected
                        : ImGuiTreeNodeFlags_None
                    )
                );
                const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                const bool hovered        = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

                if (hovered && mouse_released) {
                    if (selected) {
                        m_selected_entry.reset();
                    } else {
                        m_selected_entry = entry;
                    }
                }
            }
            ImGui::TreePop();
        }

        void reset()
        {
            m_selected_entry.reset();
        }

        void set_selected_entry(const std::shared_ptr<T>& entry)
        {
            m_selected_entry = entry;
        }

        [[nodiscard]] auto get_selected_entry() const -> std::shared_ptr<T>
        {
            return m_selected_entry;
        }

    private:
        std::shared_ptr<T> m_selected_entry;
    };

    Item_list<Brush>                     m_brushes;
    Item_list<erhe::scene::Camera>       m_cameras;
    Item_list<erhe::scene::Light>        m_lights;
    Item_list<erhe::scene::Mesh>         m_meshes;
    Item_list<erhe::primitive::Material> m_materials;
    //// Item_list<erhe::graphics::Texture>   m_textures;
};

} // namespace editor
