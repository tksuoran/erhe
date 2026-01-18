#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

class Texture_impl final
{
public:
    Texture_impl           (const Texture_impl&) = delete;
    Texture_impl& operator=(const Texture_impl&) = delete;
    Texture_impl(Texture_impl&&) noexcept;
    Texture_impl& operator=(Texture_impl&&) = delete;
    ~Texture_impl() noexcept;

    Texture_impl(Device& device, const Texture_create_info& create_info);

    [[nodiscard]] static auto is_array_target       (gl::Texture_target target) -> bool;
    [[nodiscard]] static auto get_storage_dimensions(gl::Texture_target target) -> int;
    [[nodiscard]] static auto get_mipmap_dimensions (gl::Texture_target target) -> int;
    [[nodiscard]] static auto get_mipmap_dimensions (Texture_type type) -> int;

    [[nodiscard]] auto get_debug_label           () const -> const std::string&;
    [[nodiscard]] auto get_pixelformat           () const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_width                 (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_height                (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_depth                 (unsigned int level = 0) const -> int;
    [[nodiscard]] auto get_array_layer_count     () const -> int;
    [[nodiscard]] auto get_level_count           () const -> int;
    [[nodiscard]] auto get_fixed_sample_locations() const -> bool;
    [[nodiscard]] auto get_sample_count          () const -> int;
    [[nodiscard]] auto get_texture_type          () const -> Texture_type;
    [[nodiscard]] auto is_layered                () const -> bool;
    [[nodiscard]] auto gl_name                   () const -> GLuint;
    [[nodiscard]] auto get_handle                () const -> uint64_t;
    [[nodiscard]] auto is_sparse                 () const -> bool;

    [[nodiscard]] auto get_gl_texture_target() const -> gl::Texture_target;


    void clear() const;

private:
    Gl_texture               m_handle;
    Texture_type             m_type                  {Texture_type::texture_2d};
    erhe::dataformat::Format m_pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                     m_fixed_sample_locations{true};
    bool                     m_is_sparse             {false};
    int                      m_sample_count          {0};
    int                      m_width                 {0};
    int                      m_height                {0};
    int                      m_depth                 {0};
    int                      m_array_layer_count     {0};
    int                      m_level_count           {0};
    Buffer*                  m_buffer                {nullptr};
    std::string              m_debug_label;
};

[[nodiscard]] auto gl_name(const Texture& texture) -> GLuint;

// [[nodiscard]] auto get_texture_from_handle(uint64_t handle) -> GLuint;
// [[nodiscard]] auto get_sampler_from_handle(uint64_t handle) -> GLuint;
// 
class Texture_impl_hash
{
public:
    auto operator()(const Texture_impl& texture) const noexcept -> size_t
    {
        ERHE_VERIFY(texture.gl_name() != 0);

        return static_cast<size_t>(texture.gl_name());
    }
};

[[nodiscard]] auto operator==(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Texture_impl& lhs, const Texture_impl& rhs) noexcept -> bool;
 
void convert_texture_dimensions_from_gl(gl::Texture_target target, int& width, int& height, int& depth, int& array_layer_count);
void convert_texture_dimensions_to_gl  (gl::Texture_target target, int& width, int& height, int& depth, int array_layer_count);
void convert_texture_offset_to_gl      (gl::Texture_target target, int& x, int& y, int& z, int array_layer);


[[nodiscard]] auto component_count             (gl::Pixel_format pixel_format) -> size_t;
[[nodiscard]] auto byte_count                  (gl::Pixel_type pixel_type) -> size_t;
[[nodiscard]] auto get_gl_pixel_byte_count     (erhe::dataformat::Format pixelformat) -> size_t;
[[nodiscard]] auto get_format_and_type         (erhe::dataformat::Format pixelformat, gl::Pixel_format& format, gl::Pixel_type& type) -> bool;
[[nodiscard]] auto convert_to_gl_texture_target(Texture_type type, bool multisample, bool array) -> gl::Texture_target;


} // namespace erhe::graphics
