#pragma once

#include "erhe/components/component.hpp"
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
}

namespace erhe::primitive
{
    class Embree_buffer_sink;
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
    Mesh_memory ();
    ~Mesh_memory() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    auto gl_vertex_format() const -> erhe::graphics::Vertex_format&
    {
        return *build_info_set.gl.buffer.vertex_format.get();
    }

    auto gl_index_type() const -> const gl::Draw_elements_type
    {
        return build_info_set.gl.buffer.index_type;
    }

    std::unique_ptr<erhe::graphics::Buffer_transfer_queue> gl_buffer_transfer_queue;
    std::unique_ptr<erhe::primitive::Gl_buffer_sink>       gl_buffer_sink;
    std::unique_ptr<erhe::primitive::Embree_buffer_sink>   embree_buffer_sink;
    std::shared_ptr<erhe::graphics::Buffer>                gl_vertex_buffer;
    std::shared_ptr<erhe::graphics::Buffer>                gl_index_buffer;
    erhe::primitive::Build_info_set                        build_info_set;

    std::shared_ptr<erhe::raytrace::IBuffer> embree_vertex_buffer;
    std::shared_ptr<erhe::raytrace::IBuffer> embree_index_buffer;
};

} // namespace editor
