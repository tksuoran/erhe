#pragma once

#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include <memory>
#include <thread>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Editor_scenes;
class Material_library;
class Selection_tool;

class Materials_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Materials"};
    static constexpr std::string_view c_title{"Materials"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Materials_window();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    [[nodiscard]] auto selected_material_library() const -> std::shared_ptr<Material_library>;
    [[nodiscard]] auto selected_material        () const -> std::shared_ptr<erhe::primitive::Material>;

private:
    // Component dependencies
    std::shared_ptr<Editor_scenes> m_editor_scenes;

    std::vector<const char*>                   m_material_names;
    std::shared_ptr<Material_library>          m_selected_material_library;
    std::shared_ptr<erhe::primitive::Material> m_selected_material;
};

} // namespace editor
