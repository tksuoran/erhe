#pragma once

#include "erhe/primitive/material.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace editor
{

class Material_library
{
public:
    Material_library();

    [[nodiscard]] auto materials() -> std::vector<std::shared_ptr<erhe::primitive::Material>>&;
    [[nodiscard]] auto materials() const -> const std::vector<std::shared_ptr<erhe::primitive::Material>>&;

    void add_default_materials();

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto material_combo(
        const char*                                 label,
        std::shared_ptr<erhe::primitive::Material>& selected_material,
        const bool                                  empty_option = false
    ) const -> bool;
#endif

    template <typename ...Args>
    auto make_material(
        const std::string_view name,
        Args&& ...args
    ) -> std::shared_ptr<erhe::primitive::Material>
    {
        auto material = std::make_shared<erhe::primitive::Material>(
            name,
            std::forward<Args>(args)...
        );
        const std::lock_guard<std::mutex> lock{m_mutex};

        material->index = m_materials.size();
        m_materials.push_back(material);
        return material;
    }

private:
    mutable std::mutex                                      m_mutex;
    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;
};

} // namespace editor
