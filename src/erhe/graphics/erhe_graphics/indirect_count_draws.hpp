#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {

class Buffer;
class Device;
class Indirect_count_draws;
class Indirect_count_draws_impl;

// GPU-counted multi-draws prepared in a compute pass and drawn in a later
// render pass (doc/erhe/graphics.md "Indirect count draws").
//
// A producer writes, per job, up to max_draw_count
// Draw_indexed_primitives_indirect_command records (stride 20 bytes) starting
// at the job's command_offset in a command buffer, and the number it wrote
// into a count buffer. Compute_command_encoder::
// prepare_indexed_indirect_count_draws() declares those buffers and a set of
// jobs, after the producer's dispatches, and Render_command_encoder::
// multi_draw_indexed_primitives_indirect_count(set, job) then draws one job
// of the set with the GPU-read count. A frame may prepare several sets (one
// per render pass, say); each prepare replaces the previous set's state, so
// a set is drawn before the next prepare on the same Indirect_count_draws
// (the draw verifies it is the most recent one). On Vulkan and OpenGL the
// draw is the native count draw and the prepare step only records the
// parameters; on Metal the prepare
// step dispatches a kernel that encodes each job's surviving commands into an
// indirect command buffer, and the draw executes the job's range of it.
// Needs Device_info::use_draw_indirect_count.

// An index buffer the prepared jobs draw from (each job names one by index).
// The commands' first_index addresses it from byte 0.
class Indirect_count_index_buffer
{
public:
    const Buffer*            buffer    {nullptr};
    erhe::dataformat::Format index_type{erhe::dataformat::Format::format_32_scalar_uint};
};

// One job: max_draw_count command slots at command_offset (in commands, from
// the command buffer's command 0) and the job's count at
// count_byte_offset + (job index * count_byte_stride). The jobs' command
// ranges [command_offset, command_offset + max_draw_count) do not overlap.
class Indirect_count_draw_job
{
public:
    uint32_t command_offset{0};
    uint32_t max_draw_count{0};
    uint32_t index_buffer  {0}; // index into Indirect_count_draws_prepare_info::index_buffers
};

class Indirect_count_draws_prepare_info
{
public:
    // The device frame the draws are recorded for (Device::get_frame_index()
    // of the recording role); selects the frame-in-flight slot whose
    // resources the prepare step writes. Non-decreasing across prepares.
    uint64_t                                     frame_index        {0};
    Primitive_type                               primitive_type     {Primitive_type::triangle};
    std::span<const Indirect_count_index_buffer> index_buffers      {};
    // The commands; command 0 at command_byte_offset (a multiple of 4).
    const Buffer*                                command_buffer     {nullptr};
    std::size_t                                  command_byte_offset{0};
    // The per-job uint32 counts (offset and stride multiples of 4); the
    // count is clamped to the job's max_draw_count.
    const Buffer*                                count_buffer       {nullptr};
    std::size_t                                  count_byte_offset  {0};
    std::size_t                                  count_byte_stride  {sizeof(uint32_t)};
    std::span<const Indirect_count_draw_job>     jobs               {};
};

// What prepare_indexed_indirect_count_draws() returns: names the prepared
// frame-in-flight slot of an Indirect_count_draws and the prepare's sequence
// number. Valid until the next prepare on the same Indirect_count_draws (the
// draw verifies it is the slot's most recent prepare).
class Indirect_count_draw_set
{
public:
    const Indirect_count_draws* draws   {nullptr};
    std::size_t                 slot    {0};
    uint64_t                    sequence{0};
};

// Owns the per-frame-in-flight state of the prepared draws (on Metal the
// indirect command buffer, the execution range buffer and the kernel's
// tables, each sized to its high-water mark). Persistent: create one per
// producer, not per frame.
class Indirect_count_draws final
{
public:
    // The parameters of the slot's most recent prepare, as the draw needs
    // them on every backend (copies; capacity kept across prepares).
    class Slot
    {
    public:
        uint64_t                                 frame_index        {0};
        // The prepare's number among all prepares of this
        // Indirect_count_draws (1 on); 0: never prepared.
        uint64_t                                 sequence           {0};
        Primitive_type                           primitive_type     {Primitive_type::triangle};
        std::vector<Indirect_count_index_buffer> index_buffers;
        const Buffer*                            command_buffer     {nullptr};
        std::size_t                              command_byte_offset{0};
        const Buffer*                            count_buffer       {nullptr};
        std::size_t                              count_byte_offset  {0};
        std::size_t                              count_byte_stride  {0};
        std::vector<Indirect_count_draw_job>     jobs;
    };

    Indirect_count_draws(Device& device, erhe::utility::Debug_label debug_label);
    ~Indirect_count_draws() noexcept;
    Indirect_count_draws(const Indirect_count_draws&) = delete;
    Indirect_count_draws& operator=(const Indirect_count_draws&) = delete;
    Indirect_count_draws(Indirect_count_draws&&) = delete;
    Indirect_count_draws& operator=(Indirect_count_draws&&) = delete;

    // Copies the prepare info into the frame's slot and returns the set
    // naming it and the prepare (called by Compute_command_encoder::
    // prepare_indexed_indirect_count_draws()).
    [[nodiscard]] auto store          (const Indirect_count_draws_prepare_info& prepare_info) -> Indirect_count_draw_set;
    [[nodiscard]] auto get_slot       (std::size_t slot) const -> const Slot&;
    // The number of the most recent prepare (0: none yet).
    [[nodiscard]] auto get_sequence   () const -> uint64_t;
    [[nodiscard]] auto get_device     () const -> Device&;
    [[nodiscard]] auto get_debug_label() const -> const erhe::utility::Debug_label&;
    [[nodiscard]] auto get_impl       () -> Indirect_count_draws_impl&;
    [[nodiscard]] auto get_impl       () const -> const Indirect_count_draws_impl&;

private:
    Device&                                    m_device;
    erhe::utility::Debug_label                 m_debug_label;
    std::vector<Slot>                          m_slots;
    uint64_t                                   m_sequence   {0};
    uint64_t                                   m_frame_index{0};
    std::unique_ptr<Indirect_count_draws_impl> m_impl;
};

} // namespace erhe::graphics
