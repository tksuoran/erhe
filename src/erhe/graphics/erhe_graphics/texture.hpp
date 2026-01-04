#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_item/item.hpp"

#include <string>

namespace erhe::graphics {

class Buffer;
class Device;
class Sampler;
class Texture;

class Texture_create_info
{
public:
    [[nodiscard]] auto get_texture_level_count() const -> int;

    static auto make_view(Device& device, const std::shared_ptr<Texture>& view_source) -> Texture_create_info;

    Device&                  device;
    uint64_t                 usage_mask            {0};
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

class Texture;

class Texture_reference
{
public:
    [[nodiscard]] virtual auto get_referenced_texture() const -> const Texture* = 0;
};

class Texture_impl;
class Texture
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Texture, erhe::Item_kind::not_clonable>
    , public Texture_reference
{
public:
    Texture           (const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) = delete;
    ~Texture() noexcept override;

    Texture(Device& device, const Texture_create_info& create_info);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Texture"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    // Implements Texture_reference
    auto get_referenced_texture() const -> const Texture* override;

    [[nodiscard]] static auto get_mipmap_dimensions(Texture_type type) -> int;
    [[nodiscard]] static auto get_size_level_count (int size) -> int;

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
    [[nodiscard]] auto is_sparse                 () const -> bool;
    [[nodiscard]] auto get_impl                  () -> Texture_impl&;
    [[nodiscard]] auto get_impl                  () const -> const Texture_impl&;

private:
    std::unique_ptr<Texture_impl> m_impl;
};

[[nodiscard]] auto operator==(const Texture& lhs, const Texture& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Texture& lhs, const Texture& rhs) noexcept -> bool;

[[nodiscard]] auto format_texture_handle(uint64_t handle) -> std::string;

constexpr uint64_t invalid_texture_handle = 0xffffffffu;

[[nodiscard]] auto get_texture_level_count(int width, int height = 0, int depth = 0) -> int;


} // namespace erhe::graphics
