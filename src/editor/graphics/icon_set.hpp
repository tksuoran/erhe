#pragma once

#include "erhe/components/component.hpp"
#include <filesystem>
#include "imgui.h"

#include <glm/glm.hpp>

namespace erhe::graphics
{
    class Texture;
}

namespace editor {

class Icon_set
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Icon_set"};

    Icon_set(int icon_width = 16, int icon_height = 16, int row_count = 16, int column_count = 16);
    ~Icon_set() override;

    // Implements Component
    void connect()              override;
    void initialize_component() override;

    auto load(const std::filesystem::path& path) -> ImVec2;
    auto uv1 (const ImVec2& uv0) const -> ImVec2;
    void icon(ImVec2 uv0, glm::vec4 tint_color = glm::vec4{1.0f}) const;

    std::shared_ptr<erhe::graphics::Texture> texture;

    struct Icons
    {
        ImVec2 camera{};
        ImVec2 directional_light{};
        ImVec2 point_light{};
        ImVec2 spot_light{};
        ImVec2 mesh{};
        ImVec2 node{};
    };

    Icons icons;

private:
    int   m_icon_width{0};
    int   m_icon_height{0};
    int   m_row_count{0};
    int   m_column_count{0};
    int   m_row{0};
    int   m_column{0};
    float m_icon_uv_width{0.0f};
    float m_icon_uv_height{0.0f};
};

}
