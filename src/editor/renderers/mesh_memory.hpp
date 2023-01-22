#pragma once

#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/primitive/build_info.hpp"
#include "erhe/primitive/enums.hpp"

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Vertex_format;
    class Vertex_input_state;
}

namespace erhe::primitive
{
    class Gl_buffer_sink;
}

namespace editor
{

class Mesh_memory
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Mesh_memory"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Mesh_memory ();
    ~Mesh_memory() noexcept override;

    // Implements Component
    auto get_type_hash              () const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

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

extern Mesh_memory* g_mesh_memory;

} // namespace editor
