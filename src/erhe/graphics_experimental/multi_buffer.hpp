#ifndef multi_buffer_hpp_erhe_graphics
#define multi_buffer_hpp_erhe_graphics

#include "erhe/graphics/span.hpp"
#include "erhe/gl/strong_gl_enums.hpp"

namespace erhe::graphics
{

class Buffer;

// multiple buffers, circular/ring, one instance being written to
class Multi_buffer
{
public:
    static constexpr size_t buffer_count{4};

    Multi_buffer(gl::Buffer_target target,
                 size_t            element_count,
                 size_t            element_size,
                 bool              cpu_copy = false);

    auto current()
    -> std::shared_ptr<erhe::graphics::Buffer>;

    auto current_map() const
    -> gsl::span<std::byte>;

    void touch(size_t update_serial);

    auto get_last_update_serial() const
    -> size_t;

    void advance();

    void write(size_t byte_offset, gsl::span<const float> source) const
    {
        auto dst = current_map();
        erhe::graphics::write(dst, byte_offset, source);
    }

    void write(size_t byte_offset, const gl::Draw_elements_indirect_command& source) const
    {
        auto dst = current_map();
        erhe::graphics::write(dst, byte_offset, as_span(source));
    }

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::graphics::Buffer> buffer;
        gsl::span<std::byte>                    map_data;
        size_t                                  last_update_serial{0};
    };

    std::array<Entry, buffer_count> m_storage;
    size_t                          m_current_entry{0};
    std::vector<std::byte>          m_cpu_copy;
};

} // namespace erhe::graphics

#endif
