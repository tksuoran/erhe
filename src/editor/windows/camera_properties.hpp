#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_manager;

class Camera_properties
    : public erhe::components::Component
    , public Window
{
public:
    static constexpr const char* c_name = "Camera_properties";
    Camera_properties() : erhe::components::Component(c_name) {}
    virtual ~Camera_properties() = default;

    // Implements Component
    void connect() override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager> m_scene_manager;
};

} // namespace editor
