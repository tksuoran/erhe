#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_item/item.hpp"

#include <span>

#include <string>

namespace erhe::graphics {

class Buffer;
class Instance;
class Sampler;
class Texture;

class Texture_create_info
{
public:
    [[nodiscard]] static auto calculate_level_count(int width, int height = 0, int depth = 0) -> int;

    [[nodiscard]] auto calculate_level_count() const -> int;

    static auto make_view(Instance& instance, const std::shared_ptr<Texture>& view_source) -> Texture_create_info;

    Instance&                instance;
    gl::Texture_target       target                {gl::Texture_target::texture_2d};
    gl::Internal_format      internal_format       {gl::Internal_format::rgba8};
    bool                     use_mipmaps           {false};
    bool                     fixed_sample_locations{true};
    bool                     sparse                {false};
    int                      sample_count          {0};
    int                      width                 {1};
    int                      height                {1};
    int                      depth                 {1};
    int                      level_count           {0};
    int                      row_stride            {0};
    Buffer*                  buffer                {nullptr};
    GLuint                   wrap_texture_name     {0};
    std::string              debug_label           {};
    std::shared_ptr<Texture> view_source           {};
    int                      view_min_level        {0};
    int                      view_min_layer        {0};
    // TODO layer_count?
};

class Texture : public erhe::Item<erhe::Item_base, erhe::Item_base, Texture, erhe::Item_kind::not_clonable>
{
public:
    Texture           (const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) = delete;
    ~Texture() noexcept override;

    Texture(Instance& instance, const Texture_create_info& create_info);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    using Create_info = Texture_create_info;

    static auto storage_dimensions(gl::Texture_target target) -> int;
    static auto mipmap_dimensions (gl::Texture_target target) -> int;
    static auto size_level_count  (int size) -> int;

    void upload(gl::Internal_format internal_format, int width, int height = 1, int depth = 1);

    void upload(
        gl::Internal_format                 internal_format,
        const std::span<const std::uint8_t> data,
        int                                 width,
        int                                 height = 1,
        int                                 depth = 1,
        int                                 level = 0,
        int                                 x = 0,
        int                                 y = 0,
        int                                 z = 0
    );
    void upload_subimage(
        gl::Internal_format                 internal_format,
        const std::span<const std::uint8_t> data,
        int                                 src_row_length,
        int                                 src_x,
        int                                 src_y,
        int                                 width,
        int                                 height,
        int                                 level,
        int                                 x,
        int                                 y,
        int                                 z = 0
    );

    void set_debug_label(std::string_view value);

    [[nodiscard]] auto debug_label           () const -> const std::string&;
    [[nodiscard]] auto width                 () const -> int;
    [[nodiscard]] auto height                () const -> int;
    [[nodiscard]] auto internal_format       () const -> gl::Internal_format;
    [[nodiscard]] auto depth                 () const -> int;
    [[nodiscard]] auto level_count           () const -> int;
    [[nodiscard]] auto fixed_sample_locations() const -> int;
    [[nodiscard]] auto sample_count          () const -> int;
    [[nodiscard]] auto target                () const -> gl::Texture_target;
    [[nodiscard]] auto is_layered            () const -> bool;
    [[nodiscard]] auto gl_name               () const -> GLuint;
    [[nodiscard]] auto get_handle            () const -> uint64_t;
    [[nodiscard]] auto is_sparse             () const -> bool;
    //// [[nodiscard]] auto get_sparse_tile_size() const -> Tile_size;

    [[nodiscard]] auto is_shown_in_ui() const -> bool;

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

[[nodiscard]] auto get_texture_from_handle(uint64_t handle) -> GLuint;
[[nodiscard]] auto get_sampler_from_handle(uint64_t handle) -> GLuint;

class Texture_hash
{
public:
    auto operator()(const Texture& texture) const noexcept -> size_t
    {
        ERHE_VERIFY(texture.gl_name() != 0);

        return static_cast<size_t>(texture.gl_name());
    }
};

[[nodiscard]] auto operator==(const Texture& lhs, const Texture& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Texture& lhs, const Texture& rhs) noexcept -> bool;

[[nodiscard]] auto component_count(gl::Pixel_format pixel_format) -> size_t;
[[nodiscard]] auto byte_count(gl::Pixel_type pixel_type) -> size_t;
[[nodiscard]] auto get_upload_pixel_byte_count(gl::Internal_format internalformat) -> size_t;
[[nodiscard]] auto get_format_and_type(gl::Internal_format internalformat, gl::Pixel_format& format, gl::Pixel_type& type) -> bool;
[[nodiscard]] auto format_texture_handle(uint64_t handle) -> std::string;

constexpr uint64_t invalid_texture_handle = 0xffffffffu;


} // namespace erhe::graphics
