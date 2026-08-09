#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/unique_id.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace erhe::scene {

class Camera;
class Light;
class Mesh;
class Node;
class Scene;
class Scene_host;
class Skin;

using Layer_id = uint64_t;

class Mesh_layer
{
public:
    Mesh_layer(std::string_view name, uint64_t flags, Layer_id id);

    // Public API
    [[nodiscard]] auto get_mesh_by_id(erhe::Unique_id<Node>::id_type mesh_id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_name() const -> const std::string&;

    void add   (const std::shared_ptr<Mesh>& mesh);
    void remove(const std::shared_ptr<Mesh>& mesh);

    std::vector<std::shared_ptr<Mesh>> meshes;
    std::string                        name;
    uint64_t                           flags{0};
    Layer_id                           id;
};

class Light_layer
{
public:
    Light_layer(std::string_view name, Layer_id id);

    [[nodiscard]] auto get_light_by_id(std::size_t id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_name       ()               const -> const std::string&;

    void add   (const std::shared_ptr<Light>& light);
    void remove(const std::shared_ptr<Light>& light);

    std::vector<std::shared_ptr<Light>> lights;
    std::string                         name;
    Layer_id                            id;
};

class Scene : public erhe::Item<erhe::Item_base, erhe::Item_base, Scene>
{
public:
    Scene(std::string_view name, Scene_host* host = nullptr);
    explicit Scene(const Scene& src);
    Scene& operator=(const Scene& src);
    ~Scene() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Scene"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::scene; }

    // Overrrides Item_base
    auto get_item_host() const -> erhe::Item_host* override;

    // Public API
    void sanity_check          () const;
    // Detaches all scene content from the host and its owned resources.
    // Called by the owning Scene_host (Scene_root) from its destructor,
    // because the Scene and/or its content may be co-owned elsewhere
    // (selection, undo stack, clipboard, ...) and thus outlive the host.
    // recursive_remove() runs the normal orphan path so meshes leave the
    // raytrace scene and rigid bodies leave the physics world while those are
    // still alive; the host back-pointers (Scene::m_host, node_data.host) are
    // then cleared. Without this, a Scene / Mesh / Node_physics torn down after
    // its host dereferences the freed Scene_host, raytrace scene, or physics
    // world (see node_sanity_check, Mesh::detach_rt_from_scene).
    void sever_host            ();
    void update_node_transforms();

    // Per-pass cost of update_node_transforms(), accumulated across passes
    // until sampled. Passes with an empty dirty list record nothing, so the
    // steady-state overhead is a single clock read per pass.
    class Transform_update_stats
    {
    public:
        void reset()
        {
            *this = Transform_update_stats{};
        }
        void add(const Transform_update_stats& other)
        {
            pass_count    += other.pass_count;
            dirty_count   += other.dirty_count;
            visited_count += other.visited_count;
            lock_wait_ms  += other.lock_wait_ms;
            sort_ms       += other.sort_ms;
            propagate_ms  += other.propagate_ms;
        }
        [[nodiscard]] auto total_ms() const -> double
        {
            return lock_wait_ms + sort_ms + propagate_ms;
        }

        std::size_t pass_count   {0}; // update_node_transforms() passes that had work
        std::size_t dirty_count  {0}; // dirty-list entries swapped in and sorted
        std::size_t visited_count{0}; // unique nodes recorded in the visited set (dirty roots + subtree descendants updated)
        double      lock_wait_ms {0.0};
        double      sort_ms     {0.0};
        double      propagate_ms{0.0};
    };

    // Returns the stats accumulated since the previous sample and resets the
    // accumulator. Main thread only (the same thread that runs the passes).
    [[nodiscard]] auto sample_transform_update_stats() -> Transform_update_stats
    {
        const Transform_update_stats result = m_transform_update_stats;
        m_transform_update_stats.reset();
        return result;
    }

    // Queues the node's subtree for world-transform propagation in the next
    // update_node_transforms() pass. Called by Node::handle_transform_update()
    // (every transform setter and reparent funnels through it), so any writer
    // - animation, physics, tools, undo, import - dirties the scene without
    // its own integration. The node's OWN world transform is already up to
    // date when this is called (setters update it eagerly); only descendants
    // need the pass. Callers hold the Item_host mutex per the same discipline
    // as all other hosted-item mutation.
    void mark_node_transform_dirty(const Node& node);

    // RAII bracket for transform writes made by the OWNER of
    // no_transform_update nodes' transforms (the editor's physics writeback).
    // Dirt recorded while a scope is alive keeps the propagation skip over
    // no_transform_update children; dirt from any other writer (tools, MCP,
    // undo, animation) carries them, so editing an ancestor moves its
    // body-driven subtree along. Scopes do not nest.
    class Transform_owner_writes_scope final
    {
    public:
        explicit Transform_owner_writes_scope(Scene& scene)
            : m_scene{scene}
        {
            m_scene.m_transform_owner_writes = true;
        }
        ~Transform_owner_writes_scope() noexcept
        {
            m_scene.m_transform_owner_writes = false;
        }
        Transform_owner_writes_scope(const Transform_owner_writes_scope&)            = delete;
        Transform_owner_writes_scope& operator=(const Transform_owner_writes_scope&) = delete;

    private:
        Scene& m_scene;
    };

    [[nodiscard]] auto get_mesh_by_id       (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>;
    [[nodiscard]] auto get_light_by_id      (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>;
    [[nodiscard]] auto get_camera_by_id     (erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>;
    [[nodiscard]] auto get_mesh_layer_by_id (Layer_id id) const -> std::shared_ptr<Mesh_layer>;
    [[nodiscard]] auto get_light_layer_by_id(Layer_id id) const -> std::shared_ptr<Light_layer>;
    [[nodiscard]] auto get_root_node        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto get_cameras          () -> std::vector<std::shared_ptr<Camera>>&;
    [[nodiscard]] auto get_cameras          () const -> const std::vector<std::shared_ptr<Camera>>&;
    [[nodiscard]] auto get_skins            () -> std::vector<std::shared_ptr<Skin>>&;
    [[nodiscard]] auto get_skins            () const -> const std::vector<std::shared_ptr<Skin>>&;
    // Registered nodes are kept in two buckets keyed by the
    // Item_flags::no_transform_update flag so that update_node_transforms()
    // can skip the flagged bucket entirely. Nodes migrate between buckets
    // when the flag changes (Node::handle_flag_bits_update).
    [[nodiscard]] auto get_transform_update_nodes   () const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto get_no_transform_update_nodes() const -> const std::vector<std::shared_ptr<Node>>&;
    [[nodiscard]] auto get_node_count               () const -> std::size_t;

    // Visits every registered node (both buckets, no particular order).
    // The callback returns false to stop the iteration.
    template <typename Fn>
    void for_each_node(Fn&& fn) const
    {
        for (const std::shared_ptr<Node>& node : m_transform_update_nodes) {
            if (!fn(node)) {
                return;
            }
        }
        for (const std::shared_ptr<Node>& node : m_no_transform_update_nodes) {
            if (!fn(node)) {
                return;
            }
        }
    }
    [[nodiscard]] auto get_mesh_layers      () -> std::vector<std::shared_ptr<Mesh_layer>>&;
    [[nodiscard]] auto get_mesh_layers      () const -> const std::vector<std::shared_ptr<Mesh_layer>>&;
    [[nodiscard]] auto get_light_layers     () -> std::vector<std::shared_ptr<Light_layer>>&;
    [[nodiscard]] auto get_light_layers     () const -> const std::vector<std::shared_ptr<Light_layer>>&;

    void add_mesh_layer (const std::shared_ptr<Mesh_layer>& mesh_layer);
    void add_light_layer(const std::shared_ptr<Light_layer>& light_layer);

    void register_node    (const std::shared_ptr<Node>& node);
    void unregister_node  (const std::shared_ptr<Node>& node);
    // Called by Node::handle_flag_bits_update() when the
    // Item_flags::no_transform_update bit changes on a hosted node;
    // moves the node to the bucket matching the new flag value.
    void handle_node_no_transform_update_changed(Node& node);
    void register_camera  (const std::shared_ptr<Camera>& camera);
    void unregister_camera(const std::shared_ptr<Camera>& camera);
    void register_mesh    (const std::shared_ptr<Mesh>& mesh);
    void unregister_mesh  (const std::shared_ptr<Mesh>& mesh);
    void register_skin    (const std::shared_ptr<Skin>& skin);
    void unregister_skin  (const std::shared_ptr<Skin>& skin);
    void register_light   (const std::shared_ptr<Light>& light);
    void unregister_light (const std::shared_ptr<Light>& light);

    // Scene-wide ambient light color (issues #237 / #240). Fed to the forward
    // renderer as the ambient term and serialized with the scene (scene file
    // v5). Moved here from Light_layer so it is an intrinsic scene property.
    glm::vec4 ambient_light{0.0f, 0.0f, 0.0f, 0.0f};

private:
    void update_subtree_transforms(Node& node, bool carry_body_driven);

    Scene_host*                               m_host       {nullptr};
    std::shared_ptr<erhe::scene::Node>        m_root_node;
    std::vector<std::shared_ptr<Node>>        m_transform_update_nodes;
    std::vector<std::shared_ptr<Node>>        m_no_transform_update_nodes;
    std::vector<std::shared_ptr<Mesh_layer>>  m_mesh_layers;
    std::vector<std::shared_ptr<Skin>>        m_skins;
    std::vector<std::shared_ptr<Light_layer>> m_light_layers;
    std::vector<std::shared_ptr<Camera>>      m_cameras;

    // Pending transform propagation (see mark_node_transform_dirty). Raw
    // pointers are safe: unregister_node() removes the node from the list,
    // and registered nodes are kept alive by the bucket vectors above.
    // Node_transforms::scene_transform_dirty mirrors list membership so a
    // node is enqueued at most once per pass. The processing / visited
    // containers are members only to keep their capacity across frames.
    std::vector<Node*>                        m_transform_dirty_nodes;
    std::vector<Node*>                        m_transform_dirty_processing;
    std::unordered_set<const Node*>           m_transform_update_visited;
    Transform_update_stats                    m_transform_update_stats;
    bool                                      m_updating_node_transforms{false};
    // See set_transform_owner_writes().
    bool                                      m_transform_owner_writes{false};
};

} // namespace erhe::scene
