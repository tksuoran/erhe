#pragma once

#include "erhe/components/component.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/enums.hpp"
#include "erhe/primitive/format_info.hpp"

namespace erhe::geometry
    {
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
}

namespace erhe::primitive
{
    struct Material;
    struct Primitive_build_context;
    struct Primitive_geometry;
    struct Primitive;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Layer;
    class Light;
    class Mesh;
    class Scene;
}

namespace erhe::physics
{
    class World;
}

namespace editor
{

class Mesh_memory
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Mesh_memory";
    Mesh_memory ();
    ~Mesh_memory() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    auto vertex_buffer          () -> erhe::graphics::Buffer*;
    auto index_buffer           () -> erhe::graphics::Buffer*;
    auto index_type             () -> gl::Draw_elements_type;
    auto vertex_format          () -> std::shared_ptr<erhe::graphics::Vertex_format>;
    auto vertex_format_info     () const -> const erhe::primitive::Format_info&;
    auto vertex_buffer_info     () -> erhe::primitive::Buffer_info&;
    auto primitive_build_context() -> erhe::primitive::Primitive_build_context&;
    auto buffer_transfer_queue  () -> erhe::graphics::Buffer_transfer_queue&;

private:
    std::unique_ptr<erhe::graphics::Buffer_transfer_queue>    m_buffer_transfer_queue;
    std::unique_ptr<erhe::primitive::Primitive_build_context> m_primitive_build_context;
    erhe::primitive::Format_info                              m_format_info;
    erhe::primitive::Buffer_info                              m_buffer_info;

};

} // namespace editor
