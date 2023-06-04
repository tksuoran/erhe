#pragma once

#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene
{

class Skin_data
{
public:
    uint32_t                                        joint_buffer_index{0}; // updated by Joint_buffer::update()
    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    std::vector<glm::mat4>                          inverse_bind_matrices;
    std::shared_ptr<erhe::scene::Node>              skeleton;
};

class Skin
    : public Node_attachment
{
public:
    Skin         ();
    explicit Skin(const std::string_view name);
    ~Skin        () noexcept override;

    // Implements Node_attachment
    void handle_node_scene_host_update(Scene_host* old_scene_host, Scene_host* new_scene_host) override;

    // Implements Item
    [[nodiscard]] static auto static_type     () -> uint64_t;
    [[nodiscard]] static auto static_type_name() -> const char*;
    [[nodiscard]] auto get_type () const -> uint64_t override;
    [[nodiscard]] auto type_name() const -> const char* override;

    Skin_data skin_data;

    erhe::toolkit::Unique_id<Skin> m_id;
};

[[nodiscard]] auto operator<(const Skin& lhs, const Skin& rhs) -> bool;

[[nodiscard]] auto is_skin(const Item* scene_item) -> bool;
[[nodiscard]] auto is_skin(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto as_skin(Item* scene_item) -> Skin*;
[[nodiscard]] auto as_skin(const std::shared_ptr<Item>& scene_item) -> std::shared_ptr<Skin>;

[[nodiscard]] auto get_skin(const erhe::scene::Node* node) -> std::shared_ptr<Skin>;

auto is_bone(const Item* const item) -> bool;
auto is_bone(const std::shared_ptr<Item>& item) -> bool;

} // namespace erhe::scene
