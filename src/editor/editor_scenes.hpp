#pragma once

#include "editor_message.hpp"
#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/scene_message.hpp"

#include <thread>

namespace editor
{

class Scene_root;

class Editor_scenes
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_type_name{"editor::Editor_scenes"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Editor_scenes ();
    ~Editor_scenes() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component  () override;
    void deinitialize_component() override;

    // Implements erhe::components::IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Public API
    void register_scene_root(const std::shared_ptr<Scene_root>& scene_root);
    void sanity_check       ();

    [[nodiscard]] auto get_scene_roots       () -> const std::vector<std::shared_ptr<Scene_root>>&;
    [[nodiscard]] auto get_current_scene_root() -> const std::shared_ptr<Scene_root>&;
    [[nodiscard]] auto scene_combo(
        const char*                  label,
        std::shared_ptr<Scene_root>& in_out_selected_entry,
        const bool                   empty_option
    ) const -> bool;

private:
    std::mutex                               m_mutex;
    std::vector<std::shared_ptr<Scene_root>> m_scene_roots;
    std::shared_ptr<Scene_root>              m_current_scene_root;
};

extern Editor_scenes* g_editor_scenes;

} // namespace editor

