#pragma once

#include "tools/transform/subtool.hpp"
#include "tools/tool.hpp"

#include "erhe/components/components.hpp"

#include <string_view>

namespace editor
{

enum class Handle : unsigned int;

class Move_tool
    : public erhe::components::Component
    , public Tool
    , public Subtool
{
public:
    static constexpr int              c_priority {1};
    static constexpr std::string_view c_type_name{"Move_tool"};
    static constexpr std::string_view c_title    {"Move"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Move_tool ();
    ~Move_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;

    // Implemennts Subtool
    void imgui ()                                                       override;
    auto begin (unsigned int axis_mask, Scene_view* scene_view) -> bool override;
    auto update(Scene_view* scene_view) -> bool                         override;

private:
    void update(const glm::vec3 drag_position);

    [[nodiscard]] auto snap(const glm::vec3 translation) const -> glm::vec3;

    int m_translate_snap_index{2};
};

extern Move_tool* g_move_tool;

} // namespace editor
