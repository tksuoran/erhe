#pragma once

#include "operations/operation.hpp"

#include "scene/generated/lightmap_tile_override.hpp"

#include <memory>
#include <vector>

namespace editor {

class Scene_root;

// Replaces a scene's lightmap quadtree leaf overrides
// (Scene_settings::lightmap_tile_overrides, the Lightmap window's subdivide /
// merge). Execute and undo write the list, then let the Lightmap window
// re-prepare a live partition clipped against the previous grid.
class Lightmap_tile_overrides_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<Scene_root>         scene_root;
        std::vector<Lightmap_tile_override> before;
        std::vector<Lightmap_tile_override> after;
    };

    explicit Lightmap_tile_overrides_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const std::vector<Lightmap_tile_override>& overrides);

    Parameters m_parameters;
};

} // namespace editor
