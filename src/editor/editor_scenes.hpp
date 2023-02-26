#pragma once

//#include "editor_message.hpp"
#include "erhe/components/components.hpp"
//#include "erhe/message_bus/message_bus.hpp"
//#include "erhe/scene/scene_message.hpp"

//#include <thread>

#include <memory>

namespace editor
{

class Scene_root;

class IEditor_scenes
{
public:
    virtual ~IEditor_scenes() noexcept;

    virtual void register_scene_root   (const std::shared_ptr<Scene_root>& scene_root) = 0;
    virtual void sanity_check          () = 0;
    virtual void update_node_transforms() = 0;
    virtual void update_network        () = 0;

    [[nodiscard]] virtual auto get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>& = 0;;
    [[nodiscard]] virtual auto scene_combo(
        const char*                  label,
        std::shared_ptr<Scene_root>& in_out_selected_entry,
        const bool                   empty_option
    ) const -> bool = 0;
};

class Editor_scenes_impl;

class Editor_scenes
    : public erhe::components::Component
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

private:
    std::unique_ptr<Editor_scenes_impl> m_impl;
};

extern IEditor_scenes* g_editor_scenes;

} // namespace editor

