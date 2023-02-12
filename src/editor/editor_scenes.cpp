#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"

#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

namespace editor
{

IEditor_scenes::~IEditor_scenes() noexcept = default;

class Editor_scenes_impl
    : public IEditor_scenes
{
public:
    Editor_scenes_impl()
    {
        ERHE_VERIFY(g_editor_scenes == nullptr);
        g_editor_scenes = this;
    }
    ~Editor_scenes_impl() noexcept override
    {
        ERHE_VERIFY(g_editor_scenes == this);
        g_editor_scenes = nullptr;
    }

    void register_scene_root   (const std::shared_ptr<Scene_root>& scene_root) override;
    void update_node_transforms() override;
    void sanity_check          () override;

    [[nodiscard]] auto get_scene_roots       () -> const std::vector<std::shared_ptr<Scene_root>>& override;
    [[nodiscard]] auto scene_combo(
        const char*                  label,
        std::shared_ptr<Scene_root>& in_out_selected_entry,
        const bool                   empty_option
    ) const -> bool override;

private:
    std::mutex                               m_mutex;
    std::vector<std::shared_ptr<Scene_root>> m_scene_roots;
};

IEditor_scenes* g_editor_scenes{nullptr};

Editor_scenes::Editor_scenes()
    : Component{c_type_name}
{
}

Editor_scenes::~Editor_scenes() noexcept
{
    ERHE_VERIFY(g_editor_scenes == nullptr);
}

void Editor_scenes::deinitialize_component()
{
    m_impl.reset();
}

void Editor_scenes::initialize_component()
{
    m_impl = std::make_unique<Editor_scenes_impl>();
}

void Editor_scenes_impl::register_scene_root(
    const std::shared_ptr<Scene_root>& scene_root
)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    m_scene_roots.push_back(scene_root);
}

void Editor_scenes_impl::update_node_transforms()
{
    for (const auto& scene_root : m_scene_roots)
    {
        scene_root->scene().update_node_transforms();
    }
}

[[nodiscard]] auto Editor_scenes_impl::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

void Editor_scenes_impl::sanity_check()
{
#if !defined(NDEBUG)
    for (const auto& scene_root : m_scene_roots)
    {
        scene_root->sanity_check();
    }
#endif
}

auto Editor_scenes_impl::scene_combo(
    const char*                  label,
    std::shared_ptr<Scene_root>& in_out_selected_entry,
    const bool                   empty_option
) const -> bool
{
    int selection_index = 0;
    int index = 0;
    std::vector<const char*> names;
    std::vector<std::shared_ptr<Scene_root>> entries;
    const bool empty_entry = empty_option || (!in_out_selected_entry);
    if (empty_entry)
    {
        names.push_back("(none)");
        entries.push_back({});
        ++index;
    }
    for (const auto& entry : m_scene_roots)
    {
        names.push_back(entry->get_name().c_str());
        entries.push_back(entry);
        if (in_out_selected_entry == entry)
        {
            selection_index = index;
        }
        ++index;
    }

    // TODO Move to begin / end combo
    const bool selection_changed =
        ImGui::Combo(
            label,
            &selection_index,
            names.data(),
            static_cast<int>(names.size())
        ) &&
        (in_out_selected_entry != entries.at(selection_index));
    if (selection_changed)
    {
        in_out_selected_entry = entries.at(selection_index);
    }
    return selection_changed;
}

} // namespace hextiles

