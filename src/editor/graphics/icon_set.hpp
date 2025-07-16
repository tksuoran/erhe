#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe        { class Item_base; }
namespace erhe::scene { enum class Light_type : unsigned int; }

namespace editor {

class App_context;
class Icon_settings;

class Icons
{
public:
    const char* anim             {nullptr};
    const char* bone             {nullptr};
    const char* brush_big        {nullptr};
    const char* brush_small      {nullptr};
    const char* brush_tool       {nullptr};
    const char* camera           {nullptr};
    const char* directional_light{nullptr};
    const char* drag             {nullptr};
    const char* file             {nullptr};
    const char* folder           {nullptr};
    const char* grid             {nullptr};
    const char* hud              {nullptr};
    const char* material         {nullptr};
    const char* mesh             {nullptr};
    const char* mouse_lmb        {nullptr};
    const char* mouse_lmb_drag   {nullptr};
    const char* mouse_mmb        {nullptr};
    const char* mouse_mmb_drag   {nullptr};
    const char* mouse_move       {nullptr};
    const char* mouse_rmb        {nullptr};
    const char* mouse_rmb_drag   {nullptr};
    const char* move             {nullptr};
    const char* node             {nullptr};
    const char* physics          {nullptr};
    const char* point_light      {nullptr};
    const char* pull             {nullptr};
    const char* push             {nullptr};
    const char* raytrace         {nullptr};
    const char* rotate           {nullptr};
    const char* scale            {nullptr};
    const char* scene            {nullptr};
    const char* select           {nullptr};
    const char* skin             {nullptr};
    const char* space_mouse      {nullptr};
    const char* space_mouse_lmb  {nullptr};
    const char* space_mouse_rmb  {nullptr};
    const char* spot_light       {nullptr};
    const char* texture          {nullptr};
    const char* three_dots       {nullptr};
    const char* vive             {nullptr};
    const char* vive_menu        {nullptr};
    const char* vive_trackpad    {nullptr};
    const char* vive_trigger     {nullptr};
};

class Icon_set
{
public:
    Icon_set(App_context& context);

    void item_icon(const std::shared_ptr<erhe::Item_base>& item, float scale);
    void draw_icon(const char* code, glm::vec4 color, float size = 0.0f);
    auto icon_button(
        uint32_t    id,
        const char* icon_code,
        float       size            = 0.0f,
        glm::vec4   backround_color = glm::vec4{0.0f},
        glm::vec4   tint_color      = glm::vec4{1.0f}
    ) const -> bool;

    void add_icons(const uint64_t item_type, const float size);

    [[nodiscard]] auto get_icon(const erhe::scene::Light_type type) const -> const char*;

    Icons icons;

    class Type_icon
    {
    public:
        const char*              code{nullptr};
        std::optional<glm::vec4> color{};
    };
    std::vector<std::optional<Type_icon>> type_icons;

private:
    App_context& m_context;
};

}
