#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/primitive/build_info.hpp"
#include "erhe/primitive/enums.hpp"

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
    class Vertex_input_state;
}

namespace erhe::primitive
{
    class Raytrace_buffer_sink;
    class Gl_buffer_sink;
    class Material;
    class Primitive;
    class Primitive_geometry;
}

namespace erhe::raytrace
{
    class IBuffer;
}

namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Light;
    class Mesh;
    class Mesh_layer;
    class Scene;
}

namespace editor
{

class Mesh_memory
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Mesh_memory"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Mesh_memory ();
    ~Mesh_memory() noexcept override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    [[nodiscard]] auto gl_vertex_format() const -> erhe::graphics::Vertex_format&;
    [[nodiscard]] auto gl_index_type   () const -> gl::Draw_elements_type;

    std::unique_ptr<erhe::graphics::Buffer_transfer_queue> gl_buffer_transfer_queue;
    std::unique_ptr<erhe::primitive::Gl_buffer_sink>       gl_buffer_sink;
    std::unique_ptr<erhe::graphics::Vertex_input_state>    vertex_input;
    std::shared_ptr<erhe::graphics::Buffer>                gl_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer>                gl_index_buffer;
    erhe::primitive::Build_info                            build_info;
};

} // namespace editor
