#pragma once

#include "operations/operation.hpp"

#include "animation/animation_edit.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::scene { class Animation; }

namespace editor {

// Undoable edit of animation keyframe data: holds before/after snapshots of
// the affected samplers' storage (timestamps + values). Used by the
// Animation window (keyframe move / insert / delete gestures) and the MCP
// animation edit tools. Snapshots are captured by the caller around the
// edit; execute() re-applies the "after" state (a no-op right after the
// edit) and undo() restores the "before" state.
class Animation_edit_operation : public Operation
{
public:
    Animation_edit_operation(
        std::string&&                           label,
        std::shared_ptr<erhe::scene::Animation> animation,
        std::vector<Animation_sampler_state>&&  before,
        std::vector<Animation_sampler_state>&&  after
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const std::vector<Animation_sampler_state>& states);

    std::shared_ptr<erhe::scene::Animation> m_animation;
    std::vector<Animation_sampler_state>    m_before;
    std::vector<Animation_sampler_state>    m_after;
};

// Undoable structural edit of an animation: full before/after snapshots of
// the samplers and channels arrays. Used by keying (autokey / Create Key /
// Delete Key), which can add channels and touch many samplers in one
// gesture - shapes the per-sampler snapshots of Animation_edit_operation
// cannot represent.
class Animation_structure_operation : public Operation
{
public:
    Animation_structure_operation(
        std::string&&                           label,
        std::shared_ptr<erhe::scene::Animation> animation,
        Animation_state&&                       before,
        Animation_state&&                       after
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const Animation_state& state);

    std::shared_ptr<erhe::scene::Animation> m_animation;
    Animation_state                         m_before;
    Animation_state                         m_after;
};

}
