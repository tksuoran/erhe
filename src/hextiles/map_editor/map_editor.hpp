#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "map.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace hextiles
{

class Map_hover_command final
    : public erhe::application::Command
{
public:
    explicit Map_hover_command()
        : Command{"Map_editor.hover"}
    {
    }
    ~Map_hover_command() noexcept final {}

    auto try_call(erhe::application::Input_arguments& input) -> bool override;
};

class Map_primary_brush_command final
    : public erhe::application::Command
{
public:
    explicit Map_primary_brush_command()
        : Command{"Map_editor.primary_brush"}
    {
    }
    ~Map_primary_brush_command() noexcept final {}

    auto try_call (erhe::application::Input_arguments& input) -> bool override;
    void try_ready(erhe::application::Input_arguments& input) override;
};

class Map_editor
    : public erhe::components::Component
{
public:
    static constexpr const char* c_title{"Map Editor"};
    static constexpr const char* c_type_name{"Map_editor"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Map_editor();
    ~Map_editor() noexcept;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component() override;

    // Public API
    void render         ();
    void terrain_palette();
    [[nodiscard]] auto get_map() -> Map*;

    // Commands
    void hover        (glm::vec2 window_position);
    void primary_brush(glm::vec2 mouse_position);

private:
    // Commands
    Map*                           m_map{nullptr};;
    Map_hover_command              m_map_hover_command;
    Map_primary_brush_command      m_map_primary_brush_command;
    int                            m_brush_size{1};
    terrain_tile_t                 m_left_brush{Terrain_default};
    std::optional<glm::vec2>       m_hover_window_position;
    std::optional<Tile_coordinate> m_hover_tile_position;
};

extern Map_editor* g_map_editor;

} // namespace hextiles
