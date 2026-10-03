#include "operations/lightmap_tile_overrides_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "windows/lightmap_window.hpp"

#include "erhe_verify/verify.hpp"

namespace editor {

Lightmap_tile_overrides_operation::Lightmap_tile_overrides_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    ERHE_VERIFY(m_parameters.scene_root);
    set_description(
        fmt::format(
            "[{}] Lightmap tile overrides {} -> {}",
            get_serial(),
            m_parameters.before.size(),
            m_parameters.after.size()
        )
    );
}

void Lightmap_tile_overrides_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_parameters.after);
}

void Lightmap_tile_overrides_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_parameters.before);
}

void Lightmap_tile_overrides_operation::apply(App_context& context, const std::vector<Lightmap_tile_override>& overrides)
{
    m_parameters.scene_root->get_scene_settings().lightmap_tile_overrides = overrides;
    if (context.lightmap_window != nullptr) {
        context.lightmap_window->on_tile_overrides_changed(*m_parameters.scene_root.get());
    }
}

} // namespace editor
