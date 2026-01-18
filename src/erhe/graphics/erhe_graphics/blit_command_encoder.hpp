#pragma once

#include "erhe_graphics/command_encoder.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace erhe::graphics {

class Buffer;
class Device;
class Render_pass;
class Texture;

class Blit_command_encoder_impl;
class Blit_command_encoder final : public Command_encoder
{
public:
    explicit Blit_command_encoder(Device& device);
    Blit_command_encoder(const Blit_command_encoder&) = delete;
    Blit_command_encoder& operator=(const Blit_command_encoder&) = delete;
    Blit_command_encoder(Blit_command_encoder&&) = delete;
    Blit_command_encoder& operator=(Blit_command_encoder&&) = delete;
    ~Blit_command_encoder() noexcept override;

    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) override;
    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer) override;
    void blit_framebuffer (const Render_pass& source_renderpass, glm::ivec2 source_origin, glm::ivec2 source_size, const Render_pass& destination_renderpass, glm::ivec2 destination_origin);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice,  std::uintptr_t source_level,         glm::ivec3     source_origin,          glm::ivec3 source_size, const Texture* destination_texture, std::uintptr_t destination_slice,  std::uintptr_t destination_level,         glm::ivec3     destination_origin);
    void copy_from_buffer (const Buffer*  source_buffer,  std::uintptr_t source_offset, std::uintptr_t source_bytes_per_row, std::uintptr_t source_bytes_per_image, glm::ivec3 source_size, const Texture* destination_texture, std::uintptr_t destination_slice,  std::uintptr_t destination_level,         glm::ivec3     destination_origin);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice,  std::uintptr_t source_level,         glm::ivec3     source_origin,          glm::ivec3 source_size, const Buffer* destination_buffer,   std::uintptr_t destination_offset, std::uintptr_t destination_bytes_per_row, std::uintptr_t destination_bytes_per_image);
    void generate_mipmaps (const Texture* texture);
    void fill_buffer      (const Buffer*  buffer,         std::uintptr_t offset,       std::uintptr_t length,       uint8_t value);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice, std::uintptr_t source_level, const Texture* destination_texture, std::uintptr_t destination_slice, std::uintptr_t destination_level, std::uintptr_t slice_count, std::uintptr_t level_count);
    void copy_from_texture(const Texture* source_texture, const Texture* destination_texture);
    void copy_from_buffer (const Buffer*  source_buffer,  std::uintptr_t source_offset, const Buffer* destination_buffer, std::uintptr_t destination_offset, std::uintptr_t size);
    // void update_fence     (const Fence*   fence);
    // void wait_for_fence   (const Fence*   fence);

    //void synchronize_resource            (const Resource* resource);
    //void synchronize_texture             (const Texture* texture, std::uintptr_t slice, std::uintptr_t level);

    // void get_texture_access_counters     (const Texture* texture, MTL::Region region, std::uintptr_t mipLevel, std::uintptr_t slice, bool resetCounters, const class Buffer* countersBuffer, std::uintptr_t countersBufferOffset);
    // void reset_texture_access_counters   (const Texture* texture, MTL::Region region, std::uintptr_t mipLevel, std::uintptr_t slice);
    // void optimize_contents_for_gpu_access(const Texture* texture);
    // void optimize_contents_for_gpu_access(const Texture* texture, std::uintptr_t slice, std::uintptr_t level);
    // void optimize_contents_for_cpu_access(const Texture* texture);
    // void optimize_contents_for_cpu_access(const Texture* texture, std::uintptr_t slice, std::uintptr_t level);
    // void reset_commands_in_buffer        (const IndirectCommandBuffer* buffer, NS::Range range);
    // void copy_indirect_command_buffer    (const IndirectCommandBuffer* source, NS::Range sourceRange, const class IndirectCommandBuffer* destination, std::uintptr_t destinationIndex);
    // void optimize_indirect_command_buffer(const IndirectCommandBuffer* indirectCommandBuffer, NS::Range range);
    // void sample_counters_in_buffer       (const CounterSampleBuffer* sampleBuffer, std::uintptr_t sampleIndex, bool barrier);
    // void resolve_counters                (const CounterSampleBuffer* sampleBuffer, NS::Range range, const class Buffer* destination_buffer, std::uintptr_t destination_offset);

private:
    std::unique_ptr<Blit_command_encoder_impl> m_impl;
};

} // namespace erhe::graphics
