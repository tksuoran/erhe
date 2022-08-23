#pragma once

#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/gl_objects.hpp"

#include <glm/glm.hpp>

#include <gsl/span>

#include <optional>
#include <string_view>

namespace erhe::graphics
{

class Buffer;
class Sampler;

class Texture_create_info
{
public:
    [[nodiscard]] static auto calculate_level_count(
        const int width,
        const int height = 0,
        const int depth = 0
    ) -> int;

    [[nodiscard]] auto calculate_level_count() const -> int;

    gl::Texture_target  target                {gl::Texture_target::texture_2d};
    gl::Internal_format internal_format       {gl::Internal_format::rgba8};
    bool                use_mipmaps           {false};
    bool                fixed_sample_locations{true};
    bool                sparse                {false};
    int                 sample_count          {0};
    int                 width                 {1};
    int                 height                {1};
    int                 depth                 {1};
    int                 level_count           {0};
    int                 row_stride            {0};
    Buffer*             buffer                {nullptr};
    GLuint              wrap_texture_name     {0};
};

class Texture
{
public:
    using Create_info = Texture_create_info;

    static auto storage_dimensions(const gl::Texture_target target) -> int;
    static auto mipmap_dimensions (const gl::Texture_target target) -> int;
    static auto size_level_count  (int size) -> int;

    explicit Texture(const Create_info& create_info);
    ~Texture        () noexcept;

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
    void upload_subimage(
        const gl::Internal_format        internal_format,
        const gsl::span<const std::byte> data,
        const int                        src_row_length,
        const int                        src_x,
        const int                        src_y,
        const int                        width,
        const int                        height,
        const int                        level,
        const int                        x,
        const int                        y,
        const int                        z = 0
    );

    void set_debug_label(const std::string& value);

    [[nodiscard]] auto debug_label         () const -> const std::string&;
    [[nodiscard]] auto width               () const -> int;
    [[nodiscard]] auto height              () const -> int;
    [[nodiscard]] auto internal_format     () const -> gl::Internal_format;
    [[nodiscard]] auto depth               () const -> int;
    [[nodiscard]] auto sample_count        () const -> int;
    [[nodiscard]] auto target              () const -> gl::Texture_target;
    [[nodiscard]] auto is_layered          () const -> bool;
    [[nodiscard]] auto gl_name             () const -> GLuint;
    [[nodiscard]] auto get_handle          () const -> uint64_t;
    [[nodiscard]] auto is_sparse           () const -> bool;
    [[nodiscard]] auto get_sparse_tile_size() const -> Tile_size;

private:
    Gl_texture          m_handle;
    std::string         m_debug_label;
    gl::Texture_target  m_target                {gl::Texture_target::texture_2d};
    gl::Internal_format m_internal_format       {gl::Internal_format::rgba8};
    bool                m_fixed_sample_locations{true};
    bool                m_is_sparse             {false};
    int                 m_sample_count          {0};
    int                 m_level_count           {0};
    int                 m_width                 {0};
    int                 m_height                {0};
    int                 m_depth                 {0};
    Buffer*             m_buffer                {nullptr};
};

[[nodiscard]] auto get_handle(
    const Texture& texture,
    const Sampler& sampler
) -> uint64_t;

[[nodiscard]] auto get_texture_from_handle(uint64_t handle) -> GLuint;
[[nodiscard]] auto get_sampler_from_handle(uint64_t handle) -> GLuint;

class Texture_hash
{
public:
    auto operator()(const Texture& texture) const noexcept -> size_t
    {
        Expects(texture.gl_name() != 0);

        return static_cast<size_t>(texture.gl_name());
    }
};

[[nodiscard]] auto operator==(const Texture& lhs, const Texture& rhs) noexcept -> bool;

[[nodiscard]] auto operator!=(const Texture& lhs, const Texture& rhs) noexcept -> bool;

[[nodiscard]] auto component_count(gl::Pixel_format pixel_format) -> size_t;

[[nodiscard]] auto byte_count(gl::Pixel_type pixel_type) -> size_t;

[[nodiscard]] auto get_upload_pixel_byte_count(gl::Internal_format internalformat) -> size_t;

[[nodiscard]] auto get_format_and_type(
    gl::Internal_format internalformat,
    gl::Pixel_format&   format,
    gl::Pixel_type&     type
) -> bool;

[[nodiscard]] auto create_dummy_texture() -> std::shared_ptr<Texture>;

// For non-bindless textures
class Texture_unit_cache
{
public:
    Texture_unit_cache();

    void reset(unsigned int base_texture_unit);
    auto allocate_texture_unit(uint64_t handle) -> std::optional<std::size_t>;

    auto bind( uint64_t fallback_handle) -> std::size_t;

private:
    unsigned int          m_base_texture_unit;
    std::vector<uint64_t> m_texture_units;
};

extern Texture_unit_cache s_texture_unit_cache;

} // namespace erhe::graphics
