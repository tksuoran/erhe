#pragma once

#include "renderers/programs.hpp"
#include "renderers/frame_resources.hpp"
#include "renderers/light_mesh.hpp"

#include "erhe/components/component.hpp"
#include "erhe/graphics/configuration.hpp"

#include "erhe/scene/mesh.hpp"
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>


namespace erhe::graphics
{
    class buffer;
}

namespace erhe::primitive
{
    struct Primitive;
    struct Material;
}

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Layer;
    class Light;
    class Node;
    struct Viewport;
}

namespace editor
{


class Programs;

class Base_renderer
{
public:
    enum class Primitive_color_source : unsigned int
    {
        id_offset = 0,
        mesh_wireframe_color,
        constant_color,
    };

    Primitive_color_source primitive_color_source  {Primitive_color_source::constant_color};
    glm::vec4              primitive_constant_color{1.0f, 1.0f, 1.0f, 1.0f};

    enum class Primitive_size_source : unsigned int
    {
        mesh_point_size,
        mesh_line_width,
        constant_size
    };
    Primitive_size_source primitive_size_source  {Primitive_size_source::constant_size};
    float                 primitive_constant_size{1.0f};

    struct Buffer_range
    {
        size_t first_byte_offset{0};
        size_t byte_count       {0};
    };

    struct Draw_indirect_buffer_range
    {
        Buffer_range range;
        size_t       draw_indirect_count{0};
    };

    struct Id_range
    {
        uint32_t                           offset         {0};
        uint32_t                           length         {0};
        std::shared_ptr<erhe::scene::Mesh> mesh           {nullptr};
        size_t                             primitive_index{0};
    };

    using Layer_collection    = std::vector<std::shared_ptr<erhe::scene::Layer>>;
    using Light_collection    = std::vector<std::shared_ptr<erhe::scene::Light>>;
    using Material_collection = std::vector<std::shared_ptr<erhe::primitive::Material>>;
    using Mesh_collection     = std::vector<std::shared_ptr<erhe::scene::Mesh>>;

    virtual ~Base_renderer() = default;

    static constexpr size_t s_frame_resources_count = 4;

    void create_frame_resources(size_t material_count,
                                size_t light_count,
                                size_t camera_count,
                                size_t primitive_count,
                                size_t draw_count);

    auto current_frame_resources() -> Frame_resources&;

    void next_frame();

    void base_connect(erhe::components::Component* component);

    auto update_primitive_buffer(const Mesh_collection& meshes,
                                 uint64_t               visibility_mask = erhe::scene::Mesh::c_visibility_all)
    -> Buffer_range;

    auto update_light_buffer(Light_collection&     lights,
                             erhe::scene::Viewport light_texture_viewport,
                             glm::vec4             ambient_light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f))
    -> Buffer_range;

    auto update_material_buffer(const Material_collection& materials)

    -> Buffer_range;

    auto update_camera_buffer(erhe::scene::ICamera& camera,
                              erhe::scene::Viewport viewport)
    -> Buffer_range;

    auto update_draw_indirect_buffer(const Mesh_collection&                    meshes,
                                     erhe::primitive::Primitive_geometry::Mode mode,
                                     uint64_t                                  visibility_mask = erhe::scene::Mesh::c_visibility_all)
    -> Draw_indirect_buffer_range;

    auto programs() -> const std::shared_ptr<Programs>&
    {
        return m_programs;
    }

    void bind_material_buffer();

    void bind_light_buffer();

    void bind_camera_buffer();

    void bind_primitive_buffer();

    void bind_draw_indirect_buffer();

    void reset_id_ranges();

    auto id_offset() const -> uint32_t
    {
        return m_id_offset;
    }

    auto id_ranges() const -> const std::vector<Id_range>&
    {
        return m_id_ranges;
    } 

    void debug_properties_window();

    auto max_index_count_enable() const -> bool
    {
        return m_max_index_count_enable;
    }

    auto max_index_count() const -> int
    {
        return m_max_index_count;
    }

protected:
    struct Buffer_writer
    {
        Buffer_range range;
        size_t       write_offset{0};

        void align()
        {
            while (write_offset % erhe::graphics::Configuration::implementation_defined.uniform_buffer_offset_alignment)
            {
                write_offset++;
            }
        }

        void begin()
        {
            align();
            range.first_byte_offset = write_offset;
        }

        void end()
        {
            range.byte_count = write_offset - range.first_byte_offset;
        }

        void reset()
        {
            range.first_byte_offset = 0;
            range.byte_count        = 0;
            write_offset            = 0;
        }
    };

private:
    std::shared_ptr<Programs>    m_programs;
    std::vector<Frame_resources> m_frame_resources;
    size_t                       m_current_frame_resource_slot{0};

    Buffer_writer                m_material_writer;
    Buffer_writer                m_light_writer;
    Buffer_writer                m_camera_writer;
    Buffer_writer                m_primitive_writer;
    Buffer_writer                m_draw_indirect_writer;

    uint32_t                     m_id_offset{0};
    std::vector<Id_range>        m_id_ranges;

    bool                         m_max_index_count_enable{false};
    int                          m_max_index_count{256};
};

} // namespace editor
