#pragma once

#include "time.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace editor
{

class Editor_context;
class Scene_root;
class Time;

class Editor_scenes
    : public Update_fixed_step
    , public Update_once_per_frame
{
public:
    Editor_scenes(
        Editor_context& editor_context,
        Time&           time
    );

    void register_scene_root                     (Scene_root* scene_root);
    void unregister_scene_root                   (Scene_root* scene_root);
    void sanity_check                            ();
    void update_physics_simulation_fixed_step    (const Time_context& time);
    void update_physics_simulation_once_per_frame();
    void update_node_transforms                  ();

    void update_fixed_step    (const Time_context&) override;
    void update_once_per_frame(const Time_context&) override;

    [[nodiscard]] auto get_scene_roots() -> const std::vector<Scene_root*>&;
    [[nodiscard]] auto scene_combo(
        const char*  label,
        Scene_root*& in_out_selected_entry,
        const bool   empty_option
    ) const -> bool;

private:
    Editor_context&          m_context;
    std::mutex               m_mutex;
    std::vector<Scene_root*> m_scene_roots;
};

} // namespace editor

