#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/enums.hpp"

#include <cstdint>

namespace erhe::graphics {

class Buffer;
class Device;
class Render_pass;
class Render_pipeline_state;

class Render_command_encoder final : public Command_encoder
{
public:
    Render_command_encoder(Device& device, Render_pass& render_pass);
    Render_command_encoder(const Render_command_encoder&) = delete;
    Render_command_encoder& operator=(const Render_command_encoder&) = delete;
    Render_command_encoder(Render_command_encoder&&) = delete;
    Render_command_encoder& operator=(Render_command_encoder&&) = delete;
    ~Render_command_encoder() override;

    void set_render_pipeline_state(const Render_pipeline_state& pipeline);

    void set_index_buffer (const Buffer* buffer);
    void set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index);

    void draw_primitives(Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count, std::uintptr_t instance_count);
    void draw_primitives(Primitive_type primitive_type, std::uintptr_t vertex_start, std::uintptr_t vertex_count);
    void draw_indexed_primitives(Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset, std::uintptr_t instance_count);
    void draw_indexed_primitives(Primitive_type primitive_type, std::uintptr_t index_count, erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset);

    void multi_draw_indexed_primitives_indirect(
        Primitive_type           primitive_type,
        erhe::dataformat::Format index_type,
        std::uintptr_t           indirect_offset,
        std::uintptr_t           drawcount,
        std::uintptr_t           stride
    );

private:
    void start_render_pass();
    void end_render_pass();

    Render_pass& m_render_pass;
};

} // namespace erhe::graphics
