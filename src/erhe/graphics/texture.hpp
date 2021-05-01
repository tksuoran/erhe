#ifndef texture_hpp_erhe_graphics
#define texture_hpp_erhe_graphics

#include "erhe/graphics/gl_objects.hpp"

#include <gsl/span>

namespace erhe::graphics
{

class Buffer;

class Texture
{
public:
    struct Create_info
    {
        Create_info() = default;

        Create_info(gl::Texture_target  target,
                    gl::Internal_format internal_format,
                    bool                use_mipmaps,
                    int                 width,
                    int                 height = 1,
                    int                 depth  = 1);

        void calculate_level_count();

        gl::Texture_target  target         {gl::Texture_target::texture_2d};
        gl::Internal_format internal_format{gl::Internal_format::rgba8};
        bool                use_mipmaps    {false};
        int                 sample_count   {0};
        int                 width          {1};
        int                 height         {1};
        int                 depth          {1};
        int                 level_count    {1};
        int                 row_stride     {0};
        Buffer*             buffer         {nullptr};
    };

    static auto storage_dimensions(gl::Texture_target target)
    -> int;

    static auto mipmap_dimensions(gl::Texture_target target)
    -> int;

    static auto size_level_count(int size)
    -> int;

    ~Texture() = default;

    explicit Texture(const Create_info& create_info);

    Texture(const Texture&) = delete;

    auto operator=(const Texture&)
    -> Texture& = delete;

    Texture(Texture&& other) noexcept
        : m_handle         {std::move(other.m_handle)}
        , m_debug_label    {std::move(other.m_debug_label)}
        , m_target         {other.m_target}
        , m_internal_format{other.m_internal_format}
        , m_sample_count   {other.m_sample_count}
        , m_level_count    {other.m_level_count}
        , m_width          {other.m_width}
        , m_height         {other.m_height}
        , m_depth          {other.m_depth}
        , m_buffer         {other.m_buffer}
    {
    }

    auto operator=(Texture&& other) noexcept
    -> Texture&
    {
        m_handle          = std::move(other.m_handle);
        m_debug_label     = other.m_debug_label;
        m_target          = other.m_target;
        m_internal_format = other.m_internal_format;
        m_sample_count    = other.m_sample_count;
        m_level_count     = other.m_level_count;
        m_width           = other.m_width;
        m_height          = other.m_height;
        m_depth           = other.m_depth;
        m_buffer          = other.m_buffer;
        return *this;
    }

    void upload(gl::Internal_format internal_format, int width, int height = 0, int depth = 0);

    void upload(gl::Internal_format internal_format, gsl::span<const std::byte> data, int width, int height = 0, int depth = 0);

    void set_debug_label(std::string value);

    auto debug_label() const
    -> const std::string&;

    auto width() const
    -> int;

    auto height() const
    -> int;

    auto depth() const
    -> int;

    auto sample_count() const
    -> int;

    auto target() const
    -> gl::Texture_target;

    auto is_layered() const
    -> bool;

    auto gl_name() const
    -> GLuint;

private:
    Gl_texture          m_handle;
    std::string         m_debug_label;
    gl::Texture_target  m_target         {gl::Texture_target::texture_2d};
    gl::Internal_format m_internal_format{gl::Internal_format::rgba8};
    int                 m_sample_count   {0};
    int                 m_level_count    {0};
    int                 m_width          {0};
    int                 m_height         {0};
    int                 m_depth          {0};
    Buffer*             m_buffer         {nullptr};
};

struct Texture_hash
{
    auto operator()(const Texture& texture) const noexcept
    -> size_t
    {
        Expects(texture.gl_name() != 0);

        return static_cast<size_t>(texture.gl_name());
    }
};

auto operator==(const Texture& lhs, const Texture& rhs) noexcept
-> bool;

auto operator!=(const Texture& lhs, const Texture& rhs) noexcept
-> bool;

auto component_count(gl::Pixel_format pixel_format)
-> size_t;

auto byte_count(gl::Pixel_type pixel_type)
-> size_t;

auto get_upload_pixel_byte_count(gl::Internal_format internalformat)
-> size_t;

auto get_format_and_type(gl::Internal_format internalformat,
                         gl::Pixel_format&   format,
                         gl::Pixel_type&     type)
-> bool;

} // namespace erhe::graphics

#endif
