#pragma once

#include "tools/tool.hpp"

#include "app_message.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_geometry/types.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <geogram/basic/numeric.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; class Node; class Skin; }

namespace editor {

class App_message_bus;
class Headset_view;
class Icon_set;
class Weight_paint_tool;

class Weight_paint_command : public erhe::commands::Command
{
public:
    Weight_paint_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    App_context& m_context;
};

enum class Weight_paint_blend {
    mix = 0,
    add,
    subtract
};

static constexpr const char* c_weight_paint_blend_strings[] = {
    "Mix",
    "Add",
    "Subtract"
};

// Brush that paints the active joint's skin weights (Blender-style weight
// paint, simplified). See doc/weight-paint-plan.md.
//
// The active joint comes from Weight_display (select a bone in bone mode).
// A stroke locks onto the first-hit primitive; each dab tests CPU-skinned
// (posed) vertex positions against a world-space brush sphere, blends the
// active joint's weight toward the target value with a smoothstep falloff,
// auto-normalizes the other influences, and patches the fill mesh's
// joint_indices_0 / joint_weights_0 GPU attributes live. Stroke end queues
// one undoable Paint_weights_operation, whose primitive rebuild also
// refreshes the wireframe / edge-line streams that carry their own copy of
// the joint data.
class Weight_paint_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Weight_paint_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_render           (const Render_context& context)      override;
    void tool_properties       (erhe::imgui::Imgui_window&)         override;

    auto try_ready () -> bool;
    void paint     ();
    void end_stroke();

private:
    // Per-stroke bookkeeping for one touched geometry vertex. before_* feed
    // the undo operation; stroke_start_weight / alpha_max implement Blender's
    // non-accumulate behavior (blend from the stroke-start weight, and only
    // where this dab's alpha exceeds the strongest alpha seen so far).
    class Stroke_vertex
    {
    public:
        glm::uvec4 before_joint_indices{0};
        glm::vec4  before_joint_weights{0.0f};
        float      stroke_start_weight {0.0f};
        float      alpha_max           {0.0f};
    };

    [[nodiscard]] auto begin_stroke() -> bool;
    void apply_dab();

    // Blend `alpha` worth of the target weight into `base`, clamp to [0, 1]
    // and snap near-zero to exactly 0 so painting to zero terminates.
    [[nodiscard]] auto blend_weight(float base, float alpha) const -> float;

    // Write joint data for one geometry vertex: geometry attributes (float
    // truth) plus a GPU patch of the fill mesh's two attributes for every
    // GPU vertex spawned from the geometry vertex.
    void write_vertex_joints(GEO::index_t vertex, const glm::uvec4& joint_indices, const glm::vec4& joint_weights);
    void enqueue_gpu_joint_data(uint32_t vertex_buffer_index, const glm::uvec4& joint_indices, const glm::vec4& joint_weights);

    Weight_paint_command                m_paint_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;

    // Brush parameters
    float              m_weight        {1.0f};  // target value (Blender's Weight)
    float              m_strength      {0.5f};
    float              m_radius        {0.25f}; // world units
    Weight_paint_blend m_blend         {Weight_paint_blend::mix};
    bool               m_accumulate    {false};
    bool               m_auto_normalize{true};
    bool               m_front_face_only{true};

    // Stroke state, valid while m_stroke_active
    bool                                            m_stroke_active{false};
    std::weak_ptr<erhe::scene::Mesh>                m_stroke_mesh;
    std::size_t                                     m_stroke_primitive_index{0};
    std::shared_ptr<erhe::geometry::Geometry>       m_stroke_geometry;
    std::shared_ptr<erhe::scene::Skin>              m_stroke_skin;
    uint32_t                                        m_stroke_joint_local_index{0}; // index within the skin's joints
    std::unordered_map<GEO::index_t, Stroke_vertex> m_stroke_vertices;

    std::string m_status; // why the brush refuses, shown in properties
};

}
