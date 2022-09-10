#pragma once

#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <filesystem>

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

    [[nodiscard]] auto load    (const std::filesystem::path& path) -> glm::vec2;
    [[nodiscard]] auto uv1     (const glm::vec2& uv0) const -> glm::vec2;
    [[nodiscard]] auto get_icon(const erhe::scene::Light_type type) const -> const glm::vec2;
    void               icon    (const glm::vec2 uv0, const glm::vec4 tint_color = glm::vec4{1.0f}) const;

    void icon(const erhe::scene::Camera& camera) const;
    void icon(const erhe::scene::Light&  light) const;
    void icon(const erhe::scene::Mesh&   mesh) const;
    void icon(const erhe::scene::Node&   node) const;

    struct Icons
    {
        glm::vec2 camera           {};
        glm::vec2 directional_light{};
        glm::vec2 point_light      {};
        glm::vec2 spot_light       {};
        glm::vec2 mesh             {};
        glm::vec2 node             {};
        glm::vec2 three_dots       {};
    };

    Icons icons;

private:
    std::shared_ptr<erhe::graphics::Texture> m_texture;
    uint64_t                                 m_texture_handle{0};
    int                                      m_icon_width    {0};
    int                                      m_icon_height   {0};
    int                                      m_row_count     {0};
    int                                      m_column_count  {0};
    int                                      m_row           {0};
    int                                      m_column        {0};
    float                                    m_icon_uv_width {0.0f};
    float                                    m_icon_uv_height{0.0f};
};

}
