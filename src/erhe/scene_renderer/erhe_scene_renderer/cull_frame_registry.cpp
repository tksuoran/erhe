#include "erhe_scene_renderer/cull_frame_registry.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Cull_frame_registry::Cull_frame_registry()
{
    m_frame_scenes.reserve(8);
    m_record_scenes.reserve(8);
    m_frame_end_scenes.reserve(8);
}

Cull_frame_registry::~Cull_frame_registry() noexcept = default;

void Cull_frame_registry::add_frame_scene(Draw_list_scene& draw_list_scene, const uint64_t frame_index)
{
    erhe::utility::Thread_roles::assert_render_thread();
    // Every scene of one classification belongs to the same frame.
    ERHE_VERIFY(m_frame_scenes.empty() || (m_frame_index == frame_index));
    m_frame_index = frame_index;
    m_frame_scenes.push_back(&draw_list_scene);
}

void Cull_frame_registry::classify_frame(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::utility::Thread_roles::assert_render_thread();
    if (m_frame_scenes.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    for (Draw_list_scene* draw_list_scene : m_frame_scenes) {
        draw_list_scene->classify_frame_cull_views(m_frame_index, command_buffer);
    }
    m_frame_scenes.clear();
}

void Cull_frame_registry::add_record_scene(Draw_list_scene& draw_list_scene, const uint64_t frame_index)
{
    erhe::utility::Thread_roles::assert_render_thread();
    // Every scene of one release belongs to the same frame.
    ERHE_VERIFY(m_record_scenes.empty() || (m_record_frame_index == frame_index));
    m_record_frame_index = frame_index;
    m_record_scenes.push_back(&draw_list_scene);
}

void Cull_frame_registry::release_frame_records()
{
    erhe::utility::Thread_roles::assert_render_thread();
    if (m_record_scenes.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    for (Draw_list_scene* draw_list_scene : m_record_scenes) {
        draw_list_scene->release_frame_records(m_record_frame_index);
    }
    m_record_scenes.clear();
}

void Cull_frame_registry::add_frame_end_scene(Draw_list_scene& draw_list_scene, const uint64_t frame_index)
{
    erhe::utility::Thread_roles::assert_render_thread();
    // Every scene of one frame end belongs to the same frame.
    ERHE_VERIFY(m_frame_end_scenes.empty() || (m_frame_end_frame_index == frame_index));
    m_frame_end_frame_index = frame_index;
    m_frame_end_scenes.push_back(&draw_list_scene);
}

void Cull_frame_registry::record_frame_end(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::utility::Thread_roles::assert_render_thread();
    if (m_frame_end_scenes.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    for (Draw_list_scene* draw_list_scene : m_frame_end_scenes) {
        draw_list_scene->record_frame_end(m_frame_end_frame_index, command_buffer);
    }
    m_frame_end_scenes.clear();
}

auto Cull_frame_registry::has_frame_end_scenes() const -> bool
{
    return !m_frame_end_scenes.empty();
}

} // namespace erhe::scene_renderer
