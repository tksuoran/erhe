#pragma once

#include "windows/window.hpp"

#include <memory>

namespace editor
{

class Scene_manager;

class Light_properties
    : public Window
{
public:
    explicit Light_properties(const std::shared_ptr<Scene_manager>& scene_manager);

    virtual ~Light_properties() = default;

    void window(Pointer_context& pointer_context) override;

private:
    std::shared_ptr<Scene_manager> m_scene_manager;
    int                            m_layer_index{0};
    int                            m_light_index{0};
};

} // namespace editor
