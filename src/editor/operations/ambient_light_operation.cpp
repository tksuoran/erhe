#include "operations/ambient_light_operation.hpp"

#include "editor_log.hpp"

#include "erhe_scene/scene.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Ambient_light_operation::Ambient_light_operation(erhe::scene::Light_layer* const layer, const glm::vec4 after)
    : m_layer{layer}
    , m_after{after}
{
    ERHE_VERIFY(m_layer != nullptr);
    set_description(
        fmt::format(
            "[{}] Ambient_light_operation(layer = {}, after = ({}, {}, {}, {}))",
            get_serial(),
            m_layer->get_name(),
            m_after.x, m_after.y, m_after.z, m_after.w
        )
    );
}

void Ambient_light_operation::execute(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Execute {}", describe());

    m_before = m_layer->ambient_light;
    m_layer->ambient_light = m_after;
}

void Ambient_light_operation::undo(App_context& context)
{
    static_cast<void>(context);
    log_operations->trace("Op Undo {}", describe());

    m_layer->ambient_light = m_before;
}

}
