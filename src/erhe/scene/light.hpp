#pragma once

#include "erhe/scene/camera.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"

#include <gsl/gsl>

#include <memory>
#include <string>

namespace erhe::scene
{

enum class Light_type : unsigned int
{
    directional = 0,
    point,
    spot
};

class Light
    : public ICamera
{
public:
    using Type = Light_type;

    static constexpr const char* c_type_strings[] =
    {
        "Directional",
        "Point",
        "Spot"
    };

    explicit Light(const std::string_view name);
    ~Light() override;

    auto node_type() const -> const char* override;

    auto texture_from_world() const -> glm::mat4;
    auto world_from_texture() const -> glm::mat4;

    // Implements ICamera
    void update         (const Viewport viewport)                override;
    auto projection     () -> Projection*                        override;
    auto projection     () const -> const Projection*            override;
    auto clip_from_node () const -> glm::mat4                    override;
    auto clip_from_world() const -> glm::mat4                    override;
    auto node_from_clip () const -> glm::mat4                    override;
    auto world_from_clip() const -> glm::mat4                    override;

    Type      type            {Type::directional};
    glm::vec3 color           {1.0f, 1.0f, 1.0f};
    float     intensity       {1.0f};
    float     range           {100.0f};
    float     inner_spot_angle{glm::pi<float>() * 0.4f};
    float     outer_spot_angle{glm::pi<float>() * 0.5f};
    bool      cast_shadow     {true};

    Projection m_projection;

    class Transforms
    {
    public:
        Transform clip_from_node;
        Transform clip_from_world;
        Transform texture_from_world{glm::mat4{1.0f}, glm::mat4{1.0f}};
    };

    Transforms m_light_transforms;

    static constexpr glm::mat4 texture_from_clip{
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };

    static constexpr glm::mat4 clip_from_texture{
         2.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 2.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,-1.0f, 0.0f, 1.0f
    };
};

auto is_light(const Node* const node) -> bool;
auto is_light(const std::shared_ptr<Node>& node) -> bool;
auto as_light(Node* const node) -> Light*;
auto as_light(const std::shared_ptr<Node>& node) -> std::shared_ptr<Light>;

} // namespace erhe::scene
