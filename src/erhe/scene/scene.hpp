#pragma once

#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/scene_message.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::scene
{

class Camera;
class Light;
class Mesh;
class Node;
class Scene;

class Mesh_layer
{
public:
    Mesh_layer(
        const std::string_view name,
        uint64_t               flags
    );

    [[nodiscard]] auto get_mesh_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Mesh>;

    void add   (const std::shared_ptr<Mesh>& mesh);
    void remove(const std::shared_ptr<Mesh>& mesh);

    std::vector<std::shared_ptr<Mesh>>   meshes;
    std::string                          name;
    uint64_t                             flags{0};
    erhe::toolkit::Unique_id<Mesh_layer> id;
};

class Light_layer
{
public:
    explicit Light_layer(const std::string_view name);

    [[nodiscard]] auto get_light_by_id(
        const erhe::toolkit::Unique_id<Node>::id_type id
    ) const -> std::shared_ptr<Light>;

    void add   (const std::shared_ptr<Light>& light);
    void remove(const std::shared_ptr<Light>& light);

    std::vector<std::shared_ptr<Light>>   lights;
    glm::vec4                             ambient_light{0.0f, 0.0f, 0.0f, 0.0f};
    std::string                           name;
    erhe::toolkit::Unique_id<Light_layer> id;
};

class Scene_host;

class Scene
    : public erhe::message_bus::Message_bus_node<Scene_message>
{
public:
    explicit Scene(
        erhe::message_bus::Message_bus<Scene_message>* message_bus,
        Scene_host*                                    host = nullptr
    );

    ~Scene() override;

    void sanity_check          () const;
    void sort_transform_nodes  ();
    void update_node_transforms();

    //[[nodiscard]] auto get_node_by_id         (const erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Node>;
    [[nodiscard]] auto get_mesh_by_id       (erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_light_by_id      (erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_camera_by_id     (erhe::toolkit::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>;
    [[nodiscard]] auto get_mesh_layer_by_id (erhe::toolkit::Unique_id<Mesh_layer>::id_type id) const -> std::shared_ptr<Mesh_layer>;
    [[nodiscard]] auto get_light_layer_by_id(erhe::toolkit::Unique_id<Light_layer>::id_type id) const -> std::shared_ptr<Light_layer>;

    void register_node    (const std::shared_ptr<Node>& node);
    void unregister_node  (const std::shared_ptr<Node>& node);
    void register_camera  (const std::shared_ptr<Camera>& camera);
    void unregister_camera(const std::shared_ptr<Camera>& camera);
    void register_mesh    (const std::shared_ptr<Mesh>& mesh);
    void unregister_mesh  (const std::shared_ptr<Mesh>& mesh);
    void register_light   (const std::shared_ptr<Light>& light);
    void unregister_light (const std::shared_ptr<Light>& light);

    Scene_host*                               host{nullptr};
    std::shared_ptr<erhe::scene::Node>        root_node;
    std::vector<std::shared_ptr<Node>>        flat_node_vector;
    std::vector<std::shared_ptr<Mesh_layer>>  mesh_layers;
    std::vector<std::shared_ptr<Light_layer>> light_layers;
    std::vector<std::shared_ptr<Camera>>      cameras;

    bool nodes_sorted{false};
};

} // namespace erhe::scene
