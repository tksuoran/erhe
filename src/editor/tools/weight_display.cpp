#include "tools/weight_display.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"

#include <imgui/imgui.h>

#include <cstdint>

namespace editor {

namespace {

constexpr uint32_t c_no_joint = 0xffffffffu;

// Is `joint` a joint of any skin in its scene? Only used to decide whether
// the active joint is still meaningful; the shader-side match is done per
// joint slot in the joint buffer (Joint_buffer::update writes
// debug_flags.x).
auto is_joint_of_any_skin(const erhe::scene::Node& joint) -> bool
{
    Scene_root* const scene_root = static_cast<Scene_root*>(joint.get_item_host());
    if (scene_root == nullptr) {
        return false;
    }
    for (const std::shared_ptr<erhe::scene::Skin>& skin : scene_root->get_scene().get_skins()) {
        if (!skin) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = skin->skin_data.joints;
        for (const std::shared_ptr<erhe::scene::Node>& skin_joint : joints) {
            if (skin_joint.get() == &joint) {
                return true;
            }
        }
    }
    return false;
}

} // anonymous namespace

Weight_display::Weight_display(App_context& context, App_message_bus& app_message_bus)
    : m_context{context}
{
    // Runs inside the init taskflow, before App_context is filled; the first
    // debug_joint_indices write happens at the first message (no scene - and
    // thus no joint - exists before that).
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message& message) { on_selection(message); }
    );
    m_skin_registered_subscription = app_message_bus.skin_registered.subscribe(
        [this](Skin_registered_message& message) { on_skin_registered(message); }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) { on_close_scene(message); }
    );
}

Weight_display::~Weight_display() noexcept = default;

auto Weight_display::get_active_joint() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_active_joint.lock();
}

void Weight_display::set_active_joint(const std::shared_ptr<erhe::scene::Node>& joint)
{
    m_active_joint = joint;
    update_debug_joint_indices();
}

auto Weight_display::get_show_zero_weight_black() const -> bool
{
    return m_show_zero_weight_black;
}

void Weight_display::set_show_zero_weight_black(const bool value)
{
    m_show_zero_weight_black = value;
    update_debug_joint_indices();
}

void Weight_display::on_selection(Selection_message& message)
{
    // Bone-mode viewport clicks and item tree clicks select the joint Node
    // itself (never the proxy mesh), so Item_flags::bone on a newly selected
    // node is the whole test. The last joint in the batch wins; deselection
    // does not clear the active joint on purpose - the weight display should
    // survive clicking empty space.
    std::shared_ptr<erhe::scene::Node> new_active{};
    for (const std::shared_ptr<erhe::Item_base>& item : message.selection_change.newly_selected) {
        if (!erhe::scene::is_bone(item)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (node) {
            new_active = node;
        }
    }
    if (new_active) {
        m_active_joint = new_active;
    }
    update_debug_joint_indices();
}

void Weight_display::on_skin_registered(Skin_registered_message&)
{
    // A skin leaving a scene can be the one that made the active joint a
    // joint at all; re-resolve so a joint that is no longer skinned is
    // dropped.
    update_debug_joint_indices();
}

void Weight_display::on_close_scene(Close_scene_message& message)
{
    const std::shared_ptr<erhe::scene::Node> joint = m_active_joint.lock();
    if (joint && ((joint->get_item_host() == message.scene_root.get()) || (joint->get_item_host() == nullptr))) {
        m_active_joint.reset();
    }
    update_debug_joint_indices();
}

void Weight_display::update_debug_joint_indices()
{
    if (m_context.app_rendering == nullptr) {
        return;
    }
    std::shared_ptr<erhe::scene::Node> joint = m_active_joint.lock();
    if (joint && !is_joint_of_any_skin(*joint)) {
        m_active_joint.reset(); // no longer a joint of any skin
        joint.reset();
    }
    m_context.app_rendering->debug_target_joint  = joint;
    m_context.app_rendering->debug_joint_indices = glm::uvec4{
        joint ? 0u : c_no_joint,
        m_show_zero_weight_black ? 1u : 0u,
        0u,
        0u
    };
    log_tools->trace(
        "Weight_display: active joint = '{}' global index = {:#x} zero_black = {}",
        joint ? joint->get_name() : "(none)", index, m_show_zero_weight_black
    );
}

void Weight_display::imgui()
{
    const std::shared_ptr<erhe::scene::Node> joint = m_active_joint.lock();
    if (joint) {
        ImGui::Text("Active Joint: %s", joint->get_name().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            m_active_joint.reset();
            update_debug_joint_indices();
        }
    } else {
        ImGui::TextUnformatted("Active Joint: (select a bone)");
    }
    bool zero_black = m_show_zero_weight_black;
    if (ImGui::Checkbox("Show Zero Weights as Black", &zero_black)) {
        set_show_zero_weight_black(zero_black);
    }
}

}
