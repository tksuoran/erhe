#pragma once

#include <memory>
#include <string_view>

namespace erhe::raytrace
{

class IGeometry;
class IInstance;
class Ray;
class Hit;

class IScene
{
public:
    virtual ~IScene() noexcept {};

    virtual void attach   (IGeometry* geometry) = 0;
    virtual void attach   (IInstance* instance) = 0;
    virtual void detach   (IGeometry* geometry) = 0;
    virtual void detach   (IInstance* instance) = 0;
    virtual void commit   () = 0;
    virtual void intersect(Ray& ray, Hit& hit) = 0;
    [[nodiscard]] virtual auto debug_label() const -> std::string_view = 0;

    [[nodiscard]] static auto create       (const std::string_view debug_label) -> IScene*;
    [[nodiscard]] static auto create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>;
    [[nodiscard]] static auto create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>;
};

} // namespace erhe::raytrace
