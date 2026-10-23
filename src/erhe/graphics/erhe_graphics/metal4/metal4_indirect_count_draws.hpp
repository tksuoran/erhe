#pragma once

#include "erhe_graphics/indirect_count_draws.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MTL { class Buffer; }
namespace MTL { class ComputeCommandEncoder; }
namespace MTL { class ComputePipelineState; }
namespace MTL { class Device; }
namespace MTL { class IndirectCommandBuffer; }

namespace erhe::graphics {

// Compiles the kernel that encodes prepared count draws into an indirect
// command buffer (raw MSL: SPIR-V cannot express command_buffer). Null when
// the library or the pipeline fails; the device then reports no
// use_draw_indirect_count.
[[nodiscard]] auto make_indirect_count_encode_pipeline(MTL::Device* device) -> MTL::ComputePipelineState*;

// Metal half of Indirect_count_draws: per frame-in-flight slot, the indirect
// command buffer the kernel encodes the jobs' commands into, the buffer of
// per-job execution ranges {location, length} the kernel writes, and the
// table buffer the CPU writes (the ICB's resource id, the index buffer
// addresses, the jobs). A frame may prepare several sets: the ICB and the
// range buffer are rewritten by each prepare's kernel (the GPU orders that
// after the previous set's draws: both are tracked resources, and the
// command buffer's inter-encoder fence orders the encoders), while each
// prepare's table takes its own range of the table buffer, since the CPU
// writes the tables of a frame before the GPU runs any of them. Each grows
// to its high-water mark (the ICB and range buffer: the largest set; the
// table buffer: a frame's sets) and is re-created only on growth.
class Indirect_count_draws_impl final
{
public:
    class Slot_resources
    {
    public:
        MTL::IndirectCommandBuffer* icb           {nullptr};
        std::size_t                 icb_capacity  {0}; // commands
        MTL::Buffer*                range_buffer  {nullptr};
        std::size_t                 range_capacity{0}; // jobs
        MTL::Buffer*                table_buffer  {nullptr};
        std::size_t                 table_capacity{0}; // bytes
        uint64_t                    table_frame   {0}; // the frame whose tables table_used counts
        std::size_t                 table_used    {0}; // bytes
        uint64_t                    encoded_sequence{0}; // the set the ICB and range buffer hold
    };

    Indirect_count_draws_impl(Device& device, const erhe::utility::Debug_label& debug_label);
    ~Indirect_count_draws_impl() noexcept;
    Indirect_count_draws_impl(const Indirect_count_draws_impl&) = delete;
    Indirect_count_draws_impl& operator=(const Indirect_count_draws_impl&) = delete;
    Indirect_count_draws_impl(Indirect_count_draws_impl&&) = delete;
    Indirect_count_draws_impl& operator=(Indirect_count_draws_impl&&) = delete;

    // Records the encoding dispatch of one prepared slot into encoder
    // (Compute_command_encoder_impl::encode_indirect_count_draws).
    void encode(MTL::ComputeCommandEncoder* encoder, const Indirect_count_draws::Slot& slot, std::size_t slot_index);

    [[nodiscard]] auto get_slot_resources(std::size_t slot_index) const -> const Slot_resources&;

private:
    void reserve(Slot_resources& resources, std::size_t command_count, std::size_t job_count);
    // The byte offset of table_bytes in the slot's table buffer for a
    // prepare of frame_index.
    [[nodiscard]] auto allocate_table(Slot_resources& resources, uint64_t frame_index, std::size_t table_bytes) -> std::size_t;

    Device&                     m_device;
    erhe::utility::Debug_label  m_debug_label;
    std::vector<Slot_resources> m_slots;
};

} // namespace erhe::graphics
