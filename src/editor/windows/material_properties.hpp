#pragma once

#include "erhe/application/windows/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include <memory>

namespace erhe::primitive
{
    class Material;
}

namespace editor
{

class Materials;
class Scene_root;
class Selection_tool;

class Material_properties
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_label{"Material_properties"};
    static constexpr std::string_view c_title{"Material"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Material_properties ();
    ~Material_properties() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Materials> m_materials;
};

} // namespace editor
