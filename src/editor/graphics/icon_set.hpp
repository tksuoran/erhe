#pragma once

#include "graphics/icon_rasterization.hpp"

#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <filesystem>

namespace lunasvg
{
    class Document;
}

namespace erhe::graphics
{
    class Texture;
}

namespace erhe::scene
{
    enum class Light_type : unsigned int;
}

namespace editor {

class Icon_set;
class Icon_rasterization;
class Icons;


class Icons
{
public:
    glm::vec2 brush_small      {}; // for vertex paint
    glm::vec2 brush_big        {}; // for brush tool
    glm::vec2 camera           {};
    glm::vec2 directional_light{};
    glm::vec2 drag             {};
    glm::vec2 grid             {};
    glm::vec2 hud              {};
    glm::vec2 material         {};
    glm::vec2 mesh             {};
    glm::vec2 mouse_lmb        {};
    glm::vec2 mouse_lmb_drag   {};
    glm::vec2 mouse_mmb        {};
    glm::vec2 mouse_mmb_drag   {};
    glm::vec2 mouse_move       {};
    glm::vec2 mouse_rmb        {};
    glm::vec2 mouse_rmb_drag   {};
    glm::vec2 move             {};
    glm::vec2 node             {};
    glm::vec2 point_light      {};
    glm::vec2 pull             {};
    glm::vec2 push             {};
    glm::vec2 rotate           {};
    glm::vec2 scene            {};
    glm::vec2 select           {};
    glm::vec2 space_mouse      {};
    glm::vec2 space_mouse_lmb  {};
    glm::vec2 space_mouse_rmb  {};
    glm::vec2 spot_light       {};
    glm::vec2 three_dots       {};
    glm::vec2 vive             {};
    glm::vec2 vive_menu        {};
    glm::vec2 vive_trackpad    {};
    glm::vec2 vive_trigger     {};
};

class Icon_set
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Icon_set"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Icon_set();
    ~Icon_set();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    [[nodiscard]] auto load    (const std::filesystem::path& path) -> glm::vec2;
    [[nodiscard]] auto get_icon(const erhe::scene::Light_type type) const -> const glm::vec2;

    Icons icons;

    [[nodiscard]] auto get_small_rasterization () const -> const Icon_rasterization&;
    [[nodiscard]] auto get_large_rasterization () const -> const Icon_rasterization&;
    [[nodiscard]] auto get_hotbar_rasterization() const -> const Icon_rasterization&;

private:
    Icon_rasterization m_small;
    Icon_rasterization m_large;
    Icon_rasterization m_hotbar;
    int                m_row_count   {0};
    int                m_column_count{0};
    int                m_row         {0};
    int                m_column      {0};
};

extern Icon_set* g_icon_set;

}
