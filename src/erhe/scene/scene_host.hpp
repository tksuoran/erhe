#pragma once

#include <memory>

namespace erhe::scene
{

class Scene;

class Scene_host
{
public:
    virtual ~Scene_host() noexcept;

    [[nodiscard]] virtual auto get_hosted_scene() -> Scene* = 0;
};

} // namespace erhe::scene
