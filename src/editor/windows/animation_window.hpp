#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Content_library;
class Scene_root;

class Animation_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Animation_window"};
    static constexpr std::string_view c_title{"Animation"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Animation_window ();
    ~Animation_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements Imgui_window
    void imgui() override;

private:
    float m_start_time{0.0f};
    float m_end_time  {4.0f};
    float m_time      {0.0f};
    float m_speed     {1.0f};
    std::weak_ptr<Content_library> m_content_library;
    std::weak_ptr<Scene_root>      m_scene_root;
};

extern Animation_window* g_animation_window;

} // namespace editor
