#pragma once

#include "erhe/components/component.hpp"
#include "erhe/physics/idebug_draw.hpp"

namespace editor
{

class Line_renderer;
class Text_renderer;

class Debug_draw
    : public erhe::components::Component
    , public erhe::physics::IDebug_draw
{
public:
    static constexpr std::string_view c_name{"Debug_draw"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Debug_draw();
    ~Debug_draw() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implemnents IDebug_draw
	auto get_colors          () const -> Colors                                                override;
	void set_colors          (const Colors& colors)                                            override;
    void draw_line           (const glm::vec3 from, const glm::vec3 to, const glm::vec3 color) override;
    void draw_3d_text        (const glm::vec3 location, const char* text)                      override;
    void set_debug_mode      (int debug_mode)                                                  override;
    auto get_debug_mode      () const -> int                                                   override;
    void draw_contact_point(
        const glm::vec3 point,
        const glm::vec3 normal,
        float           distance,
        int             life_time,
        const glm::vec3 color
    )                                                                                          override;
    void report_error_warning(const char* warning)                                             override;

    float line_width{4.0f};

private:
    // Component dependencies
    std::shared_ptr<Line_renderer> m_line_renderer;
    std::shared_ptr<Text_renderer> m_text_renderer;

    int            m_debug_mode{0};
    Colors         m_colors;
};

} // namespace editor
