#pragma once

#include <memory>

namespace erhe::scene
{

class Node;
class INode_attachment;
class Scene;

class Scene_host
{
public:
    virtual ~Scene_host() noexcept;

    [[nodiscard]] virtual auto get_scene() -> Scene* = 0;
};

} // namespace erhe::scene
