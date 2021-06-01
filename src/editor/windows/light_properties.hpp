#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_root;

class Light_properties
    : public erhe::components::Component
    , public Window
{
public:
    static constexpr const char* c_name = "Light_properties";
    Light_properties() : erhe::components::Component(c_name) {}
    virtual ~Light_properties() = default;

    // Implements Component
    void connect() override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    auto animation() const -> bool;

private:
    std::shared_ptr<Scene_root> m_scene_root;
    int                         m_layer_index{0};
    int                         m_light_index{0};
    bool                        m_animation{true};
};

} // namespace editor
