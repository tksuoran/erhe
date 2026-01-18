#include "renderers/composer.hpp"
#include "editor_log.hpp"
#include "renderers/composition_pass.hpp"

#include "erhe_profile/profile.hpp"

#include <imgui/imgui.h>

namespace editor {

// TODO Do deep copy instead / use Hierarchy

Composer::Composer(const Composer& other)
    : composition_passes{other.composition_passes}
{
}

Composer& Composer::operator=(const Composer& other)
{
    composition_passes = other.composition_passes;
    return *this;
}

Composer::Composer(Composer&& old) noexcept
    : composition_passes{std::move(old.composition_passes)}
{
}

Composer& Composer::operator=(Composer&& old) noexcept
{
    if (this != &old) {
        composition_passes = std::move(old.composition_passes);
    }
    return *this;
}

Composer::~Composer() noexcept
{
}

Composer::Composer(const std::string_view name)
    : Item{name}
{
}

auto Composer::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Composer::get_type_name() const -> std::string_view
{
    return static_type_name;
}

void Composer::render(const Render_context& context)
{
    log_composer->trace("Composer::render()");
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    for (const auto& composition_pass : composition_passes) {
        log_composer->trace("  rp: {}", composition_pass->describe());
        composition_pass->render(context);
    }
}

void Composer::imgui()
{
    if (!ImGui::TreeNodeEx("Composer", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    int pass_index = 0;
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{mutex};

    for (const auto& composition_pass : composition_passes) {
        ImGui::PushID(pass_index++);
        if (ImGui::TreeNodeEx(composition_pass->describe().c_str(), ImGuiTreeNodeFlags_Framed)) {
            composition_pass->imgui();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

}
