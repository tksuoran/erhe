#pragma once

#include "erhe/application/windows/imgui_window.hpp"
#include "erhe/components/components.hpp"

namespace hextiles
{
class Type_editor;

class Terrain_editor_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Terrain_editor_window"};
    static constexpr std::string_view c_title{"Terrain Editor"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Terrain_editor_window ();
    ~Terrain_editor_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;
};

class Terrain_group_editor_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Terrain_group_editor_window"};
    static constexpr std::string_view c_title{"Terrain Group Editor"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Terrain_group_editor_window ();
    ~Terrain_group_editor_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;
};

class Terrain_replacement_rule_editor_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_name {"Terrain_replacement_rule_editor_window"};
    static constexpr std::string_view c_title{"Terrain Replacement Rule Editor"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Terrain_replacement_rule_editor_window ();
    ~Terrain_replacement_rule_editor_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void initialize_component() override;

    // Implements Imgui_window
    void imgui() override;
};


} // namespace hextiles
