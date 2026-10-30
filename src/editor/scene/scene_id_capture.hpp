#pragma once

#include "renderers/id_pass_prepared.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
    class Render_pass;
    class Texture;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class App_context;
class Scene_root;

// Which meshes a whole-image id capture draws.
enum class Scene_id_capture_content : unsigned int {
    scene_only = 0, // the scene's content layer
    viewport        // content, rendertarget and bone layers: the set a viewport's id pass draws
};

enum class Scene_id_capture_state : unsigned int {
    rendering = 0, // node enabled, waiting for the rendergraph to run it
    in_flight,     // id pass and copy recorded, waiting for that frame to retire
    complete,      // ids read back and decoded (get_entries)
    failed         // see get_error
};

// One (mesh, primitive) the captured id image shows, with its pixel count.
class Scene_id_capture_entry
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    std::size_t                        primitive_index{0};
    std::size_t                        pixel_count    {0};
};

// What the render phase recorded for a capture (Scene_id_capture_node),
// read by the main thread through Scene_id_capture::poll().
class Scene_id_capture_record
{
public:
    Scene_id_capture_state                  state{Scene_id_capture_state::rendering};
    std::string                             error;
    uint64_t                                frame{0};
    std::unique_ptr<erhe::graphics::Buffer> buffer;
    // The id range table of the pass drawn into the image: the ids of the
    // read-back pixels resolve against it.
    std::vector<erhe::scene_renderer::Primitive_buffer::Id_range> id_ranges;
};

// The rendergraph node of a whole-image id capture (MCP get_visible_entries;
// doc/editor/editor.md "MCP diagnostics that read R's output"). It owns
// everything its execute reads and writes: the id color and depth targets
// of the request's size, the render pass, one prepared id pass per frame in
// flight, the camera copy taken at the request and the record. A node with
// no links: it neither consumes nor produces a rendergraph texture.
//
// Prepare phase (prepare_rendergraph_node): while nothing is recorded, the
// frame's slot gets the id pass of the scene's meshes (Id_renderer::prepare,
// all meshes, no scan) under the scene's item_host_mutex; once the render
// phase recorded (or failed), the node disables itself. Render phase: draws
// the frame's prepared pass over the whole image (Id_renderer::render_image)
// and records the copy of the whole id texture into a host-readable buffer,
// once. The record is shared under m_mutex: with frame pipelining the main
// thread prepares the next frame while the render phase records this one.
class Scene_id_capture_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Scene_id_capture_node(
        erhe::rendergraph::Rendergraph&     rendergraph,
        App_context&                        context,
        const std::shared_ptr<Scene_root>&  scene_root,
        const erhe::scene::Camera_snapshot& camera,
        int                                 width,
        int                                 height,
        Scene_id_capture_content            content
    );
    ~Scene_id_capture_node() noexcept override;

    auto get_type_name           () const -> std::string_view override { return "Scene_id_capture_node"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // Main thread: calls op with the record under m_mutex.
    template <typename Op>
    auto with_record(Op&& op) -> decltype(op(std::declval<Scene_id_capture_record&>()))
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return op(m_record);
    }

protected:
    void prepare_rendergraph_node(uint64_t frame_index) override;

private:
    class Frame_slot
    {
    public:
        uint64_t                    frame_index{0};
        // The scene root of the frame (its joint poses for skinned meshes);
        // null when the slot is not prepared.
        std::shared_ptr<Scene_root> scene_root;
        Id_pass_prepared            pass;
    };

    void fail(const std::string& error);

    App_context&                                 m_context;
    std::weak_ptr<Scene_root>                    m_scene_root;
    erhe::scene::Camera_snapshot                 m_camera;
    erhe::math::Viewport                         m_viewport{0, 0, 0, 0};
    Scene_id_capture_content                     m_content{Scene_id_capture_content::scene_only};
    std::unique_ptr<erhe::graphics::Texture>     m_color_texture;
    std::unique_ptr<erhe::graphics::Texture>     m_depth_texture;
    std::unique_ptr<erhe::graphics::Render_pass> m_render_pass;
    std::vector<Frame_slot>                      m_frame_slots;
    std::mutex                                   m_mutex;
    Scene_id_capture_record                      m_record;
};

class Scene_id_capture_create_info
{
public:
    std::shared_ptr<Scene_root>  scene_root;
    erhe::scene::Camera_snapshot camera;
    int                          width  {0};
    int                          height {0};
    Scene_id_capture_content     content{Scene_id_capture_content::scene_only};
};

// One get_visible_entries request: registers its Scene_id_capture_node when
// constructed, the rendergraph runs it on the next frame, and the destructor
// releases the node again, so the tool costs nothing per frame when unused.
// Destroy it only once poll() reports complete or failed (the recorded
// frame has retired), so no GPU work references its targets.
class Scene_id_capture
{
public:
    Scene_id_capture(App_context& context, const Scene_id_capture_create_info& create_info);
    ~Scene_id_capture() noexcept;

    Scene_id_capture(const Scene_id_capture&) = delete;
    auto operator=(const Scene_id_capture&) -> Scene_id_capture& = delete;

    // Main thread: the capture's state; decodes the ids once the frame that
    // recorded the copy has retired.
    [[nodiscard]] auto poll                  () -> Scene_id_capture_state;
    [[nodiscard]] auto get_width             () const -> int { return m_width; }
    [[nodiscard]] auto get_height            () const -> int { return m_height; }
    [[nodiscard]] auto get_error             () const -> const std::string& { return m_error; }
    // Valid while poll() reports complete: the frame the ids were rendered
    // in, the pixels with any id, the pixels whose id names no live mesh
    // primitive (its mesh destroyed since, or outside every id range), and
    // the entries, sorted by mesh then primitive index.
    [[nodiscard]] auto get_frame_index       () const -> uint64_t    { return m_frame_index; }
    [[nodiscard]] auto get_covered_pixels    () const -> std::size_t { return m_covered_pixels; }
    [[nodiscard]] auto get_unresolved_pixels () const -> std::size_t { return m_unresolved_pixels; }
    [[nodiscard]] auto get_entries           () const -> const std::vector<Scene_id_capture_entry>& { return m_entries; }

private:
    void decode(erhe::graphics::Buffer& readback_buffer, const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>& id_ranges);

    App_context&                           m_context;
    int                                    m_width {0};
    int                                    m_height{0};
    Scene_id_capture_state                 m_state{Scene_id_capture_state::rendering};
    std::string                            m_error;
    std::shared_ptr<Scene_id_capture_node> m_node;
    uint64_t                               m_frame_index      {0};
    std::size_t                            m_covered_pixels   {0};
    std::size_t                            m_unresolved_pixels{0};
    std::vector<Scene_id_capture_entry>    m_entries;
};

} // namespace editor
