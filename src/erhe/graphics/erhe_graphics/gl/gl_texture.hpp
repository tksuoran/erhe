#pragma once

#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_utility/debug_label.hpp"
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
    [[nodiscard]] auto gl_name                   () const -> GLuint;
    [[nodiscard]] auto get_handle                () const -> uint64_t;
    [[nodiscard]] auto is_sparse                 () const -> bool;

    [[nodiscard]] auto get_gl_texture_target() const -> gl::Texture_target;


    void clear();
    void set_buffer(Buffer& buffer);

    // Cross-context publication (gl-worker-thread-contexts.md, "Cross-context publication"): a worker-created or
    // worker-uploaded texture carries a fence sync issued (and flushed) on
    // the worker. The main thread calls wait_publication() - a server-side
    // glWaitSync, once per object - before its first bind of the texture
    // (which is also rule 4's attach); null sync is a fast no-op.
    // publish_from_worker() is the producer half: called at storage
    // allocation and at the end of each worker-side pixel-upload method,
    // replacing any unconsumed earlier sync.
    // Both const: they mutate only the mutable synchronization
    // bookkeeping, and the upload paths reach the impl through
    // const Texture*.
    void publish_from_worker() const;
    void wait_publication   () const;

private:
    static constexpr const char* s_pool_name = "glTexture";

    Device&                    m_device;
    Gl_texture                 m_handle;
    Texture_type               m_type                  {Texture_type::texture_2d};
    erhe::dataformat::Format   m_pixelformat           {erhe::dataformat::Format::format_8_vec4_srgb};
    bool                       m_fixed_sample_locations{true};
    bool                       m_is_sparse             {false};
    bool                       m_allocated             {false};
    int                        m_sample_count          {0};
    int                        m_width                 {0};
    int                        m_height                {0};
    int                        m_depth                 {0};
    int                        m_array_layer_count     {0};
    int                        m_level_count           {0};
    Buffer*                    m_buffer                {nullptr};
    erhe::utility::Debug_label m_debug_label;

    // Publication sync: see publish_from_worker() / wait_publication()
    // above. Mutable because consumers reach the impl through const
    // Texture&; consuming the sync mutates only the synchronization
    // bookkeeping, never the logical texture.
    mutable Gl_publication_sync m_publication_sync;
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
// Texture_create_info::sample_count 0 and 1 both mean single-sample
// (texture_2d); only 2 and above select a multisample target. This is the
// contract every backend follows (get_vulkan_sample_count, Metal
// to_mtl_texture_type) and the only GL place that interprets the count.
[[nodiscard]] constexpr auto is_multisample_sample_count(int sample_count) -> bool
{
    return sample_count > 1;
}

[[nodiscard]] auto convert_to_gl_texture_target(Texture_type type, bool multisample, bool array) -> gl::Texture_target;


} // namespace erhe::graphics
