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

class Light
    : public ICamera
{
public:
    enum class Type : unsigned int
    {
        directional = 0,
        point,
        spot
    };

    static constexpr const char* c_type_strings[] =
    {
        "Directional",
        "Point",
        "Spot"
    };

    Light(const std::string&           name,
          const std::shared_ptr<Node>& node)
        : m_name(name)
        , m_node(node)
    {
    }

    virtual ~Light() = default;

    Type       type            {Type::directional};
    glm::vec3  color           {glm::vec3(1.0f, 1.0f, 1.0f)};
    float      intensity       {1.0f};
    float      range           {100.0f};
    float      inner_spot_angle{glm::pi<float>() * 0.4f};
    float      outer_spot_angle{glm::pi<float>() * 0.5f};
    bool       cast_shadow     {true};

    void update(Viewport viewport) override;

    auto name() const -> const std::string&
    {
        return m_name;
    }

    auto node() const -> const std::shared_ptr<Node>& override
    {
        return m_node;
    }

    auto projection() -> Projection* override
    {
        return &m_projection;
    }

    auto projection() const -> const Projection* override
    {
        return &m_projection;
    }

    auto clip_from_node() const -> glm::mat4 override
    {
        return m_transforms.clip_from_node.matrix();
    }

    auto clip_from_world() const -> glm::mat4 override
    {
        return m_transforms.clip_from_world.matrix();
    }

    auto node_from_clip() const -> glm::mat4 override
    {
        return m_transforms.clip_from_node.inverse_matrix();
    }

    auto world_from_clip() const -> glm::mat4 override
    {
        return m_transforms.clip_from_world.inverse_matrix();
    }

    auto texture_from_world() const -> glm::mat4
    {
        return m_transforms.texture_from_world.matrix();
    }

    auto world_from_texture() const -> glm::mat4
    {
        return m_transforms.texture_from_world.inverse_matrix();
    }


    std::string           m_name;
    std::shared_ptr<Node> m_node;
    Projection            m_projection;

    struct Transforms
    {
        Transform clip_from_node;
        Transform clip_from_world;
        Transform texture_from_world{glm::mat4(1.0f), glm::mat4(1.0f)};
    };

    Transforms m_transforms;

    static constexpr glm::mat4 texture_from_clip{0.5f, 0.0f, 0.0f, 0.0f,
                                                 0.0f, 0.5f, 0.0f, 0.0f,
                                                 0.0f, 0.0f, 1.0f, 0.0f,
                                                 0.5f, 0.5f, 0.0f, 1.0f};

    static constexpr glm::mat4 clip_from_texture{ 2.0f, 0.0f, 0.0f, 0.0f,
                                                  0.0f, 2.0f, 0.0f, 0.0f,
                                                  0.0f, 0.0f, 1.0f, 0.0f,
                                                 -1.0f,-1.0f, 0.0f, 1.0f};
};

} // namespace erhe::scene
