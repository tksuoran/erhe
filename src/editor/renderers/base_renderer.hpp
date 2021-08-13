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
    class Primitive;
    class Material;
}

namespace erhe::scene
{
    class ICamera;
    class Camera;
    class Layer;
    class Light;
    class Node;
    class Viewport;
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

    static constexpr const char* c_primitive_color_source_strings[] =
    {
        "ID Offset",
        "Mesh Wireframe color",
        "Constant Color"
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

    class Buffer_range
    {
    public:
        size_t first_byte_offset{0};
        size_t byte_count       {0};
    };

    class Draw_indirect_buffer_range
    {
    public:
        Buffer_range range;
        size_t       draw_indirect_count{0};
    };

    class Id_range
    {
    public:
        uint32_t                           offset         {0};
        uint32_t                           length         {0};
        std::shared_ptr<erhe::scene::Mesh> mesh           {nullptr};
        size_t                             primitive_index{0};
    };

    using Layer_collection    = std::vector<std::shared_ptr<erhe::scene::Layer>>;
    using Light_collection    = std::vector<std::shared_ptr<erhe::scene::Light>>;
    using Material_collection = std::vector<std::shared_ptr<erhe::primitive::Material>>;
    using Mesh_collection     = std::vector<std::shared_ptr<erhe::scene::Mesh>>;

    Base_renderer();
    virtual ~Base_renderer();

    static constexpr size_t s_frame_resources_count = 4;

    void create_frame_resources(const size_t material_count,
                                const size_t light_count,
                                const size_t camera_count,
                                const size_t primitive_count,
                                const size_t draw_count);

    auto current_frame_resources() -> Frame_resources&;

    void next_frame();

    void base_connect(const erhe::components::Component* component);

    auto update_primitive_buffer(const Mesh_collection& meshes,
                                 const uint64_t         visibility_mask = erhe::scene::Mesh::c_visibility_all)
    -> Buffer_range;

    auto update_light_buffer(const Light_collection&     lights,
                             const erhe::scene::Viewport light_texture_viewport,
                             const glm::vec4             ambient_light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f))
    -> Buffer_range;

    auto update_material_buffer(const Material_collection& materials)
    -> Buffer_range;

    auto update_camera_buffer(erhe::scene::ICamera&       camera,
                              const erhe::scene::Viewport viewport)
    -> Buffer_range;

    auto update_draw_indirect_buffer(const Mesh_collection&                meshes,
                                     const erhe::primitive::Primitive_mode primitive_mode,
                                     const uint64_t                        visibility_mask = erhe::scene::Mesh::c_visibility_all)
    -> Draw_indirect_buffer_range;

    void bind_material_buffer     ();
    void bind_light_buffer        ();
    void bind_camera_buffer       ();
    void bind_primitive_buffer    ();
    void bind_draw_indirect_buffer();
    void reset_id_ranges          ();
    auto id_offset                () const -> uint32_t;
    auto id_ranges                () const -> const std::vector<Id_range>&;
    void debug_properties_window  ();
    auto max_index_count_enable   () const -> bool;
    auto max_index_count          () const -> int;

protected:
    class Buffer_writer
    {
    public:
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
    std::shared_ptr<Program_interface> m_program_interface;
    std::vector<Frame_resources>       m_frame_resources;
    size_t                             m_current_frame_resource_slot{0};

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
