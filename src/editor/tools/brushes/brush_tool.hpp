#pragma once

#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

class Brush_tool_impl;
class Tool;

class Brush_tool
    : public erhe::components::Component
{
public:
    static constexpr int         c_priority {4};
    static constexpr const char* c_type_name{"Brush_tool"};
    static constexpr const char* c_title    {"Brush Tool"};
    static constexpr uint32_t    c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Brush_tool ();
    ~Brush_tool() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

private:
    std::unique_ptr<Brush_tool_impl> m_impl;
};

extern Brush_tool* g_brush_tool;

} // namespace editor
