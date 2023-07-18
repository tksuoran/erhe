#pragma once

#include "editor_context.hpp"
#include "tools/brushes/brush.hpp"
#include "graphics/icon_set.hpp"

#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/animation.hpp"
#include "erhe/scene/item.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {
    class Texture;
};
namespace erhe::primitive {
    class Material;
};
namespace erhe::scene {
    class Animation;
    class Camera;
    class Light;
    class Mesh;
    class Skin;
};

namespace editor
{

class Brush;
class Editor_context;

template <typename T>
class Library
{
public:
    // T is derived from erhe::scene::Item
    Library() 
        : m_type_code{T::get_static_type()}
        , m_type_name{T::get_static_type_name()}
    {
    }

    // T is NOT derived from erhe::scene::Item
    explicit Library(
        const uint64_t item_type,
        const char*    type_name
    )
        : m_type_code{item_type}
        , m_type_name{type_name}
    {}

    [[nodiscard]] auto get_type_code() const -> uint64_t { return m_type_code; }
    [[nodiscard]] auto get_type_name() const -> const char* { return m_type_name; }
    [[nodiscard]] auto entries() -> std::vector<std::shared_ptr<T>>&;
    [[nodiscard]] auto entries() const -> const std::vector<std::shared_ptr<T>>&;

    template <typename ...Args>
    auto make(Args&& ...args) -> std::shared_ptr<T>;

    auto combo(
        Editor_context&     context,
        const char*         label,
        std::shared_ptr<T>& in_out_selected_entry,
        const bool          empty_option
    ) const -> bool;

    void add   (const std::shared_ptr<T>& entry);
    auto remove(const std::shared_ptr<T>& entry) -> bool;
    void clear ();

private:
    mutable std::mutex              m_mutex;
    uint64_t                        m_type_code;
    const char*                     m_type_name;
    std::vector<std::shared_ptr<T>> m_entries;
};

class Content_library
{
public:
    ~Content_library() noexcept;

    bool                               is_shown_in_ui{true};
    Library<Brush>                     brushes {erhe::scene::Item_type::brush, "Brush"};
    Library<erhe::scene::Animation>    animations;
    Library<erhe::scene::Camera>       cameras;
    Library<erhe::scene::Light>        lights;
    Library<erhe::scene::Mesh>         meshes;
    Library<erhe::scene::Skin>         skins;
    Library<erhe::primitive::Material> materials{erhe::scene::Item_type::material, "Material"};
    Library<erhe::graphics::Texture>   textures {erhe::scene::Item_type::texture,  "Texture"};
};

template <typename T>
auto Library<T>::entries() -> std::vector<std::shared_ptr<T>>&
{
    return m_entries;
}

template <typename T>
auto Library<T>::entries() const -> const std::vector<std::shared_ptr<T>>&
{
    return m_entries;
}

template <typename T>
template <typename ...Args>
auto Library<T>::make(Args&& ...args) -> std::shared_ptr<T>
{
    auto entry = std::make_shared<T>(std::forward<Args>(args)...);
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.push_back(entry);
    return entry;
}

template <typename T>
void Library<T>::clear()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_entries.clear();
}

template <typename T>
auto Library<T>::combo(
    Editor_context&     context,
    const char*         label,
    std::shared_ptr<T>& in_out_selected_entry,
    const bool          empty_option
) const -> bool
{
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    const char* preview_value = in_out_selected_entry ? in_out_selected_entry->get_label().c_str() : "(none)";
    bool selection_changed = false;
    if (ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge)) {
        if (empty_entry) {
            bool is_selected = !in_out_selected_entry;
            if (ImGui::Selectable("(none)", is_selected)) {
                in_out_selected_entry.reset();
                selection_changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        for (const auto& entry : m_entries) {
            if (!entry) {
                continue;
            }
            if (!entry->is_shown_in_ui()) {
                continue;
            }
            bool is_selected = (in_out_selected_entry == entry);
            context.icon_set->add_icons(get_type_code(), 1.0f);
            if (ImGui::Selectable(entry->get_label().c_str(), is_selected)) {
                in_out_selected_entry = entry;
                selection_changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    return selection_changed;
}

template <typename T>
void Library<T>::add(const std::shared_ptr<T>& entry)
{
    ERHE_VERIFY(entry);
    const std::lock_guard<std::mutex> lock{m_mutex};
    auto i = std::find(m_entries.begin(), m_entries.end(), entry);
    if (i != m_entries.end()) {
        return;
    }
    m_entries.push_back(entry);
}

template <typename T>
auto Library<T>::remove(const std::shared_ptr<T>& entry) -> bool
{
    ERHE_VERIFY(entry);
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = std::remove(m_entries.begin(), m_entries.end(), entry);
    if (i == m_entries.end()) {
        return false;
    }
    m_entries.erase(i, m_entries.end());
    return true;
}

} // namespace editor
