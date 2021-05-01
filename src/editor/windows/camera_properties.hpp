#pragma once

#include "windows/window.hpp"

#include <memory>

namespace sample
{

class Scene_manager;

class Camera_properties
    : public Window
{
public:
    explicit Camera_properties(const std::shared_ptr<Scene_manager>& scene_manager);

    virtual ~Camera_properties() = default;

    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager> m_scene_manager;
};

} // namespace sample
