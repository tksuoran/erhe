#pragma once

#include "windows/imgui_window.hpp"

#include <memory>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Scene_root;
class Selection_tool;

class Materials
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Materials"};
    static constexpr std::string_view c_title{"Materials"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Materials ();
    ~Materials() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;

    auto selected_material() const -> std::shared_ptr<erhe::primitive::Material>;

private:
    //void add_material(const std::shared_ptr<erhe::primitive::Material>& material);
    //void materials();

    Scene_root*                                             m_scene_root{nullptr};

    std::vector<std::shared_ptr<erhe::primitive::Material>> m_materials;
    std::vector<const char*>                                m_material_names;
    std::shared_ptr<erhe::primitive::Material>              m_selected_material;
};

} // namespace editor
