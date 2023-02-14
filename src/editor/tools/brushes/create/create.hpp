#pragma once

#include "erhe/components/components.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/scene/transform.hpp"

//#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Brush;
class Brush_data;
class Render_context;

class Create_preview_settings
{
public:
    const Render_context&         render_context;
    const erhe::scene::Transform& transform;
    glm::vec4                     major_color    {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4                     minor_color    {1.0f, 1.0f, 1.0f, 0.5f};
    const float                   major_thickness{6.0f};
    const float                   minor_thickness{3.0f};
    bool                          ideal_shape    {false};
};

class Brush_create
{
public:
    virtual void render_preview(const Create_preview_settings& preview_settings) = 0;

    virtual void imgui() = 0;

    [[nodiscard]] virtual auto create(Brush_data& brush_create_info) const -> std::shared_ptr<Brush> = 0;
};

class Create_impl;

class Create
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_title    {"Create"};
    static constexpr std::string_view c_type_name{"Create"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Create ();
    ~Create() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

private:
    std::unique_ptr<Create_impl> m_impl;
};

} // namespace editor
