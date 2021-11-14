#pragma once

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/span>

#include <string_view>

namespace erhe::graphics
{

class Buffer;


class Texture_create_info
{
public:
    Texture_create_info() = default;

    Texture_create_info(
        const gl::Texture_target  target,
        const gl::Internal_format internal_format,
        const bool                use_mipmaps,
        const int                 width,
        const int                 height = 1,
        const int                 depth  = 1
    );

    void calculate_level_count();

    gl::Texture_target  target                {gl::Texture_target::texture_2d};
    gl::Internal_format internal_format       {gl::Internal_format::rgba8};
    bool                use_mipmaps           {false};
    bool                fixed_sample_locations{true};
    int                 sample_count          {0};
    int                 width                 {1};
    int                 height                {1};
    int                 depth                 {1};
    int                 level_count           {1};
    int                 row_stride            {0};
    Buffer*             buffer                {nullptr};
    GLuint              wrap_texture_name     {0};
};

class Texture
{
public:
    using Create_info = Texture_create_info;

    static auto storage_dimensions(gl::Texture_target target) -> int;
    static auto mipmap_dimensions (gl::Texture_target target) -> int;
    static auto size_level_count  (int size) -> int;

    explicit Texture(const Create_info& create_info);
    ~Texture        ();
    Texture         (const Texture&) = delete;
    void operator=  (const Texture&) = delete;
    Texture         (Texture&& other) noexcept;
    auto operator=  (Texture&& other) noexcept -> Texture&;

    void upload(
        const gl::Internal_format internal_format,
        const int                 width,
        const int                 height = 1,
        const int                 depth = 1
    );

    void upload(
        const gl::Internal_format        internal_format,
        const gsl::span<const std::byte> data,
        const int                        width,
        const int                        height = 1,
        const int                        depth = 1,
        const int                        level = 0,
        const int                        x = 0,
        const int                        y = 0,
        const int                        z = 0
    );

    void set_debug_label(std::string_view value);
    auto debug_label    () const -> const std::string&;
    auto width          () const -> int;
    auto height         () const -> int;
    auto depth          () const -> int;
    auto sample_count   () const -> int;
    auto target         () const -> gl::Texture_target;
    auto is_layered     () const -> bool;
    auto gl_name        () const -> GLuint;

private:
    Gl_texture          m_handle;
    std::string         m_debug_label;
    gl::Texture_target  m_target                {gl::Texture_target::texture_2d};
    gl::Internal_format m_internal_format       {gl::Internal_format::rgba8};
    bool                m_fixed_sample_locations{true};
    int                 m_sample_count          {0};
    int                 m_level_count           {0};
    int                 m_width                 {0};
    int                 m_height                {0};
    int                 m_depth                 {0};
    Buffer*             m_buffer                {nullptr};
};

class Texture_hash
{
public:
    auto operator()(const Texture& texture) const noexcept -> size_t
    {
        Expects(texture.gl_name() != 0);

        return static_cast<size_t>(texture.gl_name());
    }
};

auto operator==(const Texture& lhs, const Texture& rhs) noexcept -> bool;

auto operator!=(const Texture& lhs, const Texture& rhs) noexcept -> bool;

auto component_count(gl::Pixel_format pixel_format) -> size_t;

auto byte_count(gl::Pixel_type pixel_type) -> size_t;

auto get_upload_pixel_byte_count(gl::Internal_format internalformat) -> size_t;

auto get_format_and_type(
    gl::Internal_format internalformat,
    gl::Pixel_format&   format,
    gl::Pixel_type&     type
) -> bool;

} // namespace erhe::graphics
