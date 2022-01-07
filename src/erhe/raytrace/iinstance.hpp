#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string_view>

namespace erhe::raytrace
{

class IScene;

class IInstance
{
public:
    virtual ~IInstance(){};

    virtual void commit       () = 0;
    virtual void set_transform(const glm::mat4 transform) = 0;
    virtual void set_scene    (IScene* scene) = 0;
    virtual void set_user_data(void* ptr) = 0;
    [[nodiscard]] virtual auto get_user_data() -> void* = 0;
    [[nodiscard]] virtual auto debug_label() const -> std::string_view = 0;

    [[nodiscard]] static auto create       (const std::string_view debug_label) -> IInstance*;
    [[nodiscard]] static auto create_shared(const std::string_view debug_label) -> std::shared_ptr<IInstance>;
    [[nodiscard]] static auto create_unique(const std::string_view debug_label) -> std::unique_ptr<IInstance>;
};

} // namespace erhe::raytrace
