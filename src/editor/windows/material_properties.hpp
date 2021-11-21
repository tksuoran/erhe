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

class Material_properties
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Material_properties"};
    static constexpr std::string_view c_title{"Material"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Material_properties ();
    ~Material_properties() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui(Pointer_context& pointer_context) override;

private:
    void materials();

    Scene_root* m_scene_root{nullptr};
    erhe::primitive::Material*  m_selected_material{nullptr};
};

} // namespace editor
