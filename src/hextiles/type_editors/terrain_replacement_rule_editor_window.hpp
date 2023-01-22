#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

namespace hextiles
{

class Terrain_replacement_rule_editor_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr const char* c_type_name{"Terrain_replacement_rule_editor_window"};
    static constexpr const char* c_title{"Terrain Replacement Rule Editor"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Terrain_replacement_rule_editor_window ();
    ~Terrain_replacement_rule_editor_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;
};

extern Terrain_replacement_rule_editor_window* g_terrain_replacement_rule_editor_window;

} // namespace hextiles
