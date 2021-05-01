#ifndef uniform_buffer_range_hpp_erhe_graphics
#define uniform_buffer_range_hpp_erhe_graphics

#include <gsl/pointers>
#include <gsl/span>

namespace erhe::graphics
{

class Buffer;
class Shader_resource;

class Storage_buffer_range
{
public:
    Storage_buffer_range(gsl::not_null<Shader_resource*> block,
                         gsl::not_null<Buffer*>          buffer,
                         size_t                          block_count);

    ~Storage_buffer_range() = default;

    auto begin_edit()
    -> gsl::span<std::byte>;

    void end_edit();

    auto byte_offset() const
    -> size_t
    {
        return m_byte_offset;
    }

    auto byte_count() const
    -> size_t
    {
        return m_capacity_byte_count;
    }

    void flush();

    void flush(size_t byte_count);

    auto buffer()
    -> Buffer*
    {
        return m_buffer;
    }

private:
    //Shader_resource* m_uniform_block{nullptr};
    gsl::not_null<Buffer*> m_buffer;
    gsl::span<std::byte>   m_edit;
    size_t                 m_capacity_byte_count;
    size_t                 m_byte_offset{0};
    bool                   m_in_edit    {false};
};

} // namespace erhe::graphics

#endif
