#pragma once

#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <filesystem>

namespace lunasvg
{
    class Document;
}

namespace erhe::application
{
    class Imgui_renderer;
}

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Node;
    enum class Light_type : unsigned int;
}

namespace editor {

class Icon_set
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Icon_set"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Icon_set();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    [[nodiscard]] auto load    (const std::filesystem::path& path) -> glm::vec2;
    [[nodiscard]] auto get_icon(const erhe::scene::Light_type type) const -> const glm::vec2;

    class Rasterization
    {
    public:
        Rasterization();
        Rasterization(Icon_set& icon_set, int size, int column_count, int row_count);

        void post_initialize(Icon_set& icon_set);
        void rasterize(
            lunasvg::Document& document,
            int                column,
            int                row
        );

        void icon(
            glm::vec2 uv0,
            glm::vec4 tint_color = glm::vec4{1.0f}
        ) const;

        auto icon_button(
            glm::vec2 uv0,
            int       frame_padding,
            glm::vec4 backround_color = glm::vec4{0.0f},
            glm::vec4 tint_color = glm::vec4{1.0f},
            bool      linear = false
        ) const -> bool;

    private:
        [[nodiscard]] auto uv1(const glm::vec2& uv0) const -> glm::vec2;

        std::shared_ptr<erhe::application::Imgui_renderer> m_imgui_renderer;
        std::shared_ptr<erhe::graphics::Texture>           m_texture;
        uint64_t                                           m_texture_handle{0};
        int                                                m_icon_width    {0};
        int                                                m_icon_height   {0};
        float                                              m_icon_uv_width {0.0f};
        float                                              m_icon_uv_height{0.0f};
    };


    class Icons
    {
    public:
        glm::vec2 camera           {};
        glm::vec2 directional_light{};
        glm::vec2 drag             {};
        glm::vec2 point_light      {};
        glm::vec2 pull             {};
        glm::vec2 push             {};
        glm::vec2 spot_light       {};
        glm::vec2 mesh             {};
        glm::vec2 move             {};
        glm::vec2 node             {};
        glm::vec2 rotate           {};
        glm::vec2 three_dots       {};
    };

    Icons icons;

    [[nodiscard]] auto get_small_rasterization() const -> const Rasterization&;
    [[nodiscard]] auto get_large_rasterization() const -> const Rasterization&;

private:
    Rasterization m_small;
    Rasterization m_large;
    int           m_row_count   {0};
    int           m_column_count{0};
    int           m_row         {0};
    int           m_column      {0};
};

}
