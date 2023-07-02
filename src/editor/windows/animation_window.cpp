#include "windows/animation_window.hpp"
#include "windows/animation_curve.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "editor_message_bus.hpp"
#include "graphics/icon_set.hpp"
#include "scene/content_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/animation.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Animation_window::Animation_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Animation", "animation"}
    , m_context                {editor_context}
{
    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            using namespace erhe::toolkit;
            if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
                if (message.scene_view != nullptr) {
                    m_scene_root = message.scene_view->get_scene_root();
                    auto root = m_scene_root.lock();
                    if (root) {
                        m_content_library = root->content_library();
                    }
                }
            }
        }
    );

}

void Animation_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    auto content_library = m_content_library.lock();
    if (!content_library) {
        return;
    }

    //ImGui::Button     ("Play");
    //ImGui::Button     ("Stop");
    //ImGui::DragFloat  ("Start Time", &m_start_time, 0.1f, 0.0f);
    //ImGui::DragFloat  ("End Time",   &m_end_time, 0.1f, 0.0f);
    const bool time_changed = ImGui::SliderFloat("Time##animation-time", &m_time, m_start_time, m_end_time);
    //ImGui::SliderFloat("Speed",      &m_speed, 0.0f, 10.0f);

    ImGui::Separator();

    for (auto& animation : content_library->animations.entries()) {
        ImGui::BulletText("%s", animation->get_name().c_str());
#if 0
        ImGui::Indent(10.0f);
        {
            ImGui::BulletText("Samplers");
            ImGui::Indent(10.0f);
            {
                for (auto& sampler : animation->samplers) {
                    std::string text = fmt::format("Sampler {}", erhe::scene::c_str(sampler.interpolation_mode));
                    ImGui::BulletText("%s", text.c_str());
                }
            }
            ImGui::Unindent(10.0f);

            ImGui::BulletText("Channels");
            ImGui::Indent(10.0f);
            {
                for (auto& channel : animation->channels) {
                    std::string text = fmt::format(
                        "{} {}",
                        //channel.target->get_name(),
                        channel.target->describe(),
                        erhe::scene::c_str(channel.path)
                    );
                    ImGui::BulletText("%s", text.c_str());
                }
            }
            ImGui::Unindent(10.0f);
        }
        ImGui::Unindent(10.0f);
#endif
        animation_curve(*animation.get());
    }

    if (!time_changed) {
        return;
    }

    for (auto& animation : content_library->animations.entries()) {
        animation->apply(m_time);
        m_context.editor_message_bus->send_message(
            Editor_message{
                .update_flags = Message_flag_bit::c_flag_bit_animation_update
            }
        );
    }

    auto root = m_scene_root.lock();
    if (root) {
        root->get_hosted_scene()->update_node_transforms();
    }

#endif
}

} // namespace editor
