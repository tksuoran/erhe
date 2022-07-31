#pragma once

#include "erhe/components/components.hpp"

#include <thread>

namespace editor
{

class Scene_root;

class Editor_scenes
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_label{"editor::Editor_scenes"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Editor_scenes ();
    ~Editor_scenes() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Implements  erhe::components::IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Public API
    void register_scene_root(const std::shared_ptr<Scene_root>& scene_root);

    [[nodiscard]] auto get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&;
    [[nodiscard]] auto get_scene_root () -> const std::shared_ptr<Scene_root>&;

private:
    std::mutex                               m_mutex;
    std::vector<std::shared_ptr<Scene_root>> m_scene_roots;
    std::shared_ptr<Scene_root>              m_scene_root;
};

} // namespace editor

