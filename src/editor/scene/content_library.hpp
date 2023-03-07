#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/primitive/material.hpp"

#include <imgui/imgui.h>

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics
{
class Texture;
};

namespace erhe::primitive
{
class Material;
};

namespace erhe::scene
{
class Camera;
class Light;
class Mesh;
};

namespace editor
{

class Brush;

template <typename T>
class Library
{
public:
    [[nodiscard]] auto entries() -> std::vector<std::shared_ptr<T>>&;
    [[nodiscard]] auto entries() const -> const std::vector<std::shared_ptr<T>>&;

    template <typename ...Args>
    auto make(Args&& ...args) -> std::shared_ptr<T>;

    auto combo(
        const char*         label,
        std::shared_ptr<T>& in_out_selected_entry,
        const bool          empty_option
    ) const -> bool;

    void add   (const std::shared_ptr<T>& entry);
    auto remove(const std::shared_ptr<T>& entry) -> bool;
    void clear ();

private:
    mutable std::mutex              m_mutex;
    std::vector<std::shared_ptr<T>> m_entries;
};

class Content_library
{
public:
    bool                               is_shown_in_ui{true};
    Library<Brush>                     brushes;
    Library<erhe::scene::Camera>       cameras;
    Library<erhe::scene::Light>        lights;
    Library<erhe::scene::Mesh>         meshes;
    Library<erhe::primitive::Material> materials;
    Library<erhe::graphics::Texture>   texture;
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
    const char*         label,
    std::shared_ptr<T>& in_out_selected_entry,
    const bool          empty_option
) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<T>> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry) {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_entries) {
        if (!entry->is_shown_in_ui()) {
            continue;
        }
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry) {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (in_out_selected_entry != entries.at(selection_index));
    if (selection_changed) {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

template <typename T>
void Library<T>::add(const std::shared_ptr<T>& entry)
{
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
    const std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = std::remove(m_entries.begin(), m_entries.end(), entry);
    if (i == m_entries.end()) {
        return false;
    }
    m_entries.erase(i, m_entries.end());
    return true;
}

} // namespace editor
