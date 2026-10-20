#pragma once

#include "erhe_graphics/command_encoder.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;
class Render_pass;
class Texture;

// One subresource of a texture and a texel position in it: the source or
// destination of a copy that involves a texture.
class Texture_location
{
public:
    const Texture* texture{nullptr};
    std::uintptr_t slice  {0};
    std::uintptr_t level  {0};
    glm::ivec3     origin {0, 0, 0};
};

// Texel data in a buffer: the source or destination of a copy between a
// buffer and a texture. bytes_per_row / bytes_per_image give the row and
// image pitch of the data in the buffer.
class Buffer_texel_location
{
public:
    const Buffer*  buffer         {nullptr};
    std::uintptr_t offset         {0};
    std::uintptr_t bytes_per_row  {0};
    std::uintptr_t bytes_per_image{0};
};

// The texel rectangle a Blit_command_encoder::blit_framebuffer copies, after
// clipping: size texels from source_origin to destination_origin. size is
// (0, 0) when nothing of the requested rectangle lies inside both
// attachments.
class Blit_framebuffer_region
{
public:
    glm::ivec2 source_origin     {0, 0};
    glm::ivec2 destination_origin{0, 0};
    glm::ivec2 size              {0, 0};
};

// Clips the unscaled copy of source_size texels at source_origin to
// destination_origin against both attachments (source_extent and
// destination_extent texels, origin at 0), moving both origins together so
// the source texel at source_origin + d still lands at destination_origin + d.
[[nodiscard]] auto clip_blit_framebuffer_region(
    glm::ivec2 source_origin,
    glm::ivec2 source_size,
    glm::ivec2 source_extent,
    glm::ivec2 destination_origin,
    glm::ivec2 destination_extent
) -> Blit_framebuffer_region;

class Blit_command_encoder_impl;
class Blit_command_encoder final : public Command_encoder
{
public:
    Blit_command_encoder(Device& device, Command_buffer& command_buffer);
    Blit_command_encoder(const Blit_command_encoder&) = delete;
    Blit_command_encoder& operator=(const Blit_command_encoder&) = delete;
    Blit_command_encoder(Blit_command_encoder&&) = delete;
    Blit_command_encoder& operator=(Blit_command_encoder&&) = delete;
    ~Blit_command_encoder() noexcept override;

    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) override;
    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer) override;
    // Copies color attachment 0 of source_renderpass to color attachment 0
    // of destination_renderpass (the attachment's level and layer; the
    // current window image for a swapchain render pass): source_size texels
    // at source_origin land at destination_origin, unscaled, nearest, with a
    // format conversion between the two attachment formats, clipped to both
    // attachments (clip_blit_framebuffer_region). Origins are texel
    // coordinates of the attachments, row 0 first in memory. Neither render
    // pass may be active. Vulkan does not support a swapchain render pass
    // here (doc/plans/xr.md "Desktop mirror").
    void blit_framebuffer (const Render_pass& source_renderpass, glm::ivec2 source_origin, glm::ivec2 source_size, const Render_pass& destination_renderpass, glm::ivec2 destination_origin);
    void copy_from_texture(const Texture_location&      source, glm::ivec3 source_size, const Texture_location&      destination);
    void copy_from_buffer (const Buffer_texel_location& source, glm::ivec3 source_size, const Texture_location&      destination);
    void copy_from_texture(const Texture_location&      source, glm::ivec3 source_size, const Buffer_texel_location& destination);
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
    erhe::utility::pimpl_ptr<Blit_command_encoder_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
