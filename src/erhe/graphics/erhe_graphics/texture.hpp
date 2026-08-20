#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_item/item.hpp"
#include "erhe_utility/debug_label.hpp"

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

    Device&                    device;
    uint64_t                   usage_mask            {0};
    Texture_type               type                  {Texture_type::texture_2d};
    erhe::dataformat::Format   pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                       use_mipmaps           {false};
    bool                       fixed_sample_locations{true};
    bool                       sparse                {false};
    int                        sample_count          {0};
    int                        width                 {1};
    int                        height                {1};
    int                        depth                 {1};
    int                        array_layer_count     {0};
    int                        level_count           {0};
    int                        row_stride            {0};
    Buffer*                    buffer                {nullptr};
    uint64_t                   wrap_texture_name     {0};
    erhe::utility::Debug_label debug_label           {};
    std::shared_ptr<Texture>   view_source           {};
    int                        view_base_level       {0};
    int                        view_base_array_layer {0};
};

class Texture;

class Texture_reference
{
public:
    virtual ~Texture_reference() noexcept;
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
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::texture; }

    // Implements Texture_reference
    auto get_referenced_texture() const -> const Texture* override;

    // Process-wide image memory accounting for memory reporting
    // (doc/reloadable-asset-loads.md). Estimated from the create info - format
    // times dimensions times levels times layers - not queried from the
    // allocator, so it is backend-neutral and approximate. Views and textures
    // wrapping an externally owned image are not counted, since they own no
    // allocation. Unlike the mesh pools, destroying a Texture really does
    // return its memory, so this figure drops when content is released.
    class Memory_statistics
    {
    public:
        std::size_t texture_count{0};
        std::size_t byte_count   {0};
    };
    [[nodiscard]] static auto get_memory_statistics() -> Memory_statistics;

    [[nodiscard]] static auto get_mipmap_dimensions(Texture_type type) -> int;
    [[nodiscard]] static auto get_size_level_count (int size) -> int;

    [[nodiscard]] auto get_debug_label           () const -> erhe::utility::Debug_label;
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

    void clear() const;

    // For texture_buffer textures: (re)associate with a buffer.
    // Can be called multiple times to change the associated buffer.
    void set_buffer(Buffer& buffer);

    // Content semantics, not a GPU property: true when the texture stores a
    // normal map as a two component X+Y map (Z reconstructed in shader) -
    // e.g. a KTX2 file encoded with `ktx encode --normal-mode`. The channel
    // layout follows the pixel format: X in RGB / Y in A for RGBA and ASTC,
    // X in R / Y in G for BC5. Set by the loader that decoded the image;
    // consumed by the shader variant selection when the texture is bound to
    // a material normal slot.
    void               set_two_component_normal(bool value) { m_two_component_normal = value; }
    [[nodiscard]] auto is_two_component_normal () const -> bool { return m_two_component_normal; }

private:
    std::unique_ptr<Texture_impl> m_impl;
    // Counted into the process-wide totals; 0 for views and wrapped images.
    std::size_t                   m_estimated_byte_count{0};
    bool                          m_two_component_normal{false};
};

[[nodiscard]] auto operator==(const Texture& lhs, const Texture& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Texture& lhs, const Texture& rhs) noexcept -> bool;

[[nodiscard]] auto format_texture_handle(uint64_t handle) -> std::string;

constexpr uint64_t invalid_texture_handle = 0xffffffffu;

[[nodiscard]] auto get_texture_level_count(int width, int height = 0, int depth = 0) -> int;


} // namespace erhe::graphics
