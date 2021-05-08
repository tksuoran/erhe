#pragma once

#include "erhe/components/component.hpp"
#include <memory>

namespace editor
{

struct Pointer_context;
class Scene_manager;

class Window
{
public:
    virtual void window(Pointer_context& pointer_context) = 0;
};

} // namespace editor
