#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_item/item.hpp"
#include "erhe_verify/verify.hpp"

#include <span>
#include <string>

namespace erhe::graphics {

class Buffer;
class Device;
class Sampler;
class Texture;

class Texture_create_info
{
public:
    [[nodiscard]] static auto calculate_level_count(int width, int height = 0, int depth = 0) -> int;

    [[nodiscard]] auto calculate_level_count() const -> int;

    static auto make_view(Device& device, const std::shared_ptr<Texture>& view_source) -> Texture_create_info;

    Device&                  device;
    Texture_type             type                  {Texture_type::texture_2d};
    erhe::dataformat::Format pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                     use_mipmaps           {false};
    bool                     fixed_sample_locations{true};
    bool                     sparse                {false};
    int                      sample_count          {0};
    int                      width                 {1};
    int                      height                {1};
    int                      depth                 {1};
    int                      array_layer_count     {0};
    int                      level_count           {0};
    int                      row_stride            {0};
    Buffer*                  buffer                {nullptr};
    uint64_t                 wrap_texture_name     {0};
    std::string              debug_label           {};
    std::shared_ptr<Texture> view_source           {};
    int                      view_base_level       {0};
    int                      view_base_array_layer {0};
};

class Texture : public erhe::Item<erhe::Item_base, erhe::Item_base, Texture, erhe::Item_kind::not_clonable>
{
public:
    Texture           (const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) = delete;
    ~Texture() noexcept override;

    Texture(Device& device, const Texture_create_info& create_info);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    using Create_info = Texture_create_info;

    [[nodiscard]] static auto is_array_target       (gl::Texture_target target) -> bool;
    [[nodiscard]] static auto get_storage_dimensions(gl::Texture_target target) -> int;
    [[nodiscard]] static auto get_mipmap_dimensions (gl::Texture_target target) -> int;
    [[nodiscard]] static auto get_mipmap_dimensions (Texture_type type) -> int;
    [[nodiscard]] static auto get_size_level_count  (int size) -> int;

    void upload(erhe::dataformat::Format internal_format, int width, int height = 1, int depth = 1, int array_layer_count = 0);

    void upload(
        const erhe::dataformat::Format      format,
        const std::span<const std::uint8_t> data,
        int                                 width,
        int                                 height = 1,
        int                                 depth = 1,
        int                                 array_layer = 0,
        int                                 level = 0,
        int                                 x = 0,
        int                                 y = 0,
        int                                 z = 0
    );
    void upload_subimage(
        const erhe::dataformat::Format      format,
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
    //// [[nodiscard]] auto get_sparse_tile_size() const -> Tile_size;

    [[nodiscard]] auto is_shown_in_ui() const -> bool;

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

void convert_texture_dimensions_from_gl(const gl::Texture_target target, int& width, int& height, int& depth, int& array_layer_count);
void convert_texture_dimensions_to_gl  (const gl::Texture_target target, int& width, int& height, int& depth, int array_layer_count);
void convert_texture_offset_to_gl      (const gl::Texture_target target, int& x, int& y, int& z, int array_layer);


[[nodiscard]] auto component_count(gl::Pixel_format pixel_format) -> size_t;
[[nodiscard]] auto byte_count(gl::Pixel_type pixel_type) -> size_t;
[[nodiscard]] auto get_upload_pixel_byte_count(const erhe::dataformat::Format pixelformat) -> size_t;
[[nodiscard]] auto get_format_and_type(const erhe::dataformat::Format pixelformat, gl::Pixel_format& format, gl::Pixel_type& type) -> bool;
[[nodiscard]] auto format_texture_handle(uint64_t handle) -> std::string;

constexpr uint64_t invalid_texture_handle = 0xffffffffu;


} // namespace erhe::graphics
