#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string_view>

namespace erhe::raytrace {

class IScene;
class Ray;
class Hit;

class IInstance
{
public:
    virtual ~IInstance() noexcept {};

    virtual void commit       () = 0;
    virtual void enable       () = 0;
    virtual void disable      () = 0;
    virtual void set_transform(glm::mat4 transform) = 0;
    virtual void set_scene    (IScene* scene) = 0;
    virtual void set_mask     (uint32_t mask) = 0;
    virtual void set_user_data(void* ptr) = 0;
    [[nodiscard]] virtual auto get_transform() const -> glm::mat4        = 0;
    [[nodiscard]] virtual auto get_scene    () const -> IScene*          = 0;
    [[nodiscard]] virtual auto get_mask     () const -> uint32_t         = 0;
    [[nodiscard]] virtual auto get_user_data() const -> void*            = 0;
    [[nodiscard]] virtual auto is_enabled   () const -> bool             = 0;
    [[nodiscard]] virtual auto debug_label  () const -> std::string_view = 0;

    [[nodiscard]] static auto create       (std::string_view debug_label) -> IInstance*;
    [[nodiscard]] static auto create_shared(std::string_view debug_label) -> std::shared_ptr<IInstance>;
    [[nodiscard]] static auto create_unique(std::string_view debug_label) -> std::unique_ptr<IInstance>;
};

} // namespace erhe::raytrace
