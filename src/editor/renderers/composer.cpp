#include "renderers/composer.hpp"
#include "editor_log.hpp"
#include "renderers/renderpass.hpp"

#include <imgui/imgui.h>

namespace editor {

// TODO Do deep copy instead / use Hierarchy

Composer::Composer(const Composer&)            = default;
Composer& Composer::operator=(const Composer&) = default;
Composer::~Composer() noexcept                 = default;

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

void Composer::render(const Render_context& context) const
{
    log_composer->trace("Composer::render()");
    for (const auto& renderpass : renderpasses) {
        log_composer->trace("  rp: {}", renderpass->describe());
        renderpass->render(context);
    }
}

void Composer::imgui()
{
    if (!ImGui::TreeNodeEx("Composer", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    int renderpass_index = 0;
    for (const auto& renderpass : renderpasses) {
        ImGui::PushID(renderpass_index++);
        if (ImGui::TreeNodeEx(renderpass->describe().c_str(), ImGuiTreeNodeFlags_Framed)) {
            renderpass->imgui();
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
}

} // namespace editor
