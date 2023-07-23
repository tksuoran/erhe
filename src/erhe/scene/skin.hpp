#pragma once

#include "erhe/scene/node.hpp"

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
    : public Item
{
public:
    Skin();
    explicit Skin(const std::string_view name);
    ~Skin() noexcept override;

    // Implements Item
    static constexpr std::string_view static_type_name{"Skin"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    [[nodiscard]] auto get_type     () const -> uint64_t         override;
    [[nodiscard]] auto get_type_name() const -> std::string_view override;

    Skin_data skin_data;
};

[[nodiscard]] auto operator<(const Skin& lhs, const Skin& rhs) -> bool;

[[nodiscard]] auto is_skin(const Item* scene_item) -> bool;
[[nodiscard]] auto is_skin(const std::shared_ptr<Item>& scene_item) -> bool;
[[nodiscard]] auto is_bone(const Item* const item) -> bool;
[[nodiscard]] auto is_bone(const std::shared_ptr<Item>& item) -> bool;

} // namespace erhe::scene
