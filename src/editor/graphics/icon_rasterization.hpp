#pragma once

#include "erhe_graphics/sampler.hpp"

#include <glm/glm.hpp>

#if defined(ERHE_SVG_LIBRARY_PLUTOSVG)
#   include <plutosvg.h>
#   include <plutovg.h>
#endif

#include <cstdint>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Device;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_renderer;
}
namespace tf {
    class Executor;
};

namespace editor {

class App_context;
class Icon_settings;
class Programs;

class Icon_load_data
{
public:
    Icon_load_data(Icon_settings& icon_settings, const char* icon_name, int column, int row, glm::vec2 uv0);
    ~Icon_load_data();

    void rasterize(int size);
    void upload   (int size, erhe::graphics::Texture& texture);

private:
    plutosvg_document_t*            m_document{nullptr};
    int                             m_column;
    int                             m_row;
    glm::vec2                       m_uv0;
    std::vector<plutovg_surface_t*> m_bitmaps;
};

class Icon_settings;
class Icon_rasterization;

class Icon_loader
{
public:
    Icon_loader(Icon_settings& icon_settings);

    void queue_icon_load            (glm::vec2& uv, const char* icon_name);
    void execute_rasterization_queue();
    void upload_to_texture          (Icon_rasterization& icon_rasterization);
    void clear_load_queue           ();

private:
    Icon_settings&                               m_icon_settings;
    std::vector<std::unique_ptr<Icon_load_data>> m_icons_to_load;
    bool                                         m_rasterization_queue_executed{false};
    int                                          m_row     {0};
    int                                          m_column  {1};
};

class Icon_rasterization
{
public:
    Icon_rasterization(App_context& context, erhe::graphics::Device& graphics_device, int size);

    [[nodiscard]] auto get_size() const -> int;

    void icon(glm::vec2 uv0, glm::vec4 background_color = glm::vec4{0.0f}, glm::vec4 tint_color = glm::vec4{1.0f}) const;
    auto icon_button(
        uint32_t                            id,
        glm::vec2                           uv0,
        glm::vec4                           backround_color = glm::vec4{0.0f},
        glm::vec4                           tint_color      = glm::vec4{1.0f},
        erhe::graphics::Filter              filter          = erhe::graphics::Filter::nearest,
        erhe::graphics::Sampler_mipmap_mode mipmap_mode     = erhe::graphics::Sampler_mipmap_mode::not_mipmapped
    ) const -> bool;
    auto get_texture() -> std::shared_ptr<erhe::graphics::Texture> { return m_texture; }

private:
    [[nodiscard]] auto uv1(const glm::vec2& uv0) const -> glm::vec2;

    App_context&                             m_context;
    std::shared_ptr<erhe::graphics::Texture> m_texture;
    erhe::graphics::Sampler                  m_linear_sampler;
    uint64_t                                 m_texture_handle{0};
    int                                      m_icon_width    {0};
    int                                      m_icon_height   {0};
    float                                    m_icon_uv_width {0.0f};
    float                                    m_icon_uv_height{0.0f};
};

}
