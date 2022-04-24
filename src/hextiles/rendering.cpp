#include "rendering.hpp"
#include "map_renderer.hpp"
#include "tiles.hpp"
#include "tile_shape.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"

namespace hextiles {

Rendering::Rendering()
    : erhe::components::Component{c_label}
{
}

Rendering::~Rendering()
{
}

void Rendering::connect()
{
    m_imgui_renderer = get<erhe::application::Imgui_renderer>();
    m_map_renderer   = get<Map_renderer>();
    m_tiles          = get<Tiles       >();
}

auto Rendering::terrain_image(terrain_t terrain, const int scale) -> bool
{
    const auto&     texel           = m_tiles->get_terrain_shape(terrain);
    const auto&     tileset_texture = m_map_renderer->tileset_texture();
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(Tile_shape::height) / static_cast<float>(tileset_texture->height()),
    };

    return m_imgui_renderer->image(
        tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

auto Rendering::unit_image(unit_t unit, const int scale) -> bool
{
    const auto&     texel           = m_tiles->get_unit_shape(unit);
    const auto&     tileset_texture = m_map_renderer->tileset_texture();
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(Tile_shape::height) / static_cast<float>(tileset_texture->height()),
    };

    return m_imgui_renderer->image(
        tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

void Rendering::make_terrain_type_combo(const char* label, terrain_t& value)
{
    auto&       preview_terrain = m_tiles->get_terrain_type(value);
    const char* preview_value   = preview_terrain.name.c_str();

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge))
    {
        const terrain_t end = static_cast<unit_t>(m_tiles->get_terrain_type_count());
        for (terrain_t i = 0; i < end; i++)
        {
            auto&      unit = m_tiles->get_terrain_type(i);
            const auto id   = fmt::format("##{}-{}", label, i);
            ImGui::PushID(id.c_str());
            bool is_selected = (value == i);
            if (terrain_image(i, 1))
            {
                value = i;
            }
            ImGui::SameLine();
            if (ImGui::Selectable(unit.name.c_str(), is_selected))
            {
                value = i;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

void Rendering::make_unit_type_combo(const char* label, unit_t& value)
{
    auto&       preview_unit  = m_tiles->get_unit_type(value);
    const char* preview_value = preview_unit.name.c_str();

    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::BeginCombo(label, preview_value, ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge))
    {
        const unit_t end = static_cast<unit_t>(m_tiles->get_unit_type_count());
        for (unit_t i = 0; i < end; i++)
        {
            auto&      unit = m_tiles->get_unit_type(i);
            const auto id   = fmt::format("##{}-{}", label, i);
            ImGui::PushID(id.c_str());
            bool is_selected = (value == i);
            if (unit_image(i, 1))
            {
                value = i;
            }
            ImGui::SameLine();
            if (ImGui::Selectable(unit.name.c_str(), is_selected))
            {
                value = i;
            }

            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

}  // namespace editor
