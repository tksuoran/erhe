#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string_view>

namespace erhe::raytrace
{

class IGeometry;
class IInstance;

class Ray
{
public:
    glm::vec3    origin   {0.0f};
    float        t_near   {0.0f};
    glm::vec3    direction{0.0f};
    float        time     {0.0f};
    float        t_far    {0.0f};
    unsigned int mask     {0};
    unsigned int id       {0};
    unsigned int flags    {0};
};

class Hit
{
public:
    glm::vec3    normal      {0.0f};
    glm::vec2    uv          {0.0f};
    unsigned int primitive_id{0};
    IGeometry*   geometry    {nullptr};
    IInstance*   instance    {nullptr};
};

class IScene
{
public:
    virtual ~IScene(){};

    virtual void attach   (IGeometry* geometry) = 0;
    virtual void attach   (IInstance* instance) = 0;
    virtual void detach   (IGeometry* geometry) = 0;
    virtual void detach   (IInstance* instance) = 0;
    virtual void commit   () = 0;
    virtual void intersect(Ray& ray, Hit& hit) = 0;
    virtual [[nodiscard]] auto debug_label() const -> std::string_view = 0;

    static [[nodiscard]] auto create       (const std::string_view debug_label) -> IScene*;
    static [[nodiscard]] auto create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>;
    static [[nodiscard]] auto create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>;
};

} // namespace erhe::raytrace
