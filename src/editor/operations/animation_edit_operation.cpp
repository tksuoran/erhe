#include "operations/animation_edit_operation.hpp"

#include "animation/animation_player.hpp"
#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_scene/animation.hpp"

#include <fmt/format.h>

#include <utility>

namespace editor {

Animation_edit_operation::Animation_edit_operation(
    std::string&&                           label,
    std::shared_ptr<erhe::scene::Animation> animation,
    std::vector<Animation_sampler_state>&&  before,
    std::vector<Animation_sampler_state>&&  after
)
    : m_animation{std::move(animation)}
    , m_before   {std::move(before)}
    , m_after    {std::move(after)}
{
    set_description(
        fmt::format(
            "[{}] Animation_edit_operation({}, animation = {}, samplers = {})",
            get_serial(),
            label,
            m_animation->get_name(),
            m_after.size()
        )
    );
}

void Animation_edit_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Animation_edit_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Animation_edit_operation::apply(App_context& context, const std::vector<Animation_sampler_state>& states)
{
    for (const Animation_sampler_state& state : states) {
        restore_sampler_state(*m_animation.get(), state);
    }
    if (context.animation_player != nullptr) {
        context.animation_player->on_animation_edited(m_animation);
    }
}

Animation_structure_operation::Animation_structure_operation(
    std::string&&                           label,
    std::shared_ptr<erhe::scene::Animation> animation,
    Animation_state&&                       before,
    Animation_state&&                       after
)
    : m_animation{std::move(animation)}
    , m_before   {std::move(before)}
    , m_after    {std::move(after)}
{
    set_description(
        fmt::format(
            "[{}] Animation_structure_operation({}, animation = {}, channels {} -> {})",
            get_serial(),
            label,
            m_animation->get_name(),
            m_before.channels.size(),
            m_after.channels.size()
        )
    );
}

void Animation_structure_operation::execute(App_context& context)
{
    log_operations->trace("Op Execute {}", describe());
    apply(context, m_after);
}

void Animation_structure_operation::undo(App_context& context)
{
    log_operations->trace("Op Undo {}", describe());
    apply(context, m_before);
}

void Animation_structure_operation::apply(App_context& context, const Animation_state& state)
{
    restore_animation_state(*m_animation.get(), state);
    if (context.animation_player != nullptr) {
        context.animation_player->on_animation_edited(m_animation);
    }
}

}
