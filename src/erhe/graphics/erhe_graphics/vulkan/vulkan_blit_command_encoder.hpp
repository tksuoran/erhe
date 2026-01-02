#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

class Blit_command_encoder_impl final
{
public:
    explicit Blit_command_encoder_impl(Device& device);
    Blit_command_encoder_impl(const Blit_command_encoder_impl&) = delete;
    Blit_command_encoder_impl& operator=(const Blit_command_encoder_impl&) = delete;
    Blit_command_encoder_impl(Blit_command_encoder_impl&&) = delete;
    Blit_command_encoder_impl& operator=(Blit_command_encoder_impl&&) = delete;
    ~Blit_command_encoder_impl() noexcept;

    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer       (Buffer_target buffer_target, const Buffer* buffer);
    void blit_framebuffer (const Render_pass& source_renderpass, glm::ivec2 source_origin, glm::ivec2 source_size, const Render_pass& destination_renderpass, glm::ivec2 destination_origin);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice,  std::uintptr_t source_level,         glm::ivec3     source_origin,          glm::ivec3 source_size, const Texture* destination_texture, std::uintptr_t destination_slice,  std::uintptr_t destination_level,         glm::ivec3     destination_origin);
    void copy_from_buffer (const Buffer*  source_buffer,  std::uintptr_t source_offset, std::uintptr_t source_bytes_per_row, std::uintptr_t source_bytes_per_image, glm::ivec3 source_size, const Texture* destination_texture, std::uintptr_t destination_slice,  std::uintptr_t destination_level,         glm::ivec3     destination_origin);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice,  std::uintptr_t source_level,         glm::ivec3     source_origin,          glm::ivec3 source_size, const Buffer* destination_buffer,   std::uintptr_t destination_offset, std::uintptr_t destination_bytes_per_row, std::uintptr_t destination_bytes_per_image);
    void generate_mipmaps (const Texture* texture);
    void fill_buffer      (const Buffer*  buffer,         std::uintptr_t offset,       std::uintptr_t length,       uint8_t value);
    void copy_from_texture(const Texture* source_texture, std::uintptr_t source_slice, std::uintptr_t source_level, const Texture* destination_texture, std::uintptr_t destination_slice, std::uintptr_t destination_level, std::uintptr_t slice_count, std::uintptr_t level_count);
    void copy_from_texture(const Texture* source_texture, const Texture* destination_texture);
    void copy_from_buffer (const Buffer*  source_buffer,  std::uintptr_t source_offset, const Buffer* destination_buffer, std::uintptr_t destination_offset, std::uintptr_t size);

private:
    Device& m_device;
};

} // namespace erhe::graphics