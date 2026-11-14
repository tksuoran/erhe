#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace erhe::scene {
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_context;
class Navigation_mesh;

// The session-only "Navigation Mesh" node of a navigation mesh with `reachable` as its flags, one per polygon
// (doc/editor/navigation.md "Visualization"). Main thread, while the upload command buffer records; null for an empty mesh.
[[nodiscard]] auto make_navigation_visualization(
    App_context&             context,
    const Navigation_mesh&   mesh,
    std::span<const uint8_t> reachable
) -> std::shared_ptr<erhe::scene::Node>;

}
